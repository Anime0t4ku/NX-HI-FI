#pragma once
#include "core.hpp"
#include <functional>
#include <cstring>
namespace nxh {
constexpr size_t MaxArtBytes = 16 * 1024 * 1024;
inline bool artRead(Input &in, void *dst, size_t n) {
    auto *p = static_cast<unsigned char *>(dst);
    while (n) {
        size_t got = in.read(p, n);
        if (!got)
            return false;
        p += got;
        n -= got;
    }
    return true;
}
inline uint32_t artBE(const unsigned char *p, int n = 4, bool sync = false) {
    uint32_t v = 0;
    for (int i = 0; i < n; i++)
        v = (v << (sync ? 7 : 8)) | (sync ? p[i] & 127 : p[i]);
    return v;
}
inline std::vector<unsigned char> deUnsync(const std::vector<unsigned char> &in) {
    std::vector<unsigned char> out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); i++) {
        out.push_back(in[i]);
        if (in[i] == 255 && i + 1 < in.size() && in[i + 1] == 0)
            ++i;
    }
    return out;
}
// Inspect metadata only, with bounded allocations and no full audio-file scan.
inline std::vector<unsigned char>
embeddedCover(Input &in, const std::function<bool(const std::vector<unsigned char> &)> &valid) {
    unsigned char h[10]{};
    if (!artRead(in, h, 4))
        return {};
    if (!std::memcmp(h, "fLaC", 4)) {
        size_t scanned = 4;
        while (scanned < 64 * 1024 * 1024) {
            if (!artRead(in, h, 4))
                break;
            size_t n = artBE(h + 1, 3);
            scanned += 4 + n;
            if ((h[0] & 127) == 6 && n <= MaxArtBytes + 4096) {
                std::vector<unsigned char> b(n);
                if (!artRead(in, b.data(), n))
                    break;
                size_t p = 4;
                auto skip = [&]() {
                    if (p + 4 > b.size())
                        return false;
                    size_t len = artBE(b.data() + p);
                    p += 4;
                    if (len > b.size() - p)
                        return false;
                    p += len;
                    return true;
                };
                if (skip() && skip() && p + 20 <= b.size()) {
                    p += 16;
                    size_t len = artBE(b.data() + p);
                    p += 4;
                    if (len && len <= MaxArtBytes && len <= b.size() - p) {
                        std::vector<unsigned char> image(b.begin() + p, b.begin() + p + len);
                        if (valid(image))
                            return image;
                    }
                }
            } else if (!in.seek(n, SEEK_CUR))
                break;
            if (h[0] & 128)
                break;
        }
    } else if (!std::memcmp(h, "ID3", 3)) {
        if (!artRead(in, h + 4, 6) || h[3] < 2 || h[3] > 4)
            return {};
        int version = h[3];
        unsigned flags = h[5];
        size_t n = artBE(h + 6, 4, true);
        if (n > 32 * 1024 * 1024 || (version == 2 && (flags & 64)))
            return {};
        std::vector<unsigned char> b(n);
        if (!artRead(in, b.data(), n))
            return {};
        if (version < 4 && (flags & 128))
            b = deUnsync(b);
        size_t p = 0;
        if (version >= 3 && (flags & 64)) {
            if (b.size() < 4)
                return {};
            p = artBE(b.data(), 4, version == 4) + (version == 3 ? 4 : 0);
        }
        size_t head = version == 2 ? 6 : 10;
        while (p <= b.size() && head <= b.size() - p) {
            auto *f = b.data() + p;
            size_t len = version == 2 ? artBE(f + 3, 3) : artBE(f + 4, 4, version == 4);
            if (!len || len > b.size() - p - head)
                break;
            bool pic = version == 2 ? !std::memcmp(f, "PIC", 3) : !std::memcmp(f, "APIC", 4);
            // Compressed/encrypted picture frames need unsupported transforms.
            bool unsupported = version == 3 ? (f[9] & 192) : version == 4 ? (f[9] & 12) : false;
            if (pic && !unsupported && len <= MaxArtBytes + 4096) {
                std::vector<unsigned char> v(b.begin() + p + head, b.begin() + p + head + len);
                if (version == 4 && ((flags & 128) || (f[9] & 2)))
                    v = deUnsync(v);
                // MIME, description and encoding may vary; signatures identify PNG/JPEG.
                for (size_t j = 0; j + 8 <= v.size(); j++) {
                    if ((v[j] == 255 && v[j + 1] == 216 && v[j + 2] == 255) ||
                        !std::memcmp(v.data() + j, "\x89PNG\r\n\x1a\n", 8)) {
                        std::vector<unsigned char> image(v.begin() + j, v.end());
                        if (image.size() <= MaxArtBytes && valid(image))
                            return image;
                    }
                }
            }
            p += head + len;
        }
    }
    return {};
}
} // namespace nxh
