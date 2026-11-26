# TabGroups for Notepad++ (beta)

Organize your open Notepad++ tabs into named groups (for example Work, Logs, Personal), save the groups, and reopen them later.

**Status: beta. Not yet tested inside a running Notepad++.** Try it with sample files and no unsaved work. Please report exactly what happened.

## Features

- Dockable panel listing groups and their tabs.
- Create, rename and delete groups. Deleting a group is designed not to close or change a file.
- Assign the current tab, or any open tab, to a group.
- Open all files in a group; files that are not open show "(not open)".
- Close all tabs in a group.
- Close all tabs except this group.
- Groups are saved automatically to TabGroups.ini in the Notepad++ plugins config folder.

## Install

1. Check your architecture in Notepad++ under ? > Debug Info (x64, x86 or arm64).
2. Download the matching zip from Releases and close Notepad++.
3. Create `<Notepad++ folder>\plugins\TabGroups\` and copy `TabGroups.dll` into it. The folder and DLL name must match.
4. Start Notepad++. The menu Plugins > TabGroups appears. Windows may warn about an unsigned DLL.

## Safety

- The plugin is designed not to modify or delete your files, and it makes no network connections.
- Closing tabs uses Notepad++'s own close command, so you get the normal "save changes?" prompt. Cancel is intended to stop the remaining closes.
- By design, starting Notepad++ only reads the saved groups and draws the panel, without opening, closing or saving any tab.

## Known limits

- Unsaved ("new 1") tabs keep their group only until the tab closes or Notepad++ exits.
- No tab coloring and no drag and drop yet.
- English only. Dark mode is not styled yet.
- Files moved in Explorer keep their old path in the group and show "(not open)".

## License

GPL-3.0-or-later. Includes code from the Notepad++ plugin template and headers (GPL) and Scintilla headers. Full texts are in `LICENSE-GPL-3.0.txt`, `THIRD-PARTY-NOTICES.txt` and `licenses/` in every download.

---

בחסות [TextCompareLab](https://textcomparelab.com)
