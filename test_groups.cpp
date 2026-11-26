#include "src/groups.h"
#include <cassert>
#include <iostream>
using namespace tg;
int main() {
    Store s;
    assert(s.addGroup(L"Work"));
    assert(s.addGroup(L"Logs"));
    assert(!s.addGroup(L"work"));            // duplicate, case-insensitive
    assert(!s.addGroup(L""));
    assert(s.assign(L"C:\\proj\\a.txt", 0));
    assert(s.assign(L"C:\\proj\\b.txt", 0));
    assert(s.assign(L"D:\\log.txt", 1));
    assert(s.groupOf(L"c:\\PROJ\\A.TXT") == 0);   // case-insensitive path match
    assert(s.assign(L"C:\\proj\\a.txt", 1));      // move between groups
    assert(s.groupOf(L"C:\\proj\\a.txt") == 1);
    assert(s.members[0].size() == 1 && s.members[1].size() == 2);
    assert(s.renameGroup(1, L"Personal"));
    assert(!s.renameGroup(1, L"Work"));           // name taken
    assert(s.changePath(L"D:\\log.txt", L"D:\\renamed.txt"));
    assert(s.groupOf(L"D:\\renamed.txt") == 1);
    assert(s.groupOf(L"D:\\log.txt") == -1);
    // round-trip through serialize/parse
    std::string ser = s.serialize();
    Store t;
    assert(t.parse(ser));
    assert(t.names.size() == 2);
    assert(t.names[0] == L"Work" && t.names[1] == L"Personal");
    assert(t.groupOf(L"c:\\proj\\a.txt") == 1);
    assert(t.groupOf(L"D:\\renamed.txt") == 1);
    assert(t.members[0].size() == 1);
    // utf-8 names survive
    Store u; assert(u.addGroup(L"\u05E2\u05D1\u05D5\u05D3\u05D4")); // Hebrew "work"
    Store v; assert(v.parse(u.serialize()));
    assert(v.names.size() == 1 && v.names[0] == L"\u05E2\u05D1\u05D5\u05D3\u05D4");
    // remove group
    s.removeGroup(0);
    assert(s.names.size() == 1 && s.names[0] == L"Personal");
    // parse garbage does not crash
    Store g2; g2.parse("[group]\nmember=C:\\x.txt\nname=\n[group]\nname=Ok\nmember=\n");
    assert(g2.names.size() == 1 && g2.names[0] == L"Ok" && g2.members[0].empty());
    std::cout << "all group-store tests passed\n";
    return 0;
}
