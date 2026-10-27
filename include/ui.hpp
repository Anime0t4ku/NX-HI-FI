#pragma once
#include "core.hpp"
#include "layout.hpp"
#include "cover_image.hpp"
#include "playback_diagnostics.hpp"
#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <unordered_map>
namespace nxh {
struct Color {
    Uint8 r, g, b, a = 255;
};
inline Color BG{32, 32, 32}, PANEL{42, 42, 42}, LINE{76, 76, 76}, TEXT{245, 245, 245},
    MUTED{183, 183, 183}, ACCENT{239, 68, 68}, PRIMARY{239, 68, 68}, FOCUS{65, 38, 38},
    FOOTER{26, 26, 26};
inline Color rgbColour(const std::string &hex) {
    auto c = colourRGB(hex);
    return {Uint8(c[0]), Uint8(c[1]), Uint8(c[2])};
}
inline double luminance(Color c) {
    auto channel = [](double v) {
        v /= 255.;
        return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4);
    };
    return .2126 * channel(c.r) + .7152 * channel(c.g) + .0722 * channel(c.b);
}
inline Color readableAccent(Color c, Color background) {
    auto contrast = [&](Color v) {
        double a = luminance(v), b = luminance(background);
        return (std::max(a, b) + .05) / (std::min(a, b) + .05);
    };
    bool dark = luminance(background) < .3;
    for (int i = 0; i < 255 && contrast(c) < 4.5; i++) {
        auto step = [&](Uint8 v) {
            return Uint8(dark ? std::min(255, int(v) + 1) : std::max(0, int(v) - 1));
        };
        c = {step(c.r), step(c.g), step(c.b)};
    }
    return c;
}
inline void setAppearance(const std::string &theme, const std::string &accent) {
    bool light = theme == "light", oled = theme == "oled";
    BG = light ? Color{255, 255, 255} : oled ? Color{0, 0, 0} : Color{32, 32, 32};
    PANEL = light ? Color{244, 244, 244} : oled ? Color{0, 0, 0} : Color{42, 42, 42};
    FOOTER = light ? Color{240, 240, 240} : oled ? Color{0, 0, 0} : Color{26, 26, 26};
    LINE = light ? Color{198, 198, 198} : Color{76, 76, 76};
    TEXT = light ? Color{25, 25, 25} : Color{245, 245, 245};
    MUTED = light ? Color{87, 87, 87} : Color{183, 183, 183};
    PRIMARY = rgbColour(accent);
    ACCENT = readableAccent(PRIMARY, BG);
    auto mix = [](int a, int b) { return Uint8((a * 4 + b) / 5); };
    FOCUS = {mix(PANEL.r, PRIMARY.r), mix(PANEL.g, PRIMARY.g), mix(PANEL.b, PRIMARY.b)};
}
class UI {
    SDL_Renderer *r_;
    Layout layout_;
    bool recording_ = false;
    nlohmann::json screenOps_ = nlohmann::json::array();
    std::unordered_map<SDL_Texture *, std::vector<unsigned char>> screenImages_;
    std::vector<unsigned char> screenCover_;
    void record(nlohmann::json op) {
        if (recording_ && screenOps_.size() < 4096)
            screenOps_.push_back(std::move(op));
    }

    bool nativeCoordinates_ = false;
    double fontScale() const {
        return layout_.docked ? 1.25 : 1.0;
    }
    int fontUnits(int pixels) const {
        return int(std::ceil(pixels / fontScale()));
    }
    int renderX(int x) const {
        return nativeCoordinates_ ? x : layout_.x(x);
    }
    struct NativeCoordinates {
        bool &flag;
        bool previous;
        explicit NativeCoordinates(bool &value) : flag(value), previous(value) {
            flag = true;
        }
        ~NativeCoordinates() {
            flag = previous;
        }
    };
    const void *fontData_;
    size_t fontSize_;
    std::unordered_map<int, TTF_Font *> fonts_;
    struct Label {
        SDL_Texture *texture;
        int w, h;
    };
    std::unordered_map<std::string, Label> labels_;
    struct Marquee {
        std::string value;
        Uint64 started = 0;
    };
    std::unordered_map<std::string, Marquee> marquees_;

