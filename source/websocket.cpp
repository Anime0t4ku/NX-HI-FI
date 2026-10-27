#include "websocket.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#ifdef __SWITCH__
#include <switch.h>
#else
#include <fcntl.h>
#endif
namespace nxh {
namespace {
bool randomBytes(void *p, size_t n) {
#ifdef __SWITCH__
    if (R_FAILED(csrngInitialize()))
        return false;
    auto rc = csrngGetRandomBytes(p, n);
    csrngExit();
    return R_SUCCEEDED(rc);
#else
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0)
        return false;
    size_t got = 0;
    while (got < n) {
        auto k = read(fd, (char *)p + got, n - got);
        if (k <= 0) {
            close(fd);
            return false;
        }
        got += k;
    }
    close(fd);
    return true;
#endif
}
bool transfer(int fd, void *p, size_t n, bool sendMode) {
    while (n) {
#ifdef MSG_NOSIGNAL
        int flags = sendMode ? MSG_NOSIGNAL : 0;
#else
        int flags = 0;
#endif
        auto k = sendMode ? send(fd, p, n, flags) : recv(fd, p, n, 0);
        if (k < 0 && errno == EINTR)
            continue;
        if (k <= 0)
            return false;
        p = (char *)p + k;
        n -= k;
    }
    return true;
}
std::string base64(const unsigned char *b, size_t n) {
    const char *alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < n; i += 3) {
        unsigned v = b[i] << 16;
        if (i + 1 < n)
            v |= b[i + 1] << 8;
        if (i + 2 < n)
            v |= b[i + 2];
        out += alphabet[(v >> 18) & 63];
        out += alphabet[(v >> 12) & 63];
        out += i + 1 < n ? alphabet[(v >> 6) & 63] : '=';
        out += i + 2 < n ? alphabet[v & 63] : '=';
    }
    return out;
}
uint32_t rotate(uint32_t n, int b) { return (n << b) | (n >> (32 - b)); }
} // namespace
std::string websocketKey() {
    unsigned char b[16];
    if (!randomBytes(b, sizeof b))
        return {};
    return base64(b, sizeof b);
}
std::string websocketAccept(const std::string &key) {
    std::string s = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    uint64_t bits = s.size() * 8;
    s += '\x80';
    while (s.size() % 64 != 56)
        s += '\0';
    for (int i = 7; i >= 0; --i)
        s += char(bits >> (i * 8));
    uint32_t h[] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
    for (size_t off = 0; off < s.size(); off += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = 0;
            for (int j = 0; j < 4; ++j)
                w[i] = (w[i] << 8) | (unsigned char)s[off + i * 4 + j];
        }
        for (int i = 16; i < 80; ++i)
            w[i] = rotate(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | (~b & d);
                k = 0x5a827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ed9eba1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8f1bbcdc;
            } else {
                f = b ^ c ^ d;
                k = 0xca62c1d6;
            }
            uint32_t t = rotate(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rotate(b, 30);
            b = a;
            a = t;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }
    unsigned char bytes[20];
    for (int i = 0; i < 20; ++i)
        bytes[i] = h[i / 4] >> (24 - 8 * (i % 4));
    return base64(bytes, 20);
}
bool websocketWrite(int fd, unsigned opcode, const std::string &payload, bool masked) {
    std::vector<unsigned char> out;
    out.push_back(0x80 | opcode);
    uint64_t n = payload.size();
    unsigned flag = masked ? 128 : 0;
    if (n < 126)
        out.push_back(flag | n);
    else if (n <= 65535) {
        out.push_back(flag | 126);
        out.push_back(n >> 8);
        out.push_back(n);
    } else {
        out.push_back(flag | 127);
        for (int i = 7; i >= 0; --i)
            out.push_back(n >> (i * 8));
    }
    unsigned char mask[4]{};
    if (masked) {
        if (!randomBytes(mask, 4))
            return false;
        out.insert(out.end(), mask, mask + 4);
    }
    for (size_t i = 0; i < n; ++i)
        out.push_back((unsigned char)payload[i] ^ (masked ? mask[i % 4] : 0));
    return transfer(fd, out.data(), out.size(), true);
}
bool websocketRead(int fd, unsigned &opcode, std::string &payload, bool expectMasked,
                   size_t maximum) {
    unsigned char h[2];
    if (!transfer(fd, h, 2, false))
        return false;
    opcode = h[0] & 15;
    bool masked = h[1] & 128;
    if (!(h[0] & 128) || (h[0] & 0x70) || masked != expectMasked)
        return false;
    uint64_t n = h[1] & 127;
    if (n == 126) {
        unsigned char b[2];
        if (!transfer(fd, b, 2, false))
            return false;
        n = (b[0] << 8) | b[1];
    } else if (n == 127) {
        unsigned char b[8];
        if (!transfer(fd, b, 8, false))
            return false;
        n = 0;
        for (auto c : b)
            n = (n << 8) | c;
    }
    if (n > maximum || ((opcode & 8) && n > 125))
        return false;
    unsigned char mask[4]{};
    if (masked && !transfer(fd, mask, 4, false))
        return false;
    payload.resize(n);
    if (n && !transfer(fd, payload.data(), n, false))
        return false;
    if (masked)
        for (size_t i = 0; i < n; ++i)
            payload[i] ^= mask[i % 4];
    return true;
}
} // namespace nxh
