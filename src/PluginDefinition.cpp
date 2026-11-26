// TabGroups for Notepad++ - organize open tabs into named groups, save them, reopen them.
// Everything runs locally; the plugin makes no network connections.
// Copyright (C) 2026 TabGroups plugin authors. License: GNU GPL v3 or later (see LICENSE-GPL-3.0.txt).
#include "PluginDefinition.h"
#include "menuCmdID.h"
#include "groups.h"
#include "resource.h"
#include "DockingFeature/DockingDlgInterface.h"
#include <commctrl.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstring>

FuncItem funcItem[nbFunc];
NppData nppData;

static const wchar_t* TITLE = L"TabGroups";
static HANDLE g_hModule = nullptr;
static tg::Store g_store;
static std::unordered_map<UINT_PTR, int> g_untitled;       // live bufferID -> group (untitled tabs only)
static std::unordered_map<UINT_PTR, std::wstring> g_lastPath; // bufferID -> last seen path (save-as / rename tracking)

static LRESULT N(UINT m, WPARAM w = 0, LPARAM l = 0) { return ::SendMessage(nppData._nppHandle, m, w, l); }

struct Pos { bool ok; int view; int index; };
static Pos posOf(UINT_PTR id) {
    if (!id) return {false, 0, 0};
    LRESULT p = N(NPPM_GETPOSFROMBUFFERID, id, 0);
    if ((int)p == -1) return {false, 0, 0};
    return {true, (int)(((unsigned)p) >> 30), (int)((unsigned)p & 0x3FFFFFFF)};
}
static void activateDoc(const Pos& p) { N(NPPM_ACTIVATEDOC, p.view, p.index); }

static std::wstring pathOf(UINT_PTR id) {
    wchar_t b[4096]; b[0] = 0;
    N(NPPM_GETFULLPATHFROMBUFFERID, id, (LPARAM)b);
    return std::wstring(b);
}
static std::wstring baseName(const std::wstring& p) {
    size_t k = p.find_last_of(L'\\');
    return k == std::wstring::npos ? p : p.substr(k + 1);
}
static bool isUntitled(const std::wstring& p) { return p.find(L'\\') == std::wstring::npos; }

static void msg(const std::wstring& s, UINT flags = MB_OK | MB_ICONINFORMATION) {
    ::MessageBoxW(nppData._nppHandle, s.c_str(), TITLE, flags);
}

struct OpenDoc { UINT_PTR id; int view; int index; std::wstring path; bool untitled; };
static std::vector<OpenDoc> collectDocs() {
    std::vector<OpenDoc> v;
    for (int view = 0; view < 2; ++view) {
        int n = (int)N(NPPM_GETNBOPENFILES, 0, view == 0 ? PRIMARY_VIEW : SECOND_VIEW);
        for (int i = 0; i < n; ++i) {
            UINT_PTR id = (UINT_PTR)N(NPPM_GETBUFFERIDFROMPOS, i, view);
            if (!id) continue;
            OpenDoc d; d.id = id; d.view = view; d.index = i;
            d.path = pathOf(id); d.untitled = isUntitled(d.path);
            v.push_back(d);
        }
    }
    return v;
}

// ---------- persistence ----------
static std::wstring iniPath() {
    wchar_t b[MAX_PATH] = L"";
    N(NPPM_GETPLUGINSCONFIGDIR, MAX_PATH, (LPARAM)b);
    std::wstring d = b;
    if (!d.empty() && d.back() != L'\\') d += L'\\';
    return d + L"TabGroups.ini";
}
static bool g_loaded = false;       // true only after a successful (or "no file yet") load. Saving is blocked until then,
static bool g_loadFailed = false;   // so a failed or early load can never overwrite the user's saved groups.
static const char* HEADER = "# TabGroups groups file v1";

