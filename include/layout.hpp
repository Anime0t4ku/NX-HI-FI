#pragma once
#include <algorithm>
#include <cmath>
namespace nxh {
// Docked uses a larger workspace at 1.25 physical pixels per layout unit.
// Horizontal design coordinates keep the same column proportions; text and
// row heights stay fixed in layout units, so the extra space holds more content.
struct Layout {
    bool docked = false;
    int width() const {
        return docked ? 1536 : 1280;
    }
    int height() const {
        return docked ? 864 : 720;
    }
    int outputWidth() const {
        return docked ? 1920 : 1280;
    }
    int outputHeight() const {
        return docked ? 1080 : 720;
    }
    int extraHeight() const {
        return height() - 720;
    }
    int x(int designX) const {
        return int(std::lround(designX * width() / 1280.0));
    }
    int designX(int pixelX) const {
        return int(std::lround(pixelX * 1280.0 / width()));
    }
    int designWidth(int pixels) const {
        return int(std::ceil(pixels * 1280.0 / width()));
    }
    int bottom(int y) const {
        return y + extraHeight();
    }
    int panelHeight() const {
        return 445 + extraHeight();
    }
    int rows() const {
        return 8 + extraHeight() / 48;
    }
    int rowTop() const {
        return 182;
    }
    int rowHeight() const {
        return 48;
    }
    int rowsBottom() const {
        return rowTop() + rows() * rowHeight();
    }
    int optionRows() const {
        return rows();
    }
    int optionFirst(int selected) const {
        return std::max(0, selected - optionRows() + 1);
    }
    int compactArtDesignSize() const {
        return docked ? 212 : 180;
    }
    int compactArtX() const {
        return 1064 - compactArtDesignSize() / 2;
    }
    int artSize(bool full) const {
        return x(full ? 440 : compactArtDesignSize());
    }
    int compactTitleY() const {
        return 178 + artSize(false) + 15;
    }
    int fullY(int y) const {
        // Spread fullscreen content over the additional vertical space.
        return y + int(std::lround(std::clamp((y - 100) / 410.0, 0.0, 1.0) * extraHeight()));
    }
    void keepVisible(int selected, int &scroll, int count) const {
        if (count <= 0) {
            scroll = 0;
            return;
        }
        selected = std::clamp(selected, 0, count - 1);
        scroll = std::clamp(scroll, std::max(0, selected - rows() + 1), selected);
        scroll = std::min(scroll, std::max(0, count - rows()));
    }
};
} // namespace nxh
