#pragma once
#include "player_icons.hpp"
#include <cstring>
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
    double rasterScale_ = 1.0;
    void fillRounded(SDL_Rect a, Color color, int radius) {
        SDL_SetRenderDrawColor(r_, color.r,color.g,color.b,color.a);
        radius=std::min({radius,a.w/2,a.h/2});
        if(radius<=0){SDL_RenderFillRect(r_,&a);return;}
        // Large opaque areas use SDL's fast rectangle path. Only the tiny
        // corner arcs need triangle rasterisation (important for CPU rendering at 4K).
        // Small internal overlaps avoid fractional-scale seams between fast spans and arcs.
        const float overlap=.8f;
        SDL_FRect spans[]={{a.x+radius-overlap,float(a.y),a.w-2*radius+2*overlap,float(a.h)},
                          {float(a.x),a.y+radius-overlap,radius+overlap,a.h-2*radius+2*overlap},
                          {a.x+a.w-radius-overlap,a.y+radius-overlap,radius+overlap,a.h-2*radius+2*overlap}};
        SDL_RenderFillRectsF(r_,spans,3);
        SDL_Color c{color.r,color.g,color.b,color.a};
        for(int corner=0;corner<4;corner++){
            SDL_Vertex vertices[10]{};int indices[24]{};
            float cx=corner<2?a.x+a.w-radius:a.x+radius;
            float cy=corner==0||corner==3?a.y+radius:a.y+a.h-radius;
            vertices[0]={{cx,cy},c,{0,0}};
            for(int step=0;step<=8;step++){
                double angle=(-.5+corner*.5+step/16.)*3.141592653589793;
                vertices[step+1]={{cx+float(std::cos(angle)*radius),cy+float(std::sin(angle)*radius)},c,{0,0}};
            }
            for(int n=0;n<8;n++){indices[n*3]=0;indices[n*3+1]=n+1;indices[n*3+2]=n+2;}
            SDL_RenderGeometry(r_,nullptr,vertices,10,indices,24);
        }
    }

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
        return rasterScale_;
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
    struct BadgeLabel{SDL_Texture *texture;int width,height,left,inkWidth;};
    std::unordered_map<std::string,BadgeLabel> badgeLabels_;
    struct Icon{SDL_Texture *texture;std::vector<std::array<int,3>> runs;};
    std::unordered_map<std::string,Icon> icons_;
    std::unordered_map<std::string, int> measuredWidths_;
    std::unordered_map<std::string, std::string> fittedText_;
    struct Marquee {
        std::string value;
        Uint64 started = 0;
    };
    std::unordered_map<std::string, Marquee> marquees_;

  public:
    // Save/restore the renderer clip, including when a row creates new text textures.
    class Clip {
        SDL_Renderer *renderer_;
        SDL_Rect previous_{};
        bool enabled_;
      public:
        Clip(UI &ui, int x, int y, int w, int h) : renderer_(ui.r_),
            enabled_(SDL_RenderIsClipEnabled(renderer_)) {
            SDL_RenderGetClipRect(renderer_, &previous_);
            SDL_Rect clip{ui.renderX(x), y, ui.renderX(x+w)-ui.renderX(x), h};
            if (enabled_) SDL_IntersectRect(&clip, &previous_, &clip);
            SDL_RenderSetClipRect(renderer_, &clip);
        }
        Clip(const Clip &) = delete;
        Clip &operator=(const Clip &) = delete;
        ~Clip() { SDL_RenderSetClipRect(renderer_, enabled_ ? &previous_ : nullptr); }
    };
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
    void refreshRasterScale() {
        int w=0,h=0;SDL_GetRendererOutputSize(r_,&w,&h);
        double next=std::max(.5,std::min(double(w)/layout_.width(),double(h)/layout_.height()));
        if(std::abs(next-rasterScale_)<.01)return;
        clear();for(auto &f:fonts_)TTF_CloseFont(f.second);fonts_.clear();rasterScale_=next;
    }
    bool setDocked(bool docked) {
        if (layout_.docked == docked)
            return false;
        layout_.docked = docked;
        refreshRasterScale();
        SDL_RenderSetLogicalSize(r_, layout_.width(), layout_.height());
        clear();
        resetMarquees();
        return true;
    }
    int pointerX(int x) const {
        return layout_.designX(x);
    }
    void clear() {
        for(auto &entry:badgeLabels_) SDL_DestroyTexture(entry.second.texture);
        badgeLabels_.clear();
        for(auto &entry:icons_) SDL_DestroyTexture(entry.second.texture);
        icons_.clear();
        measuredWidths_.clear();fittedText_.clear();
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
        bool panel = w>35 && h>35 &&
            ((color.r==PANEL.r && color.g==PANEL.g && color.b==PANEL.b) ||
             (color.r==FOCUS.r && color.g==FOCUS.g && color.b==FOCUS.b));
        if(panel) {
            // Flat rounded panels avoid full-panel alpha blends on the CPU renderer.
            fillRounded(a,color,12);
        } else SDL_RenderFillRect(r_, &a);
        if (recording_)
            record({0, a.x, a.y, a.w, a.h, color.r, color.g, color.b, color.a});
    }
    void roundedRect(int x,int y,int w,int h,Color color,int radius=12){
        SDL_Rect box{renderX(x),y,renderX(x+w)-renderX(x),h};fillRounded(box,color,radius);
        if(recording_)record({0,box.x,box.y,box.w,box.h,color.r,color.g,color.b,color.a});
    }
    int textWidth(const std::string &value, int size) {
        const auto key=std::to_string(size)+":"+(nativeCoordinates_?"native:":"design:")+value;
        auto cached=measuredWidths_.find(key);
        if(cached!=measuredWidths_.end())return cached->second;
        int width = 0;
        auto *f = font(size);
        if (f)
            TTF_SizeUTF8(f, value.c_str(), &width, nullptr);
        width = fontUnits(width);
        width=nativeCoordinates_ ? width : layout_.designWidth(width);
        if(measuredWidths_.size()>2048)measuredWidths_.clear();
        measuredWidths_[key]=width;
        return width;
    }
    void text(int x, int y, std::string value, int size = 22, Color color = TEXT,
              int maxWidth = 0) {
        x = renderX(x);
        maxWidth = int(std::lround(renderX(maxWidth) * fontScale()));
        auto *f = font(size);
        if (!f)
            return;
        if (maxWidth) {
            const auto fitKey=std::to_string(size)+":"+std::to_string(maxWidth)+":"+value;
            auto fit=fittedText_.find(fitKey);
            if(fit!=fittedText_.end())value=fit->second;
            else {
            const auto original=value;
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
            if(fittedText_.size()>2048)fittedText_.clear();
            fittedText_[fitKey]=value;
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
        SDL_RenderSetClipRect(r_, nullptr);
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
    void rightText(int right,int y,const std::string &value,int size,Color color){
        // Measure and place in logical renderer coordinates to avoid horizontal
        // design-coordinate rounding or unused space in a fixed-width label box.
        int edge=renderX(right);NativeCoordinates coordinates(nativeCoordinates_);
        text(edge-textWidth(value,size),y,value,size,color);
    }
    void centeredText(int x,int y,int w,int h,const std::string &value,int size,Color color){
        auto *face=font(size);if(!face)return;
        int tw=std::min(w,textWidth(value,size)),th=fontUnits(TTF_FontHeight(face));
        text(x+(w-tw)/2,y+(h-th)/2,value,size,color,w);
    }
    void centeredMarquee(int x,int y,int w,const std::string &value,int size,Color color,const std::string &key){
        if(textWidth(value,size)<=w)centeredText(x,y,w,size+18,value,size,color);
        else marquee(x,y,value,size,color,w,key);
    }
    int padHint(int x,int y,const std::string &button,const std::string &label){
        int id=button=="Dpad"?24:button=="+"?25:button=="-"?26:
               (button=="A"||button=="B"||button=="X"||button=="Y"||button=="R3")?27:28;
        transportIcon(x+12,y+12,24,id,false,TEXT,false);
        if(id==27||id==28)centeredText(x,y+2,24,20,button=="R3"?"R":button,id==28?8:12,TEXT);
        text(x+25,y+2,label,13,MUTED);
        return x+25+textWidth(label,13)+18;
    }
    void badge(int x, int y, int w, int h, const std::string &label, int size) {
        rect(x, y, w, h, PANEL);
        rect(x, y, w, 1, LINE);
        rect(x, y + h - 1, w, 1, LINE);
        rect(x, y, 1, h, LINE);
        rect(x + w - 1, y, 1, h, LINE);
        auto *face=font(size);if(!face)return;
        auto key=std::to_string(size)+":"+std::to_string(TEXT.r)+":"+std::to_string(TEXT.g)+":"+std::to_string(TEXT.b)+":"+label;
        auto found=badgeLabels_.find(key);
        if(found==badgeLabels_.end()){
            auto *surface=TTF_RenderUTF8_Blended(face,label.c_str(),{TEXT.r,TEXT.g,TEXT.b,TEXT.a});if(!surface)return;
            int left=surface->w,right=-1;
            for(int yy=0;yy<surface->h;yy++)for(int xx=0;xx<surface->w;xx++){
                Uint32 pixel=0;std::memcpy(&pixel,(Uint8*)surface->pixels+yy*surface->pitch+xx*surface->format->BytesPerPixel,surface->format->BytesPerPixel);
                Uint8 red,green,blue,alpha;SDL_GetRGBA(pixel,surface->format,&red,&green,&blue,&alpha);
                if(alpha){left=std::min(left,xx);right=std::max(right,xx);}
            }
            if(right<left){SDL_FreeSurface(surface);return;}
            BadgeLabel item{SDL_CreateTextureFromSurface(r_,surface),surface->w,surface->h,left,right-left+1};SDL_FreeSurface(surface);if(!item.texture)return;
            found=badgeLabels_.emplace(key,item).first;
        }
        // Centre the visible glyphs inside the actual rendered box, not font advance
        // width in design units. Font side bearings and conversion rounding no longer bias it.
        auto &item=found->second;int bx=renderX(x),bw=renderX(x+w)-bx;
        // Fonts are already rasterised at output scale. Keep glyph pixels 1:1,
        // instead of enlarging a ceil-rounded logical width on the 1080p layout.
        const float scale=float(fontScale());
        const float tw=item.inkWidth/scale,th=item.height/scale;
        SDL_Rect source{item.left,0,item.inkWidth,item.height};
        SDL_FRect dest{std::round((bx+(bw-tw)*.5f)*scale)/scale,
                       std::round((y+(h-th)*.5f)*scale)/scale,tw,th};
        SDL_RenderCopyF(r_,item.texture,&source,&dest);
        if(recording_)record({1,int(std::lround(dest.x))-fontUnits(item.left),int(dest.y),label,size,TEXT.r,TEXT.g,TEXT.b,TEXT.a,0,nullptr});
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
    // Cached antialiased vector icons at the actual framebuffer scale.
    void transportIcon(int cx,int cy,int size,int icon,bool playing,Color color,bool one){
        auto key=std::to_string(icon)+":"+std::to_string(size)+":"+std::to_string(int(fontScale()*100))+":"+std::to_string(color.r)+":"+std::to_string(color.g)+":"+std::to_string(color.b)+":"+std::to_string(playing)+":"+std::to_string(one);
        auto found=icons_.find(key);
        if(found==icons_.end()){
            auto shape=playerIconShape(icon,playing,one);int pixels=std::max(16,int(std::ceil(size*fontScale())));
            auto *surface=SDL_CreateRGBSurfaceWithFormat(0,pixels,pixels,32,SDL_PIXELFORMAT_RGBA32);if(!surface)return;
            for(int y=0;y<pixels;y++)for(int x=0;x<pixels;x++){
                int coverage=0;for(int sy=0;sy<4;sy++)for(int sx=0;sx<4;sx++)coverage+=shape.hit((x+(sx+.5f)/4)/pixels,(y+(sy+.5f)/4)/pixels);
                auto *pixel=(Uint32*)((Uint8*)surface->pixels+y*surface->pitch)+x;*pixel=SDL_MapRGBA(surface->format,color.r,color.g,color.b,coverage*255/16);
            }
            Icon item{SDL_CreateTextureFromSurface(r_,surface),{}};SDL_FreeSurface(surface);if(!item.texture)return;
            SDL_SetTextureBlendMode(item.texture,SDL_BLENDMODE_BLEND);
            // The NX Screen Control protocol represents icons with short scanline runs.
            for(int y=0;y<size;y++){int first=-1;for(int x=0;x<=size;x++){bool hit=x<size&&shape.hit((x+.5f)/size,(y+.5f)/size);if(hit&&first<0)first=x;if(!hit&&first>=0){item.runs.push_back({first,y,x-first});first=-1;}}}
            found=icons_.emplace(key,std::move(item)).first;
        }
        SDL_Rect dst{renderX(cx)-size/2,cy-size/2,size,size};SDL_RenderCopy(r_,found->second.texture,nullptr,&dst);
        if(recording_)for(auto &run:found->second.runs)record({0,dst.x+run[0],dst.y+run[1],run[2],1,color.r,color.g,color.b,color.a});
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