static bool fileExists(const std::wstring& p) { return ::GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

static void backupBadFile(const std::wstring& p) {
    std::wstring bak = p + L".bad-" + std::to_wstring((unsigned long long)::GetTickCount64());
    ::MoveFileExW(p.c_str(), bak.c_str(), 0);   // keep the user's file, never delete it
}

static void loadStore() {
    if (g_loaded) return;
    std::wstring path = iniPath();
    if (!fileExists(path)) { g_loaded = true; return; }              // first run: nothing to load
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { g_loadFailed = true; return; }   // locked/unreadable: do not touch it
    LARGE_INTEGER sz{};
    bool ok = ::GetFileSizeEx(h, &sz) && sz.QuadPart >= 0 && sz.QuadPart <= 8 * 1024 * 1024;
    std::string buf;
    if (ok) {
        buf.assign((size_t)sz.QuadPart, '\0');
        DWORD rd = 0;
        if (sz.QuadPart > 0) ok = ::ReadFile(h, &buf[0], (DWORD)sz.QuadPart, &rd, NULL) && rd == (DWORD)sz.QuadPart;
    }
    ::CloseHandle(h);
    if (!ok) { g_loadFailed = true; return; }
    if (buf.size() >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) buf.erase(0, 3);
    if (!buf.empty() && buf.compare(0, strlen(HEADER), HEADER) != 0) {
        // Not our file format (or damaged): set it aside, start empty, never overwrite it.
        backupBadFile(path);
        g_loaded = true;
        return;
    }
    g_store.parse(buf);
    g_loaded = true;
}
// Atomic save: write a temp file next to the target, flush it, then replace the target in one step.
static bool saveStore() {
    if (!g_loaded) return false;      // never write before the saved groups were read
    std::wstring path = iniPath();
    std::wstring dir = path.substr(0, path.find_last_of(L'\\'));
    ::CreateDirectoryW(dir.c_str(), NULL);
    std::string data = g_store.serialize();
    std::wstring tmp = path + L".tmp";
    HANDLE h = ::CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    bool ok = ::WriteFile(h, data.data(), (DWORD)data.size(), &wr, NULL) && wr == (DWORD)data.size() && ::FlushFileBuffers(h);
    ::CloseHandle(h);
    if (!ok) { ::DeleteFileW(tmp.c_str()); return false; }
    if (!::MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        ::DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

// ---------- autosave failure reporting ----------
// One modal warning when saving first fails, a persistent status line in the panel while it keeps failing,
// and the Save button / "Save groups now" retries. Success clears both.
static bool g_saveFailed = false;
static std::wstring g_statusText;
static void applyStatus();
static void noteSaveResult(bool ok) {
    if (ok) {
        if (g_saveFailed) { g_saveFailed = false; g_statusText.clear(); applyStatus(); }
        return;
    }
    bool first = !g_saveFailed;
    g_saveFailed = true;
    g_statusText = g_loadFailed ? L"NOT SAVED: groups file unreadable (left untouched). Press Save to retry."
                                : L"NOT SAVED: could not write groups file. Press Save to retry.";
    applyStatus();
    if (first)
        ::MessageBoxW(nppData._nppHandle,
            g_loadFailed ? L"Your groups could not be saved, because the saved groups file (TabGroups.ini) could not be read and was left untouched. Your changes are kept in memory only. Close other programs using that file, then press Save in the Tab Groups panel (or Plugins > TabGroups > Save groups now) to retry.\n\nThis warning appears once until saving works again."
                         : L"Your groups could not be saved (is the Notepad++ plugins config folder writable?). Your changes are kept in memory only. Press Save in the Tab Groups panel (or Plugins > TabGroups > Save groups now) to retry.\n\nThis warning appears once until saving works again.",
            TITLE, MB_OK | MB_ICONWARNING);
}
static bool autoSave() { bool ok = saveStore(); noteSaveResult(ok); return ok; }

// ---------- small dialogs ----------
struct InputParams { const wchar_t* title; const wchar_t* prompt; std::wstring* value; };
static INT_PTR CALLBACK inputProc(HWND d, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_INITDIALOG: {
            InputParams* p = (InputParams*)l;
            ::SetWindowLongPtrW(d, GWLP_USERDATA, (LONG_PTR)p);
            ::SetWindowTextW(d, p->title);
            ::SetDlgItemTextW(d, IDC_PROMPT, p->prompt);
            ::SetDlgItemTextW(d, IDC_TEXT, p->value->c_str());
            ::SendDlgItemMessageW(d, IDC_TEXT, EM_SETSEL, 0, -1);
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(w) == IDOK) {
                InputParams* p = (InputParams*)::GetWindowLongPtrW(d, GWLP_USERDATA);
                wchar_t b[512]; b[0] = 0;
                ::GetDlgItemTextW(d, IDC_TEXT, b, 512);
                p->value->assign(b);
                ::EndDialog(d, 1);
                return TRUE;
            }
            if (LOWORD(w) == IDCANCEL) { ::EndDialog(d, 0); return TRUE; }
            break;
    }
    return FALSE;
}
static bool inputBox(const wchar_t* title, const wchar_t* prompt, const std::wstring& def, std::wstring& out) {
    out = def;
    InputParams p{ title, prompt, &out };
    return ::DialogBoxParamW((HINSTANCE)g_hModule, MAKEINTRESOURCEW(IDD_INPUT), nppData._nppHandle, inputProc, (LPARAM)&p) == 1;
}

static INT_PTR CALLBACK assignProc(HWND d, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_INITDIALOG: {
            ::SetWindowLongPtrW(d, GWLP_USERDATA, (LONG_PTR)(int*)l);
            HWND lb = ::GetDlgItem(d, IDC_LIST);
            for (auto& n : g_store.names) ::SendMessageW(lb, LB_ADDSTRING, 0, (LPARAM)n.c_str());
            ::SendMessageW(lb, LB_SETCURSEL, 0, 0);
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(w) == IDOK || (LOWORD(w) == IDC_LIST && HIWORD(w) == LBN_DBLCLK)) {
                int* out = (int*)::GetWindowLongPtrW(d, GWLP_USERDATA);
                *out = (int)::SendDlgItemMessageW(d, IDC_LIST, LB_GETCURSEL, 0, 0);
                ::EndDialog(d, 1);
                return TRUE;
            }
            if (LOWORD(w) == IDCANCEL) { ::EndDialog(d, 0); return TRUE; }
            break;
    }
    return FALSE;
}
static int pickGroup() {
    int out = -1;
    if (::DialogBoxParamW((HINSTANCE)g_hModule, MAKEINTRESOURCEW(IDD_ASSIGN), nppData._nppHandle, assignProc, (LPARAM)&out) == 1)
        return out;
    return -1;
}

// ---------- dockable panel ----------
enum NodeKind { K_GROUP = 0, K_TAB = 1, K_CLOSED = 2, K_UNGROUPED = 3 };
struct NodeInfo { int kind; int group; UINT_PTR bufId; std::wstring path; };

static void rebuildTree();

class GroupsPanel : public DockingDlgInterface {
public:
    GroupsPanel() : DockingDlgInterface(IDD_TABGROUPS_DLG) {}
    RECT m_treeRect{}; int m_statusH = 0;

protected:
    INT_PTR CALLBACK run_dlgProc(UINT message, WPARAM wParam, LPARAM lParam) override {
        switch (message) {
            case WM_INITDIALOG: {
                RECT r;
                ::GetWindowRect(::GetDlgItem(_hSelf, IDC_TREE), &r);
                ::MapWindowPoints(NULL, _hSelf, (POINT*)&r, 2);
                m_treeRect = r;
                { RECT sr; ::GetWindowRect(::GetDlgItem(_hSelf, IDC_STATUS), &sr); m_statusH = sr.bottom - sr.top; }
                applyStatus();
                rebuildTree();
                return TRUE;
            }
            case WM_SIZE: {
                RECT c; ::GetClientRect(_hSelf, &c);
                int w = c.right - m_treeRect.left - 4;
                int hgt = c.bottom - m_treeRect.top - 4 - m_statusH - 2;
                ::SetWindowPos(::GetDlgItem(_hSelf, IDC_STATUS), NULL, m_treeRect.left, c.bottom - m_statusH - 2, w > 20 ? w : 20, m_statusH, SWP_NOZORDER);
                ::SetWindowPos(::GetDlgItem(_hSelf, IDC_TREE), NULL,
                               m_treeRect.left, m_treeRect.top, w > 20 ? w : 20, hgt > 20 ? hgt : 20, SWP_NOZORDER);
                return TRUE;
            }
            case WM_COMMAND:
                switch (LOWORD(wParam)) {
                    case IDC_BTN_NEWGRP: newGroupCmd(); return TRUE;
                    case IDC_BTN_ASSIGN: assignCurrentCmd(); return TRUE;
                    case IDC_BTN_SAVE: saveGroupsCmd(); return TRUE;
                }
                return FALSE;
            case WM_NOTIFY: {
                NMHDR* h = (NMHDR*)lParam;
                if (h->idFrom != IDC_TREE) break;
                HWND tree = ::GetDlgItem(_hSelf, IDC_TREE);
                if (h->code == TVN_DELETEITEM) {
                    NMTREEVIEWW* tv = (NMTREEVIEWW*)lParam;
                    delete (NodeInfo*)tv->itemOld.lParam;
                    return TRUE;
                }
                if (h->code == NM_DBLCLK) {
                    HTREEITEM it = TreeView_GetSelection(tree);
                    NodeInfo* np = it ? (NodeInfo*)getItemParam(tree, it) : nullptr;
                    if (np) { NodeInfo copy = *np; onActivateNode(&copy); }
                    return TRUE;
                }
                if (h->code == NM_RCLICK) {
                    onRightClick(tree);
                    return TRUE;
                }
                return FALSE;
            }
        }
        return DockingDlgInterface::run_dlgProc(message, wParam, lParam);
    }

private:
    static LPARAM getItemParam(HWND tree, HTREEITEM it) {
        TVITEMW ti{}; ti.mask = TVIF_PARAM; ti.hItem = it;
        TreeView_GetItem(tree, &ti);
        return ti.lParam;
    }
    void onActivateNode(NodeInfo* ni) {
        if (ni->kind == K_TAB || ni->kind == K_UNGROUPED) {
            Pos p = posOf(ni->bufId);
            if (p.ok) activateDoc(p);
        } else if (ni->kind == K_CLOSED) {
            openPath(ni->path);
        }
    }
    static bool openPath(const std::wstring& p) {
        DWORD a = ::GetFileAttributesW(p.c_str());
        if (a == INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_DIRECTORY)) {
            msg(L"File not found:\n" + p, MB_OK | MB_ICONWARNING);
            return false;
        }
        return N(NPPM_DOOPEN, 0, (LPARAM)p.c_str()) != 0;
    }
    static int viewCount(UINT_PTR id) {   // 0, 1, or 2 (same buffer shown in both views)
        int n = 0;
        for (auto& d : collectDocs()) if (d.id == id) ++n;
        return n;
    }
    static bool inGroup(const OpenDoc& d, int g) {
        return d.untitled ? (g_untitled.count(d.id) && g_untitled[d.id] == g) : (g_store.groupOf(d.path) == g);
    }
    // Closes ONE tab of this buffer (one view). Returns true if a tab was closed (or the buffer was already gone),
    // false if the user cancelled the save prompt or the tab could not be activated.
    static bool closeDoc(UINT_PTR id) {
        Pos p = posOf(id);
        if (!p.ok) return true;
        int countBefore = viewCount(id);
        UINT_PTR before = (UINT_PTR)N(NPPM_GETCURRENTBUFFERID);
        activateDoc(p);
        if ((UINT_PTR)N(NPPM_GETCURRENTBUFFERID) != id) return false;   // never close a tab we could not activate
        N(NPPM_MENUCOMMAND, 0, IDM_FILE_CLOSE);   // Notepad++ prompts for unsaved changes
        bool closed = viewCount(id) < countBefore;   // false when the user cancelled
        if (before && before != id) { Pos b = posOf(before); if (b.ok) activateDoc(b); }   // put the user back where they were
        return closed;
    }
    void onRightClick(HWND tree) {
        POINT pt; ::GetCursorPos(&pt);
        POINT cp = pt; ::ScreenToClient(tree, &cp);
        TVHITTESTINFO ht{}; ht.pt = cp;
        HTREEITEM it = TreeView_HitTest(tree, &ht);
        if (it) TreeView_SelectItem(tree, it);
        NodeInfo* np = it ? (NodeInfo*)getItemParam(tree, it) : nullptr;
        // Work on a copy: Notepad++ notifications rebuild the tree (and free its nodes) while a command runs.
        NodeInfo copyInfo{}; if (np) copyInfo = *np;
        NodeInfo* ni = np ? &copyInfo : nullptr;

        enum { C_NEW = 1, C_SAVE, C_REFRESH, C_ACTIVATE, C_CLOSETAB, C_REMOVE, C_OPENFILE,
               C_RENAME, C_DELGROUP, C_REOPEN, C_OPENALL, C_CLOSEALL, C_CLOSEOTHERS, C_ASSIGN_BASE = 100 };

        HMENU m = ::CreatePopupMenu();
        if (!ni) {
            ::AppendMenuW(m, MF_STRING, C_NEW, L"New group...");
            ::AppendMenuW(m, MF_STRING, C_SAVE, L"Save groups now");
            ::AppendMenuW(m, MF_STRING, C_REFRESH, L"Refresh");
        } else if (ni->kind == K_GROUP) {
            ::AppendMenuW(m, MF_STRING, C_OPENALL, L"Open all files");
            ::AppendMenuW(m, MF_STRING, C_CLOSEALL, L"Close all tabs in group");
            if (ni->group >= 0) ::AppendMenuW(m, MF_STRING, C_CLOSEOTHERS, L"Close all tabs except this group");
            ::AppendMenuW(m, MF_SEPARATOR, 0, NULL);
            ::AppendMenuW(m, MF_STRING, C_RENAME, L"Rename group...");
            ::AppendMenuW(m, MF_STRING, C_DELGROUP, L"Delete group (tabs stay open)");
            ::AppendMenuW(m, MF_SEPARATOR, 0, NULL);
            ::AppendMenuW(m, MF_STRING, C_NEW, L"New group...");
        } else if (ni->kind == K_TAB) {
            ::AppendMenuW(m, MF_STRING, C_ACTIVATE, L"Activate");
            ::AppendMenuW(m, MF_STRING, C_CLOSETAB, L"Close tab");
            ::AppendMenuW(m, MF_STRING, C_REMOVE, L"Remove from group");
        } else if (ni->kind == K_CLOSED) {
            ::AppendMenuW(m, MF_STRING, C_OPENFILE, L"Open file");
            ::AppendMenuW(m, MF_STRING, C_REMOVE, L"Remove from group");
        } else { // K_UNGROUPED
            ::AppendMenuW(m, MF_STRING, C_ACTIVATE, L"Activate");
            ::AppendMenuW(m, MF_STRING, C_CLOSETAB, L"Close tab");
            HMENU sub = ::CreatePopupMenu();
            for (size_t i = 0; i < g_store.names.size(); ++i)
                ::AppendMenuW(sub, MF_STRING, C_ASSIGN_BASE + (UINT)i, g_store.names[i].c_str());
            ::AppendMenuW(m, MF_POPUP, (UINT_PTR)sub, L"Assign to group");
            if (g_store.names.empty()) ::EnableMenuItem(m, 2, MF_BYPOSITION | MF_GRAYED);
        }
        int cmd = ::TrackPopupMenu(m, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, _hSelf, NULL);
        ::DestroyMenu(m);
        if (cmd == 0) return;

        switch (cmd) {
            case C_NEW: newGroupCmd(); break;
            case C_SAVE: saveGroupsCmd(); break;
            case C_REFRESH: rebuildTree(); break;
            case C_ACTIVATE: if (ni) { Pos p = posOf(ni->bufId); if (p.ok) activateDoc(p); } break;
            case C_CLOSETAB: if (ni) { closeDoc(ni->bufId); rebuildTree(); } break;
            case C_OPENFILE: if (ni) { openPath(ni->path); rebuildTree(); } break;
            case C_REMOVE:
                if (ni) {
                    if (ni->kind == K_TAB && ni->path.empty()) g_untitled.erase(ni->bufId);
                    else g_store.unassign(ni->path);
                    autoSave(); rebuildTree();
                }
                break;
            case C_RENAME:
                if (ni && ni->group >= 0 && ni->group < (int)g_store.names.size()) {
                    std::wstring name;
                    if (inputBox(L"Rename group", L"Group name:", g_store.names[ni->group], name) && !name.empty()) {
                        if (!g_store.renameGroup(ni->group, name)) msg(L"A group with this name already exists.");
                        autoSave(); rebuildTree();
                    }
                }
                break;
            case C_DELGROUP:
                if (ni && ni->group >= 0 && ni->group < (int)g_store.names.size()) {
                    std::wstring q = L"Delete group \"" + g_store.names[ni->group] + L"\"? Tabs stay open.";
                    if (::MessageBoxW(nppData._nppHandle, q.c_str(), TITLE, MB_YESNO | MB_ICONQUESTION) == IDYES) {
                        int g = ni->group;
                        g_store.removeGroup(g);
                        for (auto it = g_untitled.begin(); it != g_untitled.end();) {
                            if (it->second == g) it = g_untitled.erase(it);
                            else { if (it->second > g) it->second -= 1; ++it; }
                        }
                        autoSave(); rebuildTree();
                    }
                }
                break;
            case C_OPENALL:
                if (ni) { openAllInGroup(ni->group); rebuildTree(); }
                break;
            case C_CLOSEALL:
                if (ni) { closeAllInGroup(ni->group); rebuildTree(); }
                break;
            case C_CLOSEOTHERS:
                if (ni && ni->group >= 0) { closeAllExceptGroup(ni->group); rebuildTree(); }
                break;
            default:
                if (cmd >= C_ASSIGN_BASE && ni && ni->kind == K_UNGROUPED) {
                    int g = cmd - C_ASSIGN_BASE;
                    if (g >= 0 && g < (int)g_store.names.size()) assignDoc(ni->bufId, ni->path, g);
                }
                break;
        }
    }
    static void openAllInGroup(int g) {
        if (g < 0 || g >= (int)g_store.names.size()) return;
        auto docs = collectDocs();
        int opened = 0, missing = 0;
        for (auto& p : g_store.members[g]) {
            bool already = false;
            for (auto& d : docs) if (!d.untitled && tg::lower(d.path) == tg::lower(p)) { already = true; break; }
            if (already) continue;
            DWORD a = ::GetFileAttributesW(p.c_str());
            if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY)) {
                if (N(NPPM_DOOPEN, 0, (LPARAM)p.c_str())) ++opened;
            } else ++missing;
        }
        if (missing > 0)
            msg(L"Opened " + std::to_wstring(opened) + L" file(s). " + std::to_wstring(missing) +
                L" file(s) were not found (moved or deleted?).", MB_OK | MB_ICONWARNING);
    }
    // Close every open tab that is NOT in group g. Works from a snapshot of buffer IDs taken up front, so tab
    // reordering or notifications while closing cannot change what gets closed. Each tab is re-checked just before
    // it is closed. A buffer shown in both views is closed in both views unless it belongs to the group.
    // Stops at the first Cancel on a save prompt. Group membership is never changed.
    static void closeAllExceptGroup(int g) {
        if (g < 0 || g >= (int)g_store.names.size()) return;
        auto docs = collectDocs();
        bool anyMemberOpen = false;
        std::vector<UINT_PTR> victims;
        for (auto& d : docs) {
            if (inGroup(d, g)) { anyMemberOpen = true; continue; }
            bool dup = false;
            for (auto v : victims) if (v == d.id) { dup = true; break; }
            if (!dup) victims.push_back(d.id);
        }
        // A buffer in the group that is also open in the other view is a member in both views (same buffer ID),
        // so it never lands in victims. Same ID cannot be both a member and a victim.
        if (!anyMemberOpen) {
            msg(L"This group has no open tabs, so nothing was closed. (Use \"Open all files\" first.)");
            return;
        }
        for (UINT_PTR id : victims) {
            for (int pass = 0; pass < 2; ++pass) {          // at most one tab per view
                if (!posOf(id).ok) break;                   // already gone
                bool member = false;
                for (auto& d : collectDocs()) if (d.id == id && inGroup(d, g)) { member = true; break; }
                if (member) break;                          // became a member meanwhile: keep it
                if (!closeDoc(id)) return;                  // user cancelled or tab could not be activated: stop
            }
        }
    }
    static void closeAllInGroup(int g) {
        for (int guard = 0; guard < 300; ++guard) {
            auto docs = collectDocs();
            UINT_PTR target = 0;
            for (auto& d : docs) {
                bool inG = d.untitled ? (g_untitled.count(d.id) && g_untitled[d.id] == g)
                                      : (g_store.groupOf(d.path) == g);
                if (inG) { target = d.id; break; }
            }
            if (!target) break;
            if (!closeDoc(target)) break;   // user cancelled a save prompt
        }
    }
