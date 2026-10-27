#pragma once
#include "artwork.hpp"
namespace nxh {
struct TrackDetails {
    std::string title, artist, album, format;
    uint32_t sampleRate = 0, bitDepth = 0, channels = 0, bitRate = 0;
    double duration = 0;
    bool durationEstimated = false;
};
inline void metaUTF8(std::string &out, uint32_t cp) {
    if (cp < 32 || cp == 127) {
        if (cp)
            out += ' ';
        return;
    }
    if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
        cp = 0xfffd;
    if (cp < 128)
        out += char(cp);
    else if (cp < 2048) {
        out += char(192 | (cp >> 6));
        out += char(128 | (cp & 63));
    } else if (cp < 65536) {
        out += char(224 | (cp >> 12));
        out += char(128 | ((cp >> 6) & 63));
        out += char(128 | (cp & 63));
    } else {
        out += char(240 | (cp >> 18));
        out += char(128 | ((cp >> 12) & 63));
        out += char(128 | ((cp >> 6) & 63));
        out += char(128 | (cp & 63));
    }
}
inline std::string metaText(const unsigned char *p, size_t n, int encoding = 3) {
    std::string out;
    if (encoding == 1 || encoding == 2) {
        bool little = false;
        size_t i = 0;
        if (n >= 2 && p[0] == 255 && p[1] == 254) {
            little = true;
            i = 2;
        } else if (n >= 2 && p[0] == 254 && p[1] == 255)
            i = 2;
        auto word = [&](size_t at) {
            return little ? uint32_t(p[at]) | (uint32_t(p[at + 1]) << 8) : artBE(p + at, 2);
        };
        for (; i + 1 < n && out.size() < 4096; i += 2) {
            uint32_t cp = word(i);
            if (!cp)
                break;
            if (cp >= 0xd800 && cp <= 0xdbff && i + 3 < n) {
                uint32_t low = word(i + 2);
                if (low >= 0xdc00 && low <= 0xdfff) {
                    cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                    i += 2;
                }
            }
            metaUTF8(out, cp);
        }
    } else {
        for (size_t i = 0; i < n && out.size() < 4096;) {
            uint32_t cp = p[i++];
            if (!cp)
                break;
            if (encoding == 3 && cp >= 128) {
                unsigned need = cp >= 0xf0 && cp <= 0xf4   ? 3
                                : cp >= 0xe0 && cp <= 0xef ? 2
                                : cp >= 0xc2 && cp <= 0xdf ? 1
                                                           : 0;
                if (!need || i + need > n) {
                    metaUTF8(out, 0xfffd);
                    continue;
                }
                uint32_t val = cp & (need == 3 ? 7 : need == 2 ? 15 : 31);
                bool ok = true;
                for (unsigned j = 0; j < need; j++)
                    if ((p[i + j] & 192) != 128)
                        ok = false;
                if (!ok) {
                    metaUTF8(out, 0xfffd);
                    continue;
                }
                for (unsigned j = 0; j < need; j++)
                    val = (val << 6) | (p[i++] & 63);
                if (val < (need == 3 ? 65536 : need == 2 ? 2048 : 128))
                    val = 0xfffd;
                cp = val;
            }
            metaUTF8(out, cp);
        }
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    size_t begin = out.find_first_not_of(' ');
    return begin == std::string::npos ? std::string{} : out.substr(begin);
}
inline uint32_t metaLE(const unsigned char *p, int n = 4) {
    uint32_t v = 0;
    for (int i = n - 1; i >= 0; i--)
        v = (v << 8) | p[i];
    return v;
}
inline void metaField(TrackDetails &m, std::string key, std::string value) {
    if (value.empty())
        return;
    key = lower(key);
    if (key == "title" || key == "tit2" || key == "tt2" || key == "inam")
        m.title = value;
    else if (key == "artist" || key == "tpe1" || key == "tp1" || key == "iart")
        m.artist = value;
    else if (key == "album" || key == "talb" || key == "tal" || key == "iprd")
        m.album = value;
}
inline void metaComments(TrackDetails &m, const std::vector<unsigned char> &b) {
    size_t p = 0;
    auto next = [&](size_t &n) {
        if (p + 4 > b.size())
            return false;
        n = metaLE(b.data() + p);
        p += 4;
        return n <= b.size() - p;
    };
    size_t n;
    if (!next(n))
        return;
    p += n;
    if (p + 4 > b.size())
        return;
    uint32_t count = metaLE(b.data() + p);
    p += 4;
    for (uint32_t i = 0; i < count; i++) {
        if (!next(n))
            break;
        if (n <= 65536) {
            std::string v = metaText(b.data() + p, n);
            auto eq = v.find('=');
            if (eq != std::string::npos)
                metaField(m, v.substr(0, eq), v.substr(eq + 1));
        }
        p += n;
    }
}
inline void inspectMp3(Input &in, TrackDetails &m) {
    if (!in.seek(0, SEEK_SET))
        return;
    unsigned char h[10];
    if (!artRead(in, h, 10))
        return;
    uint64_t start = 0;
    if (!std::memcmp(h, "ID3", 3))
        start = 10 + uint64_t(artBE(h + 6, 4, true)) + ((h[3] == 4 && (h[5] & 16)) ? 10 : 0);
    if (start >= in.size() || !in.seek(start, SEEK_SET))
        return;
    std::vector<unsigned char> b(std::min<uint64_t>(65536, in.size() - start));
    if (!artRead(in, b.data(), b.size()))
        return;
    struct Frame {
        int rate = 0, kbps = 0, size = 0, samples = 0, channels = 0, version = 0;
    };
    auto frame = [&](size_t p) {
        Frame f;
        if (p + 4 > b.size() || b[p] != 255 || (b[p + 1] & 224) != 224)
            return f;
        int version = (b[p + 1] >> 3) & 3, layer = (b[p + 1] >> 1) & 3, index = b[p + 2] >> 4,
            rateIndex = (b[p + 2] >> 2) & 3;
        if (version == 1 || layer != 1 || index == 0 || index == 15 || rateIndex == 3)
            return f;
        const int high[] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
        const int low[] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160};
        const int rates[] = {44100, 48000, 32000};
        f.version = version;
        f.rate = rates[rateIndex] / (version == 3 ? 1 : version == 2 ? 2 : 4);
        f.kbps = version == 3 ? high[index] : low[index];
        f.samples = version == 3 ? 1152 : 576;
        f.channels = (b[p + 3] >> 6) == 3 ? 1 : 2;
        f.size = (version == 3 ? 144000 : 72000) * f.kbps / f.rate + ((b[p + 2] >> 1) & 1);
        return f;
    };
    for (size_t p = 0; p + 4 < b.size(); p++) {
        auto f = frame(p);
        if (!f.rate)
            continue;
        if (p + f.size + 4 <= b.size() && frame(p + f.size).rate != f.rate)
            continue;
        m.sampleRate = f.rate;
        m.channels = f.channels;
        m.bitRate = f.kbps;
        size_t x = p + 4 + ((b[p + 1] & 1) ? 0 : 2) +
                   (f.version == 3 ? (f.channels == 1 ? 17 : 32) : (f.channels == 1 ? 9 : 17));
        uint32_t count = 0;
        if (x + 12 <= b.size() &&
            (!std::memcmp(b.data() + x, "Xing", 4) || !std::memcmp(b.data() + x, "Info", 4)) &&
            (artBE(b.data() + x + 4) & 1))
            count = artBE(b.data() + x + 8);
        size_t v = p + 36;
        if (!count && v + 18 <= b.size() && !std::memcmp(b.data() + v, "VBRI", 4))
            count = artBE(b.data() + v + 14);
        if (count) {
            m.duration = double(count) * f.samples / f.rate;
            m.bitRate =
                m.duration > 0 ? uint32_t((in.size() - start - p) * 8 / m.duration / 1000) : 0;
            return;
        }
        bool cbr = true;
        size_t next = p;
        int checked = 0;
        while (next + 4 <= b.size() && checked < 12) {
            auto other = frame(next);
            if (!other.rate)
                break;
            if (other.kbps != f.kbps || other.rate != f.rate) {
                cbr = false;
                break;
            }
            ++checked;
            next += other.size;
        }
        if (cbr && checked >= 3) {
            m.duration = double(in.size() - start - p) * 8 / (f.kbps * 1000.);
            m.durationEstimated = true;
        }
        return;
    }
}
inline void inspectOggDuration(Input &in, TrackDetails &m) {
    if (!m.sampleRate || !in.size())
        return;
    uint64_t start = in.size() > 65536 ? in.size() - 65536 : 0;
    if (!in.seek(start, SEEK_SET))
        return;
    std::vector<unsigned char> b(in.size() - start);
    if (!artRead(in, b.data(), b.size()))
        return;
    for (size_t p = 0; p + 27 <= b.size(); p++) {
        if (std::memcmp(b.data() + p, "OggS", 4) || b[p + 4] || !(b[p + 5] & 4))
            continue;
        size_t count = b[p + 26];
        if (p + 27 + count > b.size())
            continue;
        size_t bytes = 0;
        for (size_t i = 0; i < count; i++)
            bytes += b[p + 27 + i];
        if (bytes > b.size() - p - 27 - count)
            continue;
        uint64_t samples = 0;
        for (int i = 7; i >= 0; i--)
            samples = (samples << 8) | b[p + 6 + i];
        if (samples != UINT64_MAX && samples > 0)
            m.duration = double(samples) / m.sampleRate;
    }
}
inline TrackDetails readTrackDetails(Input &in, const std::string &path) {
    TrackDetails m;
    auto dot = path.find_last_of('.');
    auto ext = dot == std::string::npos ? "" : lower(path.substr(dot));
    m.format = ext == ".mp3"                      ? "MP3"
               : ext == ".flac"                   ? "FLAC"
               : (ext == ".ogg" || ext == ".oga") ? "Ogg Vorbis"
               : ext == ".wav"                    ? "WAV"
                                                  : "";
    struct Rewind {
        Input &in;
        ~Rewind() { in.seek(0, SEEK_SET); }
    } rewind{in};
    if (!in.seek(0, SEEK_SET))
        return m;
    unsigned char h[12]{};
    if (!artRead(in, h, 12))
        return m;
    if (!std::memcmp(h, "ID3", 3) && h[3] >= 2 && h[3] <= 4) {
        int version = h[3];
        unsigned flags = h[5];
        size_t n = artBE(h + 6, 4, true);
        if (n > 32 * 1024 * 1024 || (version == 2 && (flags & 64)) || !in.seek(10, SEEK_SET))
            return m;
        std::vector<unsigned char> b(n);
        if (!artRead(in, b.data(), n))
            return m;
        if (version < 4 && (flags & 128))
            b = deUnsync(b);
        size_t p = 0, head = version == 2 ? 6 : 10;
        if (version >= 3 && (flags & 64)) {
            if (b.size() < 4)
                return m;
            p = size_t(artBE(b.data(), 4, version == 4)) + (version == 3 ? 4 : 0);
        }
        while (p <= b.size() && head <= b.size() - p) {
            auto *f = b.data() + p;
            size_t len = version == 2 ? artBE(f + 3, 3) : artBE(f + 4, 4, version == 4);
            if (!len || len > b.size() - p - head)
                break;
            std::string id(reinterpret_cast<const char *>(f), version == 2 ? 3 : 4);
            bool unsupported = version == 3 ? (f[9] & 224) : version == 4 ? (f[9] & 76) : false;
            if (!unsupported && len <= 65536 && f[0] == 'T') {
                std::vector<unsigned char> v(b.begin() + p + head, b.begin() + p + head + len);
                if (version == 4 && ((flags & 128) || (f[9] & 2)))
                    v = deUnsync(v);
                size_t start = (version == 4 && (f[9] & 1)) ? 4 : 0;
                if (start < v.size() && v[start] <= 3)
                    metaField(m, id,
                              metaText(v.data() + start + 1, v.size() - start - 1, v[start]));
            }
            p += head + len;
        }
    } else if (!std::memcmp(h, "fLaC", 4)) {
        in.seek(4, SEEK_SET);
        size_t scanned = 4;
        while (scanned < 64 * 1024 * 1024) {
            if (!artRead(in, h, 4))
                break;
            unsigned type = h[0] & 127;
            bool last = h[0] & 128;
            size_t n = artBE(h + 1, 3);
            scanned += 4 + n;
            if ((type == 0 && n == 34) || (type == 4 && n <= 1024 * 1024)) {
                std::vector<unsigned char> b(n);
                if (!artRead(in, b.data(), n))
                    break;
                if (type == 4)
                    metaComments(m, b);
                else {
                    m.sampleRate = (uint32_t(b[10]) << 12) | (uint32_t(b[11]) << 4) | (b[12] >> 4);
                    m.channels = ((b[12] >> 1) & 7) + 1;
                    m.bitDepth = (((b[12] & 1) << 4) | (b[13] >> 4)) + 1;
                }
            } else if (!in.seek(n, SEEK_CUR))
                break;
            if (last)
                break;
        }
    } else if (!std::memcmp(h, "RIFF", 4) && !std::memcmp(h + 8, "WAVE", 4)) {
        for (int chunks = 0; chunks < 1024; chunks++) {
            if (!artRead(in, h, 8))
                break;
            size_t n = metaLE(h + 4);
            uint64_t next = in.position() + n + (n & 1);
            if (next > in.size())
                break;
            if (!std::memcmp(h, "fmt ", 4) && n >= 16) {
                unsigned char b[16];
                if (!artRead(in, b, 16))
                    break;
                m.channels = metaLE(b + 2, 2);
                m.sampleRate = metaLE(b + 4);
                m.bitDepth = metaLE(b + 14, 2);
            } else if (!std::memcmp(h, "LIST", 4) && n >= 4 && n <= 1024 * 1024) {
                std::vector<unsigned char> b(n);
                if (!artRead(in, b.data(), n))
                    break;
                if (!std::memcmp(b.data(), "INFO", 4))
                    for (size_t p = 4; p + 8 <= b.size();) {
                        size_t len = metaLE(b.data() + p + 4);
                        if (len > b.size() - p - 8)
                            break;
                        metaField(m, std::string(reinterpret_cast<char *>(b.data() + p), 4),
                                  metaText(b.data() + p + 8, len, 0));
                        p += 8 + len + (len & 1);
                    }
            }
            if (!in.seek(next, SEEK_SET))
                break;
        }
    } else if (!std::memcmp(h, "OggS", 4)) {
        in.seek(0, SEEK_SET);
        std::vector<unsigned char> packet;
        uint32_t serial = 0;
        bool haveSerial = false, done = false;
        for (int page = 0; page < 32 && !done; page++) {
            unsigned char pageHead[27];
            if (!artRead(in, pageHead, 27) || std::memcmp(pageHead, "OggS", 4) || pageHead[4])
                break;
            if (!haveSerial) {
                serial = metaLE(pageHead + 14);
                haveSerial = true;
            }
            unsigned char lace[255];
            size_t count = pageHead[26];
            if (!artRead(in, lace, count))
                break;
            if (metaLE(pageHead + 14) != serial) {
                size_t skip = 0;
                for (size_t i = 0; i < count; i++)
                    skip += lace[i];
                if (!in.seek(skip, SEEK_CUR))
                    break;
                continue;
            }
            for (size_t i = 0; i < count; i++) {
                size_t len = lace[i];
                size_t start = packet.size();
                if (start + len > 1024 * 1024) {
                    done = true;
                    break;
                }
                packet.resize(start + len);
                if (!artRead(in, packet.data() + start, len)) {
                    done = true;
                    break;
                }
                if (len < 255) {
                    if (metaLE(pageHead + 14) == serial && packet.size() >= 7 &&
                        !std::memcmp(packet.data() + 1, "vorbis", 6)) {
                        if (packet[0] == 1 && packet.size() >= 30) {
                            m.channels = packet[11];
                            m.sampleRate = metaLE(packet.data() + 12);
                        }
                        if (packet[0] == 3) {
                            metaComments(
                                m, std::vector<unsigned char>(packet.begin() + 7, packet.end()));
                            done = true;
                            break;
                        }
                    }
                    packet.clear();
                }
            }
        }
    }
    if (m.format == "MP3")
        inspectMp3(in, m);
    if (m.format == "Ogg Vorbis")
        inspectOggDuration(in, m);
    return m;
}
inline std::string audioDetails(const TrackDetails &m) {
    std::string out = m.format;
    if (m.bitDepth)
        out += (out.empty() ? "" : " / ") + std::to_string(m.bitDepth) + " bit";
    if (m.sampleRate) {
        char rate[40];
        std::snprintf(rate, sizeof rate, "%.3f", m.sampleRate / 1000.);
        std::string r = rate;
        while (!r.empty() && r.back() == '0')
            r.pop_back();
        if (!r.empty() && r.back() == '.')
            r.pop_back();
        out += (out.empty() ? "" : " / ") + r + " kHz";
    }
    return out;
}
} // namespace nxh
