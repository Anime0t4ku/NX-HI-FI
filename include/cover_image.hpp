#pragma once
#include "cover_codec.h"
#include <SDL.h>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
namespace nxh {
inline bool coverDimensions(const std::vector<unsigned char> &bytes, uint32_t &width,
                            uint32_t &height) {
    width = height = 0;
    if (bytes.size() < 24 || bytes.size() > 16u * 1024u * 1024u)
        return false;
    if (!std::memcmp(bytes.data(), "\x89PNG\r\n\x1a\n", 8)) {
        for (int i = 0; i < 4; ++i) {
            width = (width << 8) | bytes[16 + i];
            height = (height << 8) | bytes[20 + i];
        }
    } else if (bytes[0] == 255 && bytes[1] == 216) {
        size_t p = 2;
        while (p + 1 < bytes.size()) {
            if (bytes[p++] != 255)
                continue;
            while (p < bytes.size() && bytes[p] == 255)
                ++p;
            if (p >= bytes.size())
                return false;
            unsigned marker = bytes[p++];
            if (marker == 217 || marker == 218)
                break;
            if (marker == 1 || (marker >= 208 && marker <= 215))
                continue;
            if (p + 2 > bytes.size())
                return false;
            size_t length = (size_t(bytes[p]) << 8) | bytes[p + 1];
            if (length < 2 || length > bytes.size() - p)
                return false;
            if ((marker >= 192 && marker <= 195) || (marker >= 197 && marker <= 199) ||
                (marker >= 201 && marker <= 203) || (marker >= 205 && marker <= 207)) {
                if (length < 8)
                    return false;
                height = (uint32_t(bytes[p + 3]) << 8) | bytes[p + 4];
                width = (uint32_t(bytes[p + 5]) << 8) | bytes[p + 6];
                break;
            }
            p += length;
        }
    }
    return width > 0 && height > 0 && width <= 4096 && height <= 4096;
}
inline SDL_Surface *boundedCoverSurface(const std::vector<unsigned char> &bytes) {
    uint32_t width, height;
    if (!coverDimensions(bytes, width, height))
        return nullptr;
    SDL_Surface *surface = nullptr;
    nx_cover_rgb rgb{};
    int ok = (bytes[0] == 255 && bytes[1] == 216)
                 ? nx_cover_decode_jpeg(bytes.data(), bytes.size(), &rgb)
                 : nx_cover_decode_png(bytes.data(), bytes.size(), &rgb);
    if (!ok)
        return nullptr;
    surface = SDL_CreateRGBSurfaceWithFormat(0, rgb.width, rgb.height, rgb.channels * 8,
                                             rgb.channels == 4 ? SDL_PIXELFORMAT_RGBA32
                                                               : SDL_PIXELFORMAT_RGB24);
    if (surface)
        for (int y = 0; y < rgb.height; ++y)
            std::memcpy(static_cast<unsigned char *>(surface->pixels) + size_t(y) * surface->pitch,
                        rgb.pixels + size_t(y) * rgb.pitch, size_t(rgb.width) * rgb.channels);
    std::free(rgb.pixels);
    return surface;
}
inline std::vector<unsigned char> remoteCoverJpeg(SDL_Surface *source) {
    if (!source || source->w < 1 || source->h < 1)
        return {};
    double scale = std::min(1.0, 512.0 / std::max(source->w, source->h));
    auto target = SDL_CreateRGBSurfaceWithFormat(0, std::max(1, int(source->w * scale)),
                                                 std::max(1, int(source->h * scale)), 24,
                                                 SDL_PIXELFORMAT_RGB24);
    if (!target)
        return {};
    if (SDL_BlitScaled(source, nullptr, target, nullptr) < 0) {
        SDL_FreeSurface(target);
        return {};
    }
    unsigned char *jpeg = nullptr;
    size_t size = 0;
    int ok = nx_cover_encode_jpeg(static_cast<unsigned char *>(target->pixels), target->w,
                                  target->h, target->pitch, &jpeg, &size);
    SDL_FreeSurface(target);
    std::vector<unsigned char> result;
    if (ok) {
        try {
            result.assign(jpeg, jpeg + size);
        } catch (...) {
            std::free(jpeg);
            return {};
        }
    }
    std::free(jpeg);
    return result;
}
} // namespace nxh
