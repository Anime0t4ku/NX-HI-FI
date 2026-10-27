#include "client.hpp"
#include "websocket.hpp"
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <chrono>
namespace nxh {
namespace {
bool writeAll(int fd, const std::string &s) {
    size_t off = 0;
    while (off < s.size()) {
#ifdef MSG_NOSIGNAL
        auto n = send(fd, s.data() + off, s.size() - off, MSG_NOSIGNAL);
#else
        auto n = send(fd, s.data() + off, s.size() - off, 0);
#endif
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return false;
        off += n;
    }
    return true;
}
} // namespace
std::string urlEncode(const std::string &s) {
    std::string out;
    const char *hex = "0123456789ABCDEF";
    for (unsigned char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~')
            out += c;
        else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}
ClientSession::ClientSession(RemoteClientConfig c, bool screen)
    : config(std::move(c)), screen_(screen) {
    events_ = std::thread(&ClientSession::eventLoop, this);
    commands_ = std::thread(&ClientSession::commandLoop, this);
}
void ClientSession::stop() {
    quit_ = true;
    cv_.notify_all();
    std::lock_guard<std::mutex> lock(mutex_);
    if (eventFd_ >= 0)
        shutdown(eventFd_, SHUT_RDWR);
    if (commandFd_ >= 0)
        shutdown(commandFd_, SHUT_RDWR);
}
ClientSession::~ClientSession() {
    stop();
    if (events_.joinable())
        events_.join();
    if (commands_.joinable())
        commands_.join();
}
uint64_t ClientSession::request(std::string method, std::string path, Json body) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (jobs_.size() >= 32)
        return 0;
    auto id = ++nextId_;
    jobs_.push_back({id, std::move(method), std::move(path), std::move(body)});
    cv_.notify_one();
    return id;
}
bool ClientSession::poll(Result &r) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (results_.empty())
        return false;
    r = std::move(results_.front());
    results_.pop_front();
    return true;
}
ClientSession::Json ClientSession::snapshot() {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}
std::string ClientSession::error() {
    std::lock_guard<std::mutex> lock(mutex_);
    return error_;
}
int ClientSession::connectSocket(bool event) {
    addrinfo hints{}, *addresses = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(config.host.c_str(), std::to_string(config.port).c_str(), &hints, &addresses))
        return -1;
    int fd = -1;
    for (auto a = addresses; a && !quit_; a = a->ai_next) {
        fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0)
            continue;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (quit_) {
                close(fd);
                fd = -1;
                break;
            }
            (event ? eventFd_ : commandFd_) = fd;
        }
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        int rc = connect(fd, a->ai_addr, a->ai_addrlen);
        if (rc < 0 && errno == EINPROGRESS) {
            fd_set w;
            FD_ZERO(&w);
            FD_SET(fd, &w);
            timeval tv{3, 0};
            rc = select(fd + 1, nullptr, &w, nullptr, &tv);
            if (rc > 0) {
                int error = 0;
                socklen_t n = sizeof error;
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &n);
                rc = error ? -1 : 0;
            } else
                rc = -1;
        }
        if (rc == 0) {
            fcntl(fd, F_SETFL, flags);
            timeval tv{3, 0};
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
            break;
        }
        closeSocket(event, fd);
        fd = -1;
    }
    freeaddrinfo(addresses);
    return fd;
}
void ClientSession::closeSocket(bool event, int fd) {
    std::lock_guard<std::mutex> lock(mutex_);
    int &current = event ? eventFd_ : commandFd_;
    if (current == fd) {
        current = -1;
        close(fd);
    }
}
ClientSession::Result ClientSession::http(const Job &job) {
    Result result;
    result.id = job.id;
    int fd = connectSocket(false);
    if (fd < 0) {
        result.error = "Cannot connect to client.";
        return result;
    }
    std::string body = job.method == "POST" ? job.body.dump() : "";
    std::string req = job.method + " " + job.path + " HTTP/1.1\r\nHost: " + config.host + ":" +
                      std::to_string(config.port) +
                      "\r\nConnection: close\r\nX-NX-HiFi: 1\r\nX-NX-Hi-Fi: 1\r\nContent-Type: "
                      "application/json\r\nContent-Length: " +
                      std::to_string(body.size()) + "\r\n\r\n" + body;
    std::string response;
    if (writeAll(fd, req)) {
        char b[8192];
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(12);
        while (!quit_ && response.size() < 8 * 1024 * 1024 &&
               std::chrono::steady_clock::now() < deadline) {
            auto n = recv(fd, b, sizeof b, 0);
            if (n <= 0)
                break;
            response.append(b, n);
        }
    }
    closeSocket(false, fd);
    auto split = response.find("\r\n\r\n");
    if (split == std::string::npos) {
        result.error = "Client returned an incomplete response.";
        return result;
    }
    auto header = lower(response.substr(0, split));
    auto bytes = response.substr(split + 4);
    if (header.find("transfer-encoding: chunked") != std::string::npos) {
        std::string decoded;
        size_t off = 0;
        while (true) {
            auto end = bytes.find("\r\n", off);
            if (end == std::string::npos) {
                result.error = "Incomplete chunked response.";
                return result;
            }
            char *tail = nullptr;
            auto line = bytes.substr(off, end - off);
            auto n = strtoul(line.c_str(), &tail, 16);
            if (tail == line.c_str() || (*tail && *tail != ';') || n > bytes.size() - end - 2) {
                result.error = "Invalid chunked response.";
                return result;
            }
            off = end + 2;
            if (!n)
                break;
            decoded.append(bytes, off, n);
            off += n;
            if (bytes.substr(off, 2) != "\r\n") {
                result.error = "Invalid chunk delimiter.";
                return result;
            }
            off += 2;
        }
        bytes = std::move(decoded);
    }
    result.bytes = std::move(bytes);
    result.data = Json::parse(result.bytes, nullptr, false);
    result.ok = response.compare(9, 3, "200") == 0;
    if (result.ok && result.data.is_discarded() && job.path.rfind("/api/art", 0) != 0 &&
        job.path.rfind("/api/screen/art", 0) != 0) {
        result.ok = false;
        result.error = "Incomplete or invalid client response.";
    }
    if (!result.data.is_discarded() && result.data.is_object() &&
        result.data.value("ok", true) == false)
        result.ok = false;
    if (!result.ok && result.error.empty())
        result.error = !result.data.is_discarded() && result.data.is_object()
                           ? result.data.value("error", std::string("Client rejected the request."))
                       : result.bytes.empty() ? "Client rejected the request."
                                              : result.bytes.substr(0, 512);
    return result;
}
void ClientSession::commandLoop() {
    while (!quit_) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [&] { return quit_ || !jobs_.empty(); });
            if (quit_)
                break;
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }
        auto result = http(job);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (results_.size() < 64)
                results_.push_back(std::move(result));
        }
    }
}
void ClientSession::eventLoop() {
    while (!quit_) {
        int fd = connectSocket(true);
        bool open = false;
        if (fd >= 0) {
            auto key = websocketKey();
            std::string req = std::string("GET ") + (screen_ ? "/ws?screen=1" : "/ws") +
                              " HTTP/1.1\r\nHost: " + config.host + ":" +
                              std::to_string(config.port) +
                              "\r\nUpgrade: websocket\r\nConnection: "
                              "Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: " +
                              key + "\r\n\r\n";
            std::string header;
            if (!key.empty() && writeAll(fd, req)) {
                char c;
                while (header.size() < 16384 && header.find("\r\n\r\n") == std::string::npos &&
                       recv(fd, &c, 1, 0) == 1)
                    header += c;
                open = header.compare(9, 3, "101") == 0 &&
                       header.find(websocketAccept(key)) != std::string::npos;
            }
            if (open) {
                connected_ = true;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    error_.clear();
                }
                while (!quit_) {
                    fd_set r;
                    FD_ZERO(&r);
                    FD_SET(fd, &r);
                    timeval tv{1, 0};
                    int ready = select(fd + 1, &r, nullptr, nullptr, &tv);
                    if (ready < 0)
                        break;
                    if (!ready)
                        continue;
                    unsigned opcode;
                    std::string data;
                    if (!websocketRead(fd, opcode, data, false))
                        break;
                    if (opcode == 8)
                        break;
                    if (opcode == 9) {
                        if (!websocketWrite(fd, 10, data, true))
                            break;
                    } else if (opcode == 1) {
                        auto j = Json::parse(data, nullptr, false);
                        if (j.is_object()) {
                            std::lock_guard<std::mutex> lock(mutex_);
                            state_ = std::move(j);
                        }
                    }
                }
            }
            closeSocket(true, fd);
        }
        connected_ = false;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            error_ = "Disconnected. Reconnecting to " + config.name + "...";
            cv_.wait_for(lock, std::chrono::seconds(1), [&] { return quit_.load(); });
        }
    }
}
} // namespace nxh
