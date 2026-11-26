// TabGroups for Notepad++. Copyright (C) 2026 TabGroups plugin authors. GPL v3 or later.
#ifndef PLUGINDEFINITION_H
#define PLUGINDEFINITION_H
#include "PluginInterface.h"

const TCHAR NPP_PLUGIN_NAME[] = TEXT("TabGroups");
const int nbFunc = 5;

void pluginInit(HANDLE hModule);
void pluginCleanUp();
void commandMenuInit();
void commandMenuCleanUp();
bool setCommand(size_t index, const TCHAR *cmdName, PFUNCPLUGINCMD pFunc, ShortcutKey *sk = nullptr, bool check0nInit = false);

void togglePanel();
void newGroupCmd();
void assignCurrentCmd();
void saveGroupsCmd();
void aboutBox();
void onNotification(SCNotification *notifyCode);

#endif
