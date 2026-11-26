// TabGroups for Notepad++ - DLL entry and Notepad++ plugin exports.
// Based on npp-plugins/plugintemplate (Don HO, GPL v2 or later). Copyright (C) 2026 TabGroups plugin authors. GPL v3 or later.
#include "PluginDefinition.h"

extern FuncItem funcItem[nbFunc];
extern NppData nppData;

BOOL APIENTRY DllMain(HANDLE hModule, DWORD reasonForCall, LPVOID /*lpReserved*/) {
    try {
        switch (reasonForCall) {
            case DLL_PROCESS_ATTACH: pluginInit(hModule); break;
            case DLL_PROCESS_DETACH: pluginCleanUp(); break;
            default: break;
        }
    } catch (...) { return FALSE; }
    return TRUE;
}

extern "C" __declspec(dllexport) void setInfo(NppData notpadPlusData) {
    nppData = notpadPlusData;
    commandMenuInit();
}

extern "C" __declspec(dllexport) const TCHAR * getName() { return NPP_PLUGIN_NAME; }

extern "C" __declspec(dllexport) FuncItem * getFuncsArray(int *nbF) {
    *nbF = nbFunc;
    return funcItem;
}

extern "C" __declspec(dllexport) void beNotified(SCNotification *notifyCode) {
    try {
        switch (notifyCode->nmhdr.code) {
            case NPPN_SHUTDOWN: commandMenuCleanUp(); break;
            default: break;
        }
        onNotification(notifyCode);
    } catch (...) {}
}

extern "C" __declspec(dllexport) LRESULT messageProc(UINT /*Message*/, WPARAM /*wParam*/, LPARAM /*lParam*/) { return TRUE; }

#ifdef UNICODE
extern "C" __declspec(dllexport) BOOL isUnicode() { return TRUE; }
#endif