public:
    static void assignDoc(UINT_PTR id, const std::wstring& path, int g) {
        if (path.empty()) { // untitled member node carries empty path; keyed by buffer id
            g_untitled[id] = g;
            msg(L"Done. This tab is unsaved, so it stays in the group only until the tab closes or Notepad++ exits. Save the file to keep it in the group permanently.");
        } else if (isUntitled(path)) {
            g_untitled[id] = g;
            msg(L"Done. This tab is unsaved (\"" + path + L"\"), so it stays in the group only until the tab closes or Notepad++ exits. Save the file to keep it in the group permanently.");
        } else {
            g_store.assign(path, g);
        }
        autoSave(); rebuildTree();
    }
};

static GroupsPanel g_panel;
static void applyStatus() {
    if (!g_panel.isCreated()) return;
    HWND st = ::GetDlgItem(g_panel.getHSelf(), IDC_STATUS);
    if (st) ::SetWindowTextW(st, g_statusText.c_str());
}
static bool g_panelVisible = false;

// ---------- tree build ----------
static HTREEITEM addNode(HWND tree, HTREEITEM parent, const std::wstring& text, NodeInfo* ni) {
    TVINSERTSTRUCTW is{};
    is.hParent = parent;
    is.hInsertAfter = TVI_LAST;
    is.item.mask = TVIF_TEXT | TVIF_PARAM;
    is.item.pszText = (LPWSTR)text.c_str();
    is.item.lParam = (LPARAM)ni;
    return TreeView_InsertItem(tree, &is);
}

