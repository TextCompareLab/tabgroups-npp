#!/bin/bash
set -x
for a in x86_64:x64 x86:x86 aarch64:arm64; do
  t=${a%%:*}; n=${a##*:}
  python3 -m ziglang c++ -std=c++17 -target $t-windows-gnu -shared -Os -w -DUNICODE -D_UNICODE -DNOMINMAX \
    -o out/$n/TabGroups.dll src/PluginMain.cpp src/PluginDefinition.cpp src/DockingFeature/StaticDialog.cpp src/tabgroups.rc \
    -lshell32 -luser32 -lcomdlg32 -lgdi32 -lcomctl32 -lshlwapi -Wl,--strip-all || echo "BUILD FAILED $n"
  rm -f out/$n/*.lib
done
echo ALLDONE
