#include "discovery.hpp"
#include "socket_write.hpp"
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <algorithm>
#ifdef __SWITCH__
#include <switch.h>
#else
#include <ifaddrs.h>
#include <net/if.h>
#endif
namespace nxh {
using J = nlohmann::json;
J remoteHttp(const RemoteClientConfig &c, const std::string &method, const std::string &path,
             const J &body, const std::atomic<bool> &cancel, int timeoutMs) {
    auto fail = [] {
        return J{{"ok", false}, {"error", "Remote client is offline or unavailable."}};
    };
    auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    auto left = [&] {
        return int(std::chrono::duration_cast<std::chrono::milliseconds>(
                       until - std::chrono::steady_clock::now())
                       .count());
    };
    addrinfo hints{}, *list = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (cancel || getaddrinfo(c.host.c_str(), std::to_string(c.port).c_str(), &hints, &list))
        return fail();
    int fd = -1;
    for (auto a = list; a && !cancel && left() > 0; a = a->ai_next) {
        fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0)
            continue;
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
        int rc = connect(fd, a->ai_addr, a->ai_addrlen);
        while (rc < 0 && !cancel && left() > 0) {
            pollfd p{fd, POLLOUT, 0};
            int n = poll(&p, 1, std::min(50, left()));
            if (n > 0) {
                int e = 0;
                socklen_t size = sizeof e;
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &e, &size);
                rc = e ? -1 : 0;
                break;
            }
        }
        if (rc == 0)
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(list);
    if (fd < 0)
        return fail();
    std::string data = method == "POST" ? body.dump() : "";
    auto req = method + " " + path + " HTTP/1.1\r\nHost: " + c.host + ":" + std::to_string(c.port) +
               "\r\nConnection: close\r\nX-NX-HiFi: 1\r\nX-NX-Hi-Fi: 1\r\nContent-Type: "
               "application/json\r\nContent-Length: " +
               std::to_string(data.size()) + "\r\n\r\n" + data;
    std::string response;
    if (!cancel && left() > 0 && writeSocketBytes(fd, req.data(), req.size(), left())) {
        char buffer[4096];
        while (!cancel && left() > 0 && response.size() < 256 * 1024) {
            pollfd p{fd, POLLIN, 0};
            if (poll(&p, 1, std::min(50, left())) <= 0)
                continue;
            auto n = recv(fd, buffer, sizeof buffer, 0);
            if (n <= 0)
                break;
            response.append(buffer, n);
            auto split = response.find("\r\n\r\n");
            if (split != std::string::npos) {
                auto h = lower(response.substr(0, split));
                auto pos = h.find("content-length:");
                if (pos != std::string::npos) {
                    auto length = strtoul(h.c_str() + pos + 15, nullptr, 10);
                    if (response.size() >= split + 4 + length)
                        break;
                }
            }
        }
    }
    close(fd);
    auto split = response.find("\r\n\r\n");
    if (cancel || split == std::string::npos || response.compare(9, 3, "200"))
        return fail();
    auto bytes = response.substr(split + 4);
    if (lower(response.substr(0, split)).find("transfer-encoding: chunked") != std::string::npos) {
        std::string decoded;
        size_t off = 0;
        for (;;) {
            auto end = bytes.find("\r\n", off);
            if (end == std::string::npos)
                return fail();
            char *tail = nullptr;
            auto line = bytes.substr(off, end - off);
            auto n = strtoul(line.c_str(), &tail, 16);
            if (tail == line.c_str() || (*tail && *tail != ';') || n > bytes.size() - end - 2)
                return fail();
            off = end + 2;
            if (!n)
                break;
            decoded.append(bytes, off, n);
            off += n;
            if (bytes.substr(off, 2) != "\r\n")
                return fail();
            off += 2;
        }
        bytes = std::move(decoded);
    }
    auto j = J::parse(bytes, nullptr, false);
    return j.is_discarded() ? fail() : j;
}
ProbeResult probeRemote(RemoteClientConfig c, const std::atomic<bool> &cancel, int ms) {
    try {
        c.port = 8182;
        ProbeResult out;
        out.client = c;
        auto info = remoteHttp(c, "GET", "/api/info", {}, cancel, ms);
        if (info.is_object() && info.value("app", std::string{}) == "NX Hi-Fi") {
            out.online = true;
            out.client.type = "nx";
            out.screen = info.value("screen_control", false);
            if (out.client.name.empty())
                out.client.name = info.value("name", std::string("NX Hi-Fi"));
            return out;
        }
        if (cancel)
            return out;
        auto state = remoteHttp(c, "GET", "/api/state", {}, cancel, ms);
        if (state.is_object() && state.contains("playback") && state["playback"].is_object() &&
            state.contains("sources_revision") && state.contains("settings")) {
            out.online = true;
            out.client.type = "nx";
        } else if (state.is_object() && state.contains("state") && state["state"].is_string() &&
                   (state.contains("title") || state.contains("track")) &&
                   (state.contains("volume") || state.contains("format"))) {
            out.online = true;
            out.client.type = "mister";
        }
        if (out.online && out.client.name.empty())
            out.client.name = out.client.type == "nx" ? "NX Hi-Fi" : "MiSTer Hi-Fi";
        if (!out.online)
            out.error = "Remote client is offline or is not a compatible Hi-Fi client.";
        return out;
    } catch (...) {
        ProbeResult r;
        r.client = c;
        r.error = "Client returned an incompatible response.";
        return r;
    }
}
std::vector<std::string> subnetHosts(const std::string &ip, const std::string &mask) {
    in_addr a{}, m{};
    std::vector<std::string> out;
    if (inet_pton(AF_INET, ip.c_str(), &a) != 1 || inet_pton(AF_INET, mask.c_str(), &m) != 1)
        return out;
    uint32_t addr = ntohl(a.s_addr), netmask = ntohl(m.s_addr), inverse = ~netmask;
    if (!addr || !netmask || (inverse & (inverse + 1)))
        return out;
    // Bound wide networks to the local /24, keeping discovery fast and cancellable.
    netmask |= 0xffffff00u;
    uint32_t begin = addr & netmask, end = begin | ~netmask;
    for (uint32_t host = begin + 1; host < end; ++host)
        if (host != addr) {
            in_addr b{htonl(host)};
            out.emplace_back(inet_ntoa(b));
        }
    return out;
}
std::vector<std::string> localSubnetHosts() {
    std::string ip, mask;
#ifdef __SWITCH__
    if (R_SUCCEEDED(nifmInitialize(NifmServiceType_User))) {
        u32 addr = 0, netmask = 0, gw = 0, dns1 = 0, dns2 = 0;
        if (R_SUCCEEDED(nifmGetCurrentIpConfigInfo(&addr, &netmask, &gw, &dns1, &dns2))) {
            in_addr a{addr}, m{netmask};
            ip = inet_ntoa(a);
            mask = inet_ntoa(m);
        }
        nifmExit();
    }
#else
    ifaddrs *all = nullptr;
    if (!getifaddrs(&all)) {
        for (auto a = all; a; a = a->ifa_next) {
            if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET || !a->ifa_netmask ||
                (a->ifa_flags & IFF_LOOPBACK))
                continue;
            ip = inet_ntoa(reinterpret_cast<sockaddr_in *>(a->ifa_addr)->sin_addr);
            mask = inet_ntoa(reinterpret_cast<sockaddr_in *>(a->ifa_netmask)->sin_addr);
            break;
        }
        freeifaddrs(all);
    }