static void rebuildTree() {
    if (!g_panel.isCreated()) return;
    HWND tree = ::GetDlgItem(g_panel.getHSelf(), IDC_TREE);
    if (!tree) return;
    auto docs = collectDocs();
    g_lastPath.clear();
    for (auto& d : docs) if (!d.untitled) g_lastPath[d.id] = d.path;
    // prune untitled memberships whose tab is gone
    for (auto it = g_untitled.begin(); it != g_untitled.end();) {
        bool alive = false;
        for (auto& d : docs) if (d.id == it->first) { alive = true; break; }
        if (!alive) it = g_untitled.erase(it); else ++it;
    }

    ::SendMessageW(tree, WM_SETREDRAW, FALSE, 0);
    TreeView_DeleteAllItems(tree);   // frees NodeInfo via TVN_DELETEITEM

    for (size_t gi = 0; gi < g_store.names.size(); ++gi) {
        std::wstring label = g_store.names[gi];
        HTREEITEM gnode = addNode(tree, TVI_ROOT, label, new NodeInfo{ K_GROUP, (int)gi, 0, L"" });
        // saved members
        for (auto& mp : g_store.members[gi]) {
            const OpenDoc* od = nullptr;
            for (auto& d : docs) if (!d.untitled && tg::lower(d.path) == tg::lower(mp)) { od = &d; break; }
            if (od)
                addNode(tree, gnode, baseName(mp), new NodeInfo{ K_TAB, (int)gi, od->id, mp });
            else
                addNode(tree, gnode, baseName(mp) + L"  (not open)", new NodeInfo{ K_CLOSED, (int)gi, 0, mp });
        }
        // untitled members
        for (auto& d : docs)
            if (d.untitled && g_untitled.count(d.id) && g_untitled[d.id] == (int)gi)
                addNode(tree, gnode, d.path + L"  (unsaved)", new NodeInfo{ K_TAB, (int)gi, d.id, L"" });
        TreeView_Expand(tree, gnode, TVE_EXPAND);
    }

    HTREEITEM un = addNode(tree, TVI_ROOT, L"Ungrouped (open tabs)", new NodeInfo{ K_GROUP, -1, 0, L"" });
    for (auto& d : docs) {
        bool grouped = d.untitled ? (g_untitled.count(d.id) > 0) : (g_store.groupOf(d.path) >= 0);
        if (!grouped)
            addNode(tree, un, d.untitled ? d.path : baseName(d.path), new NodeInfo{ K_UNGROUPED, -1, d.id, d.untitled ? L"" : d.path });
    }
    TreeView_Expand(tree, un, TVE_EXPAND);
    ::SendMessageW(tree, WM_SETREDRAW, TRUE, 0);
    ::InvalidateRect(tree, NULL, TRUE);
}

