#include "layout.hpp"
#include <cassert>
#include <iostream>
using namespace nxh;
int main() {
    Layout handheld{false}, docked{true};
    assert(handheld.outputWidth() == 1280 && handheld.outputHeight() == 720);
    assert(docked.outputWidth() == 1920 && docked.outputHeight() == 1080);
    assert(handheld.rows() == 10 && docked.rows() == 13);
    assert(docked.x(828) > handheld.x(828));
    assert(docked.x(328) > handheld.x(328));
    for (const Layout &l : {handheld, docked}) {
        assert(l.width() * l.outputHeight() == l.height() * l.outputWidth());
        for (int x = 0; x <= 1280; ++x)
            assert(l.designX(l.x(x)) == x);
        assert(l.rowsBottom() < 155 + l.panelHeight());
        assert(l.footerY()+l.footerHeight()==l.height());
        assert(155+l.panelHeight()<=l.footerY()-16);
        assert(l.fullY(510) < l.bottom(605));
        assert(100 + l.artSize(true) < l.bottom(605));
        assert(178 + l.artSize(false) < l.compactTitleY());
        assert(l.optionFirst(15) + l.optionRows() == 16);
        assert(170 + l.optionRows() * 48 < 100 + 510 + l.extraHeight());
        for (int count = 0; count <= 35; ++count)
            for (int selected = 0; selected < count; ++selected)
                for (int initial = 0; initial < 35; ++initial) {
                    int scroll = initial;
                    l.keepVisible(selected, scroll, count);
                    assert(scroll >= 0 && scroll <= selected);
                    assert(selected < scroll + l.rows());
                    assert(scroll <= std::max(0, count - l.rows()));
                }
    }
    // Docked row 11 remains selected and visible after undocking and redocking.
    int selected = 10, scroll = 0;
    handheld.keepVisible(selected, scroll, 30);
    assert(scroll == 1 && selected == 10);
    docked.keepVisible(selected, scroll, 30);
    assert(scroll <= selected && selected < scroll + docked.rows());
    std::cout << "Handheld/docked geometry, pointer mapping and selection reflow passed.\n";
}
