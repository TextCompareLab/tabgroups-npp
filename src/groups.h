// TabGroups for Notepad++ - pure group-membership logic, no Windows calls (Linux-testable).
// Copyright (C) 2026 TabGroups plugin authors. License: GNU GPL v3 or later.
#pragma once
#include <string>
#include <vector>
#include <cwctype>

namespace tg {

inline std::wstring lower(std::wstring s) { for (auto& c : s) c = (wchar_t)towlower((wint_t)c); return s; }

inline std::string wideToUtf8(const std::wstring& w) {
    std::string o;
    for (wchar_t c : w) {
        unsigned u = (unsigned)c;
        if (u < 0x80) o += (char)u;
        else if (u < 0x800) { o += (char)(0xC0 | (u >> 6)); o += (char)(0x80 | (u & 63)); }
        else { o += (char)(0xE0 | (u >> 12)); o += (char)(0x80 | ((u >> 6) & 63)); o += (char)(0x80 | (u & 63)); }
    }
    return o;
}
inline std::wstring utf8ToWide(const std::string& s) {
    std::wstring o;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) { o += (wchar_t)c; ++i; }
        else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) { o += (wchar_t)(((c & 31) << 6) | (s[i+1] & 63)); i += 2; }
        else if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) { o += (wchar_t)(((c & 15) << 12) | ((s[i+1] & 63) << 6) | (s[i+2] & 63)); i += 3; }
        else ++i; // skip invalid
    }
    return o;
}

struct Store {
    std::vector<std::wstring> names;
    std::vector<std::vector<std::wstring>> members;

    int findGroup(const std::wstring& name) const {
        std::wstring n = lower(name);
        for (size_t i = 0; i < names.size(); ++i) if (lower(names[i]) == n) return (int)i;
        return -1;
    }
    int groupOf(const std::wstring& path) const {
        std::wstring p = lower(path);
        for (size_t i = 0; i < members.size(); ++i)
            for (auto& m : members[i]) if (lower(m) == p) return (int)i;
        return -1;
    }
    bool addGroup(const std::wstring& name) {
        if (name.empty() || findGroup(name) != -1) return false;
        names.push_back(name); members.emplace_back(); return true;
    }
    bool renameGroup(int i, const std::wstring& name) {
        if (i < 0 || i >= (int)names.size() || name.empty()) return false;
        int ex = findGroup(name); if (ex != -1 && ex != i) return false;
        names[i] = name; return true;
    }
    void removeGroup(int i) {
        if (i < 0 || i >= (int)names.size()) return;
        names.erase(names.begin() + i); members.erase(members.begin() + i);
    }
    bool assign(const std::wstring& path, int g) {
        if (g < 0 || g >= (int)names.size() || path.empty()) return false;
        unassign(path);
        members[g].push_back(path); return true;
    }
    void unassign(const std::wstring& path) {
        std::wstring p = lower(path);
        for (auto& v : members)
            for (size_t k = 0; k < v.size(); ++k)
                if (lower(v[k]) == p) { v.erase(v.begin() + k); return; }
    }
    bool changePath(const std::wstring& oldP, const std::wstring& newP) {
        int g = groupOf(oldP); if (g < 0) return false;
        unassign(oldP); assign(newP, g); return true;
    }
    std::string serialize() const {
        std::string o = "# TabGroups groups file v1\r\n";
        for (size_t i = 0; i < names.size(); ++i) {
            o += "[group]\r\nname=" + wideToUtf8(names[i]) + "\r\n";
            for (auto& m : members[i]) o += "member=" + wideToUtf8(m) + "\r\n";
        }
        return o;
    }
    bool parse(const std::string& utf8) {
        names.clear(); members.clear();
        size_t pos = 0; int cur = -1;
        while (pos <= utf8.size()) {
            size_t e = utf8.find_first_of("\r\n", pos);
            std::string line = utf8.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
            pos = (e == std::string::npos) ? utf8.size() + 1 : e + 1;
            if (line == "[group]") { names.push_back(L""); members.emplace_back(); cur = (int)names.size() - 1; }
            else if (cur >= 0 && line.rfind("name=", 0) == 0) names[cur] = utf8ToWide(line.substr(5));
            else if (cur >= 0 && line.rfind("member=", 0) == 0) {
                std::wstring m = utf8ToWide(line.substr(7));
                if (!m.empty() && groupOf(m) < 0) members[cur].push_back(m);
            }
        }
        // drop nameless groups
        for (int i = (int)names.size() - 1; i >= 0; --i)
            if (names[i].empty()) { names.erase(names.begin() + i); members.erase(members.begin() + i); }
        return true;
    }
};

} // namespace tg