// ---------- commands ----------
bool setCommand(size_t index, const TCHAR *cmdName, PFUNCPLUGINCMD pFunc, ShortcutKey *sk, bool check0nInit) {
    if (index >= (size_t)nbFunc || !pFunc) return false;
    lstrcpyn(funcItem[index]._itemName, cmdName, 63);
    funcItem[index]._pFunc = pFunc;
    funcItem[index]._init2Check = check0nInit;
    funcItem[index]._pShKey = sk;
    return true;
}

void pluginInit(HANDLE hModule) {
    g_hModule = hModule;
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_TREEVIEW_CLASSES };
    ::InitCommonControlsEx(&icc);
}
void pluginCleanUp() {}
void commandMenuCleanUp() {}

void commandMenuInit() {
    setCommand(0, TEXT("Show / Hide Tab Groups panel"), togglePanel);
    setCommand(1, TEXT("New group..."), newGroupCmd);
    setCommand(2, TEXT("Assign current tab to group..."), assignCurrentCmd);
    setCommand(3, TEXT("Save groups now"), saveGroupsCmd);
    setCommand(4, TEXT("About TabGroups..."), aboutBox);
}

void togglePanel() {
    if (!g_panel.isCreated()) {
        g_panel.init((HINSTANCE)g_hModule, nppData._nppHandle);
        tTbData data{};
        g_panel.create(&data);
        data.uMask = DWS_DF_CONT_LEFT;
        data.pszModuleName = g_panel.getPluginFileName();
        data.dlgID = 0;   // funcItem[0] tracks the check mark
        N(NPPM_DMMREGASDCKDLG, 0, (LPARAM)&data);
        g_panelVisible = false;
    }
    g_panelVisible = !g_panelVisible;
    g_panel.display(g_panelVisible);
    N(NPPM_SETMENUITEMCHECK, funcItem[0]._cmdID, g_panelVisible ? TRUE : FALSE);
}

