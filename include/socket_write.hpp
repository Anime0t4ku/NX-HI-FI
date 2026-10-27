#pragma once
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <poll.h>
#include <sys/socket.h>
namespace nxh {
// BSD accepted sockets may inherit nonblocking mode. A partial write or
// EAGAIN must not discard the rest of a cover. Bound both send size and wait.
inline bool writeSocketBytes(int fd, const void *data, size_t count, int timeoutMs = 6000) {
    auto *bytes = static_cast<const char *>(data);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (count) {
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                             deadline - std::chrono::steady_clock::now())
                             .count();
        if (remaining <= 0)
            return false;
        pollfd socket{fd, POLLOUT, 0};
        int ready = poll(&socket, 1, int(remaining));
        if (ready < 0 && errno == EINTR)
            continue;
        if (ready <= 0 || (socket.revents & (POLLERR | POLLHUP | POLLNVAL)))
            return false;
#ifdef MSG_NOSIGNAL
        int flags = MSG_NOSIGNAL;
#else
        int flags = 0;
#endif
        auto sent = send(fd, bytes, std::min(count, size_t(16384)), flags);
        if (sent < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
            continue;
        if (sent <= 0)
            return false;
        bytes += sent;
        count -= sent;
    }
    return true;
}
} // namespace nxh
