#include "settings_navigation.hpp"
#include <cassert>
#include <set>
#include <iostream>
using namespace nxh;
int main() {
    SettingsNavigation nav;
    const auto fields = settingFields();
    std::set<int> seen;
    assert(!nav.entries && nav.current().name == "Appearance");
    for (int i=0; i<int(nav.categories.size()); ++i) {
        nav.category=i;
        for (int index : nav.current().fields) {
            assert(seen.insert(index).second);
            assert(fields[index]["group"] == nav.current().name);
        }
        nav.entries=true;
        for (int row=0; row<int(nav.current().fields.size()); ++row) {
            assert(nav.fieldIndex()==nav.current().fields[row]);
            nav.move(1,2);
            assert(nav.current().selected>=nav.current().scroll);
            assert(nav.current().selected<nav.current().scroll+2);
        }
        assert(nav.current().selected==0);
    }
    assert(seen.size()==fields.size());
    nav.category=0;nav.entries=true;nav.move(2,2);
    assert(fields[nav.fieldIndex()]["key"]=="show_clock");
    nav.entries=false;nav.move(1,2);assert(nav.current().name=="Artwork");
    nav.move(-1,2);nav.entries=true;
    assert(fields[nav.fieldIndex()]["key"]=="show_clock");
    nav.entries=false;nav.category=0;nav.move(-1,2);
    assert(nav.current().name=="Remote");
    std::cout << "Settings categories: every field reachable, wrap, scrolling and remembered entries passed.\n";
}