void newGroupCmd() {
    std::wstring name;
    if (!inputBox(L"New group", L"Group name (e.g. Work, Logs, Personal):", L"", name)) return;
    while (!name.empty() && (name.back() == L' ' || name.back() == L'\t')) name.pop_back();
    if (name.empty()) return;
    if (!g_store.addGroup(name)) { msg(L"A group with this name already exists."); return; }
    autoSave(); rebuildTree();
}

void assignCurrentCmd() {
    if (g_store.names.empty()) { msg(L"Create a group first (Plugins > TabGroups > New group)."); return; }
    UINT_PTR id = (UINT_PTR)N(NPPM_GETCURRENTBUFFERID);
    if (!id) { msg(L"No active tab."); return; }
    int g = pickGroup();
    if (g < 0 || g >= (int)g_store.names.size()) return;
    GroupsPanel::assignDoc(id, pathOf(id), g);
}

void saveGroupsCmd() {
    if (g_loadFailed) {
        // Retry reading the saved file first. Edits made since startup are merged on top, so nothing is lost either way.
        tg::Store mem = g_store; auto oldUntitled = g_untitled;
        g_loadFailed = false; g_store = tg::Store(); loadStore();
        if (g_loaded) {
            g_untitled.clear();
            for (size_t i = 0; i < mem.names.size(); ++i) {
                g_store.addGroup(mem.names[i]);
                int ng = g_store.findGroup(mem.names[i]);
                for (auto& m : mem.members[i]) g_store.assign(m, ng);
                for (auto& u : oldUntitled) if (u.second == (int)i) g_untitled[u.first] = ng;
            }
            rebuildTree();
        } else { g_store = mem; g_untitled = oldUntitled; }
    }
    if (saveStore()) { noteSaveResult(true); msg(L"Groups saved."); }
    else {
        g_saveFailed = true;                                     // keep the status line; this explicit click gets its own message
        g_statusText = g_loadFailed ? L"NOT SAVED: groups file unreadable (left untouched). Press Save to retry." : L"NOT SAVED: could not write groups file. Press Save to retry.";
        applyStatus();
        msg(g_loadFailed ? L"Groups were not saved: the saved groups file could not be read, so it was left untouched." : L"Groups could not be saved (is the Notepad++ plugins config folder writable?).", MB_OK | MB_ICONWARNING);
    }
}

