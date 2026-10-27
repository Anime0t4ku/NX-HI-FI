#include "cover_image.hpp"
#include "ui.hpp"
#include <png.h>
#include <jpeglib.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <vector>
using Bytes = std::vector<unsigned char>;
static Bytes jpegFixture(int width, int height, int channels = 3) {
    jpeg_compress_struct codec{};
    jpeg_error_mgr error{};
    codec.err = jpeg_std_error(&error);
    jpeg_create_compress(&codec);
    unsigned char *data = nullptr;
    unsigned long size = 0;
    jpeg_mem_dest(&codec, &data, &size);
    codec.image_width = width;
    codec.image_height = height;
    codec.input_components = channels;
    codec.in_color_space = channels == 4 ? JCS_CMYK : channels == 1 ? JCS_GRAYSCALE : JCS_RGB;
    jpeg_set_defaults(&codec);
    jpeg_set_quality(&codec, 95, TRUE);
    jpeg_start_compress(&codec, TRUE);
    Bytes row(width * channels);
    uint32_t random = 0x12345678;
    while (codec.next_scanline < codec.image_height) {
        for (auto &v : row) {
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            v = random;
        }
        JSAMPROW pointer = row.data();
        jpeg_write_scanlines(&codec, &pointer, 1);
    }
    jpeg_finish_compress(&codec);
    jpeg_destroy_compress(&codec);
    Bytes out(data, data + size);
    free(data);
    return out;
}
static void pngWrite(png_structp png, png_bytep bytes, png_size_t size) {
    auto &out = *static_cast<Bytes *>(png_get_io_ptr(png));
    out.insert(out.end(), bytes, bytes + size);
}
static void pngFlush(png_structp) {}
static Bytes pngFixture(int width, int height, bool interlaced) {
    Bytes out;
    auto png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    auto info = png_create_info_struct(png);
    assert(png && info);
    assert(!setjmp(png_jmpbuf(png)));
    png_set_write_fn(png, &out, pngWrite, pngFlush);
    png_set_IHDR(png, info, width, height, 8, PNG_COLOR_TYPE_RGBA,
                 interlaced ? PNG_INTERLACE_ADAM7 : PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    Bytes pixels(size_t(width) * height * 4);
    std::vector<png_bytep> rows(height);
    for (int y = 0; y < height; ++y) {
        rows[y] = pixels.data() + size_t(y) * width * 4;
        for (int x = 0; x < width; ++x) {
            auto p = rows[y] + x * 4;
            p[0] = x % 256;
            p[1] = y % 256;
            p[2] = (x + y) % 256;
            p[3] = (x * 3 + y) % 256;
        }
    }
    png_write_image(png, rows.data());
    png_write_end(png, info);
    png_destroy_write_struct(&png, &info);
    return out;
}
static void checkPreview(const Bytes &bytes) {
    auto *surface = nxh::boundedCoverSurface(bytes);
    assert(surface && surface->w <= 1024 && surface->h <= 1024);
    auto exported = nxh::remoteCoverJpeg(surface);
    assert(!exported.empty() && exported.size() < 1024 * 1024);
    nx_cover_rgb remote{};
    assert(nx_cover_decode_jpeg(exported.data(), exported.size(), &remote));
    assert(remote.width <= 512 && remote.height <= 512);
    free(remote.pixels);
    SDL_FreeSurface(surface);
}
int main() {
    assert(SDL_Init(0) == 0);
    auto jpeg = jpegFixture(3000, 3000);
    assert(jpeg.size() > 10 * 1024 * 1024 && jpeg.size() < 16 * 1024 * 1024);
    checkPreview(jpeg);
    checkPreview(jpegFixture(127, 95, 1));
    checkPreview(jpegFixture(127, 95, 4));
    auto png = pngFixture(3001, 2503, false);
    checkPreview(png);
    for (bool interlaced : {false, true}) {
        auto encoded = pngFixture(63, 45, interlaced);
        nx_cover_rgb rgb{};
        assert(nx_cover_decode_png(encoded.data(), encoded.size(), &rgb));
        assert(rgb.channels == 4 && rgb.width == 63 && rgb.height == 45);
        for (int y = 0; y < 45; ++y)
            for (int x = 0; x < 63; ++x) {
                auto p = rgb.pixels + size_t(y) * rgb.pitch + x * 4;
                assert(p[0] == x % 256 && p[1] == y % 256 && p[2] == (x + y) % 256 &&
                       p[3] == (x * 3 + y) % 256);
            }
        free(rgb.pixels);
    }
    auto bigInterlaced = pngFixture(2049, 1025, true);
    nx_cover_rgb rgb{};
    assert(nx_cover_decode_png(bigInterlaced.data(), bigInterlaced.size(), &rgb));
    for (int y = 0; y < rgb.height; ++y)
        for (int x = 0; x < rgb.width; ++x) {
            auto p = rgb.pixels + size_t(y) * rgb.pitch + x * 4;
            assert(p[0] == (x * 4) % 256 && p[1] == (y * 4) % 256 && p[2] == (x * 4 + y * 4) % 256);
        }
    free(rgb.pixels);
    Bytes brokenJpeg{255, 216, 255, 192, 0, 2};
    brokenJpeg.resize(30);
    assert(!nxh::boundedCoverSurface(brokenJpeg));
    for (auto *bytes : {&jpeg, &png}) {
        Bytes truncated(bytes->begin(), bytes->begin() + 32);
        assert(!nxh::boundedCoverSurface(truncated));
    }
    unsigned char *output = nullptr;
    size_t size = 0;
    assert(!nx_cover_encode_jpeg(nullptr, 512, 512, 1536, &output, &size) && !output && !size);
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    assert(SDL_InitSubSystem(SDL_INIT_VIDEO) == 0);
    auto *window = SDL_CreateWindow("cover-test", 0, 0, 1280, 720, SDL_WINDOW_HIDDEN);
    auto *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    assert(window && renderer);
    {
        nxh::UI ui(renderer, nullptr, 0);
        for (auto *bytes : {&jpeg, &png}) {
            Bytes exported;
            auto *texture = ui.cover(*bytes, &exported);
            assert(texture && !exported.empty());
            int width = 0, height = 0;
            assert(SDL_QueryTexture(texture, nullptr, nullptr, &width, &height) == 0);
            assert(width <= 1024 && height <= 1024);
            SDL_DestroyTexture(texture);
        }
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::cout << "Large JPEG (>10 MB), bounded PNG/JPEG decode, 512px export, alpha/Adam7 pixels "
                 "and malformed-artwork recovery passed.\n";
}