#endif
    return subnetHosts(ip, mask);
}
ClientScanner::~ClientScanner() {
    cancel();
    if (worker_.joinable())
        worker_.join();
}
void ClientScanner::cancel() {
    cancel_ = true;
}
std::vector<ProbeResult> ClientScanner::results() {
    std::lock_guard<std::mutex> lock(mutex_);
    return results_;
}
void ClientScanner::start(std::vector<std::string> hosts) {
    std::lock_guard<std::mutex> lifecycle(lifecycle_);
    if (running_)
        return;
    cancel();
    if (worker_.joinable())
        worker_.join();
    cancel_ = false;
    completed_ = 0;
    total_ = hosts.size();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        results_.clear();
    }
    running_ = true;
    worker_ = std::thread([this, hosts = std::move(hosts)] {
        std::atomic<size_t> next{0};
        std::vector<std::thread> threads;
        for (int i = 0; i < 8; ++i)
            threads.emplace_back([&] {
                while (!cancel_) {
                    auto index = next++;
                    if (index >= hosts.size())
                        break;
                    RemoteClientConfig c;
                    c.host = hosts[index];
                    auto result = probeRemote(c, cancel_, 250);
                    if (result.online) {
                        std::lock_guard<std::mutex> lock(mutex_);
                        results_.push_back(std::move(result));
                    }
                    ++completed_;
                }
            });
        for (auto &t : threads)
            t.join();
        running_ = false;
    });
}
} // namespace nxh