void aboutBox() {
    msg(L"TabGroups 0.1.3 (private beta)\n\n"
        L"Organize open tabs into named groups (work, logs, personal, ...), save the groups, and reopen them later.\n\n"
        L"Groups are stored in TabGroups.ini in the Notepad++ plugins config folder.\n"
        L"Unsaved (\"new\") tabs keep their group only until closed.\n\n"
        L"License: GNU GPL v3 or later. Source code is included with the download.");
}

void onNotification(SCNotification *notifyCode) {
    if (!notifyCode) return;
    switch (notifyCode->nmhdr.code) {
        case NPPN_READY:
            loadStore();
            rebuildTree();
            break;
        case NPPN_BUFFERACTIVATED:
        case NPPN_FILEOPENED:
        case NPPN_DOCORDERCHANGED:
            rebuildTree();
            break;
        case NPPN_FILECLOSED: {
            UINT_PTR id = (UINT_PTR)notifyCode->nmhdr.idFrom;
            g_untitled.erase(id);
            g_lastPath.erase(id);
            rebuildTree();
            break;
        }
        case NPPN_FILESAVED:
        case NPPN_FILERENAMED: {
            UINT_PTR id = (UINT_PTR)notifyCode->nmhdr.idFrom;
            if (!id) break;
            std::wstring np = pathOf(id);
            if (np.empty() || isUntitled(np)) break;
            auto uit = g_untitled.find(id);
            if (uit != g_untitled.end()) {
                int g = uit->second;
                g_untitled.erase(uit);
                g_store.assign(np, g);
                autoSave();
            } else {
                auto lp = g_lastPath.find(id);
                if (lp != g_lastPath.end() && tg::lower(lp->second) != tg::lower(np) && g_store.changePath(lp->second, np))
                    autoSave();
            }
            rebuildTree();
            break;
        }
        case NPPN_SHUTDOWN:
            saveStore();
            break;
        default:
            break;
    }
}