  public:
    UI(SDL_Renderer *r, const void *data, size_t size) : r_(r), fontData_(data), fontSize_(size) {}
    ~UI() {
        clear();
        for (auto &v : fonts_)
            TTF_CloseFont(v.second);
    }
    void releaseCover(SDL_Texture *texture) {
        screenImages_.erase(texture);
        SDL_DestroyTexture(texture);
    }
    bool recordingScreen() const {
        return recording_;
    }
    void recordScreen(bool enabled) {
        recording_ = enabled;
    }
    nlohmann::json screenFrame() const {
        return {{"w", layout_.width()}, {"h", layout_.height()}, {"ops", screenOps_}};
    }
    const std::vector<unsigned char> &screenCover() const {
        return screenCover_;
    }
    void replayScreen(const nlohmann::json &frame, SDL_Texture *cover, int top = 78) {
        int w = frame.value("w", 0), h = frame.value("h", 0);
        if ((w != 1280 && w != 1536) || (h != 720 && h != 864) || !frame.contains("ops") ||
            !frame["ops"].is_array())
            return;
        double scale = std::min((layout_.width() - 48.) / w, (layout_.height() - top - 20.) / h);
        int ox = (layout_.width() - int(w * scale)) / 2,
            oy = top + (layout_.height() - top - 20 - int(h * scale)) / 2;
        NativeCoordinates native(nativeCoordinates_);
        for (auto &op : frame["ops"]) {
            if (!op.is_array() || op.size() < 5 || !op[0].is_number_integer())
                continue;
            int kind = op[0].get<int>(), x = ox + int(op[1].get<int>() * scale),
                y = oy + int(op[2].get<int>() * scale);
            if (kind == 0 && op.size() == 9)
                rect(x, y, std::max(1, int(op[3].get<int>() * scale)),
                     std::max(1, int(op[4].get<int>() * scale)),
                     {op[5].get<Uint8>(), op[6].get<Uint8>(), op[7].get<Uint8>(),
                      op[8].get<Uint8>()});
            if (kind == 1 && op.size() == 11 && op[3].is_string()) {
                SDL_Rect previous{};
                bool clipped = SDL_RenderIsClipEnabled(r_);
                SDL_RenderGetClipRect(r_, &previous);
                if (op[10].is_array() && op[10].size() == 4) {
                    auto c = op[10];
                    SDL_Rect clip{ox + int(c[0].get<int>() * scale),
                                  oy + int(c[1].get<int>() * scale), int(c[2].get<int>() * scale),
                                  int(c[3].get<int>() * scale)};
                    SDL_RenderSetClipRect(r_, &clip);
                }
                text(x, y, op[3].get<std::string>(), std::max(8, int(op[4].get<int>() * scale)),
                     {op[5].get<Uint8>(), op[6].get<Uint8>(), op[7].get<Uint8>(),
                      op[8].get<Uint8>()});
                SDL_RenderSetClipRect(r_, clipped ? &previous : nullptr);
            }
            if (kind == 2 && cover) {
                SDL_Rect dst{x, y, int(op[3].get<int>() * scale), int(op[4].get<int>() * scale)};
                SDL_RenderCopy(r_, cover, nullptr, &dst);
                if (recording_) {
                    auto i = screenImages_.find(cover);
                    if (i != screenImages_.end()) {
                        screenCover_ = i->second;
                        record({2, dst.x, dst.y, dst.w, dst.h});
                    }
                }
            }
        }
    }
    bool screenPoint(const nlohmann::json &frame, int designX, int y, int &targetX, int &targetY,
                     int top = 78) const {
        int w = frame.value("w", 0), h = frame.value("h", 0);
        if (w <= 0 || h <= 0)
            return false;
        double scale = std::min((layout_.width() - 48.) / w, (layout_.height() - top - 20.) / h);
        int ox = (layout_.width() - int(w * scale)) / 2,
            oy = top + (layout_.height() - top - 20 - int(h * scale)) / 2;
        targetX = int((layout_.x(designX) - ox) / scale);
        targetY = int((y - oy) / scale);
        return targetX >= 0 && targetX < w && targetY >= 0 && targetY < h;
    }
    const Layout &layout() const {
        return layout_;
    }
    bool setDocked(bool docked) {
        if (layout_.docked == docked)
            return false;
        layout_.docked = docked;
        SDL_RenderSetLogicalSize(r_, layout_.width(), layout_.height());
        clear();
        resetMarquees();
        return true;
    }
    int pointerX(int x) const {
        return layout_.designX(x);
    }
    void clear() {
        for (auto &v : labels_)
            SDL_DestroyTexture(v.second.texture);
        labels_.clear();
    }
    TTF_Font *font(int size) {
        size = int(std::lround(size * fontScale()));
        auto &f = fonts_[size];
        if (!f)
            f = TTF_OpenFontRW(SDL_RWFromConstMem(fontData_, fontSize_), 1, size);
        return f;
    }
    void rect(int x, int y, int w, int h, Color color) {
        SDL_SetRenderDrawColor(r_, color.r, color.g, color.b, color.a);
        SDL_Rect a{renderX(x), y, renderX(x + w) - renderX(x), h};
        SDL_RenderFillRect(r_, &a);
        if (recording_)
            record({0, a.x, a.y, a.w, a.h, color.r, color.g, color.b, color.a});
    }
    int textWidth(const std::string &value, int size) {
        int width = 0;
        auto *f = font(size);
        if (f)
            TTF_SizeUTF8(f, value.c_str(), &width, nullptr);
        width = fontUnits(width);
        return nativeCoordinates_ ? width : layout_.designWidth(width);
    }
    void text(int x, int y, std::string value, int size = 22, Color color = TEXT,
              int maxWidth = 0) {
        x = renderX(x);
        maxWidth = int(std::lround(renderX(maxWidth) * fontScale()));
        auto *f = font(size);
        if (!f)
            return;
        if (maxWidth) {
            int w = 0;
            TTF_SizeUTF8(f, value.c_str(), &w, nullptr);
            if (w > maxWidth) {
                do {
                    if (value.empty())
                        break;
                    size_t end = value.size() - 1;
                    while (end > 0 && (static_cast<unsigned char>(value[end]) & 0xc0) == 0x80)
                        end--;
                    value.resize(end);
                    TTF_SizeUTF8(f, (value + "...").c_str(), &w, nullptr);
                } while (w > maxWidth);
                value += "...";
            }
        }
        auto key = std::to_string(size) + ":" + std::to_string(color.r) + "," +
                   std::to_string(color.g) + "," + std::to_string(color.b) + ":" + value;
        auto it = labels_.find(key);
        if (it == labels_.end()) {
            auto *s =
                TTF_RenderUTF8_Blended(f, value.c_str(), {color.r, color.g, color.b, color.a});
            if (!s)
                return;
            Label label{SDL_CreateTextureFromSurface(r_, s), s->w, s->h};
            SDL_FreeSurface(s);
            if (!label.texture)
                return;
            it = labels_.emplace(key, label).first;
        }
        SDL_Rect dst{x, y, fontUnits(it->second.w), fontUnits(it->second.h)};
        SDL_RenderCopy(r_, it->second.texture, nullptr, &dst);
        nlohmann::json clip = nullptr;
        if (recording_ && SDL_RenderIsClipEnabled(r_)) {
            SDL_Rect c;
            SDL_RenderGetClipRect(r_, &c);
            clip = {c.x, c.y, c.w, c.h};
        }
        if (recording_)
            record({1, x, y, value, size, color.r, color.g, color.b, color.a, 0, clip});
    }
    void resetMarquees() {
        marquees_.clear();
    }
    void marquee(int x, int y, const std::string &value, int size, Color color, int width,
                 const std::string &id) {
        x = renderX(x);
        width = renderX(width);
        NativeCoordinates coordinates(nativeCoordinates_);
        auto *face = font(size);
        if (!face)
            return;
        auto &m = marquees_[id];
        if (m.value != value) {
            m.value = value;
            m.started = SDL_GetTicks64();
        }
        int w = textWidth(value, size);
        int offset = int(marqueeOffset(w, width, (SDL_GetTicks64() - m.started) / 1000.));
        SDL_Rect previous{};
        bool clipped = SDL_RenderIsClipEnabled(r_);
        SDL_RenderGetClipRect(r_, &previous);
        SDL_Rect clip{x, y, width, fontUnits(TTF_FontHeight(face)) + 2};
        if (clipped)
            SDL_IntersectRect(&clip, &previous, &clip);
        SDL_RenderSetClipRect(r_, &clip);
        text(x - offset, y, value, size, color);
        SDL_RenderSetClipRect(r_, clipped ? &previous : nullptr);
    }
    void begin() {
        screenOps_ = nlohmann::json::array();
        screenCover_.clear();
        if (recording_)
            record({0, 0, 0, layout_.width(), layout_.height(), BG.r, BG.g, BG.b, 255});
        if (labels_.size() > 600)
            clear();
        SDL_SetRenderDrawColor(r_, BG.r, BG.g, BG.b, 255);
        SDL_RenderClear(r_);
    }
    void button(int x, int y, int w, int h, const std::string &label, bool focus = false) {
        rect(x, y, w, h, focus ? FOCUS : PANEL);
        if (focus)
            rect(x, y, 4, h, ACCENT);
        text(x + 14, y + (h - 22) / 2 - 2, label, 20, focus ? ACCENT : TEXT, w - 24);
    }
    void badge(int x, int y, int w, int h, const std::string &label, int size) {
        rect(x, y, w, h, PANEL);
        rect(x, y, w, 1, LINE);
        rect(x, y + h - 1, w, 1, LINE);
        rect(x, y, 1, h, LINE);
        rect(x + w - 1, y, 1, h, LINE);
        text(x + 10, y + 5, label, size, TEXT, w - 20);
    }
    void line(int x0, int y0, int x1, int y1, int thick, Color c) {
        x0 = renderX(x0);
        x1 = renderX(x1);
        NativeCoordinates coordinates(nativeCoordinates_);
        int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
        for (;;) {
            rect(x0 - thick / 2, y0 - thick / 2, thick, thick, c);
            if (x0 == x1 && y0 == y1)
                break;
            int e = 2 * err;
            if (e >= dy) {
                err += dy;
                x0 += sx;
            }
            if (e <= dx) {
                err += dx;
                y0 += sy;
            }
        }
    }
    // Same geometric transport shapes as MiSTer Hi-Fi's framebuffer controls.
    void transportIcon(int cx, int cy, int size, int icon, bool playing, Color c, bool one) {
        cx = renderX(cx);
        NativeCoordinates coordinates(nativeCoordinates_);
        int h = size / 2, t = std::max(2, size / 12);
        if (icon == 1) {
            if (playing) {
                int w = std::max(3, size / 5), gap = std::max(3, size / 7);
                rect(cx - gap / 2 - w, cy - h, w, size, c);
                rect(cx + gap / 2, cy - h, w, size, c);
            } else
                for (int y = -h; y <= h; y++)
                    rect(cx - h / 3, cy + y, std::max(1, h - std::abs(y)), 2, c);
        } else if (icon == 0 || icon == 2) {
            int bar = std::max(2, size / 9), bx = h - bar, tri = size * 2 / 5;
            rect(icon == 2 ? cx + bx : cx - bx - bar, cy - h, bar, size, c);
            for (int y = -h; y < h; y++) {
                int w = std::max(1, tri - tri * std::abs(y) / std::max(1, h));
                rect(icon == 2 ? cx - tri / 2 : cx + tri / 2 - w, cy + y, w, 2, c);
            }
        } else if (icon == 3)
            rect(cx - h, cy - h, size, size, c);
        else if (icon == 4 || icon == 5) {
            int l = cx - h, r = cx + h, top = cy - size / 3, bot = cy + size / 3;
            if (icon == 4) {
                line(l, top, cx - size / 8, top, t, c);
                line(cx - size / 8, top, cx + size / 8, bot, t, c);
                line(cx + size / 8, bot, r - size / 8, bot, t, c);
                line(l, bot, cx - size / 8, bot, t, c);
                line(cx - size / 8, bot, cx + size / 8, top, t, c);
                line(cx + size / 8, top, r - size / 8, top, t, c);
                for (int y : {top, bot}) {
                    line(r - size / 8, y, r - size / 4, y - size / 8, t, c);
                    line(r - size / 8, y, r - size / 4, y + size / 8, t, c);
                }
            } else {
                line(l + size / 8, top, r - size / 8, top, t, c);
                line(r - size / 8, top, r - size / 4, top - size / 8, t, c);
                line(r - size / 8, top, r - size / 4, top + size / 8, t, c);
                line(r - size / 8, bot, l + size / 8, bot, t, c);
                line(l + size / 8, bot, l + size / 4, bot - size / 8, t, c);
                line(l + size / 8, bot, l + size / 4, bot + size / 8, t, c);
                line(r - size / 8, top, r, cy, t, c);
                line(r, cy, r - size / 8, bot, t, c);
                line(l + size / 8, bot, l, cy, t, c);
                line(l, cy, l + size / 8, top, t, c);
                if (one)
                    text(cx - 4, cy - 10, "1", 14, c);
            }
        } else if (icon == 6 || icon == 7) {
            // Seek uses the same triangle shape, doubled.
            for (int off : {-7, 7})
                for (int y = -h; y < h; y++) {
                    int w = std::max(1, h - std::abs(y));
                    rect(icon == 7 ? cx + off - 5 : cx + off + 5 - w, cy + y, w, 2, c);
                }
        } else {
            int u = std::max(1, size / 12);
            rect(cx - h, cy - size / 6, size / 4, size / 3, c);
            line(cx - size / 4, cy - size / 6, cx, cy - size / 3, u, c);
            line(cx, cy - size / 3, cx, cy + size / 3, u, c);
            line(cx, cy + size / 3, cx - size / 4, cy + size / 6, u, c);
            line(cx + size / 3 - size / 8, cy, cx + size / 3 + size / 8, cy, u, c);
            if (icon == 9)
                line(cx + size / 3, cy - size / 8, cx + size / 3, cy + size / 8, u, c);
        }
    }
    SDL_Texture *cover(const std::vector<unsigned char> &bytes,
                       std::vector<unsigned char> *remoteBytes = nullptr) {
        if (remoteBytes)
            remoteBytes->clear();
        if (bytes.size() < 24)
            return nullptr;
        playbackTrace("artwork.bounded-decode.begin", bytes.size());
        auto *surface = boundedCoverSurface(bytes);
        playbackTrace("artwork.bounded-decode.end", surface ? std::max(surface->w, surface->h) : 0);
        if (!surface)
            return nullptr;
        auto encoded = remoteSurface(surface);
        if (remoteBytes)
            *remoteBytes = encoded;
        playbackTrace("artwork.texture.begin");
        auto *texture = SDL_CreateTextureFromSurface(r_, surface);
        playbackTrace("artwork.texture.end", bool(texture));
        SDL_FreeSurface(surface);
        if (texture) {
            screenImages_[texture] = std::move(encoded);
        }
        return texture;
    }
    std::vector<unsigned char> remoteSurface(SDL_Surface *surface) {
        return remoteCoverJpeg(surface);
    }
    void image(SDL_Texture *t, int x, int y, int size) {
        x = renderX(x);
        size = renderX(size);
        NativeCoordinates coordinates(nativeCoordinates_);
        rect(x, y, size, size, PANEL);
        if (t) {
            int w, h;
            SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
            float scale = float(size) / std::max(w, h);
            SDL_Rect dst{x + (size - int(w * scale)) / 2, y + (size - int(h * scale)) / 2,
                         int(w * scale), int(h * scale)};
            SDL_RenderCopy(r_, t, nullptr, &dst);
            if (recording_) {
                auto i = screenImages_.find(t);
                if (i != screenImages_.end()) {
                    screenCover_ = i->second;
                    record({2, dst.x, dst.y, dst.w, dst.h});
                }
            }
        } else {
            for (int i = 0; i < 5; i++)
                rect(x + size / 4 + i * size / 10, y + size / 2 - (i % 3 + 1) * size / 20,
                     size / 18, (i % 3 + 1) * size / 10, LINE);
        }
    }
};
inline bool inside(int x, int y, int left, int top, int w, int h) {
    return x >= left && x < left + w && y >= top && y < top + h;
}
inline std::string timeLabel(double n) {
    int s = std::max(0, int(n));
    char b[32];
    std::snprintf(b, sizeof b, "%d:%02d", s / 60, s % 60);
    return b;
}
} // namespace nxh
