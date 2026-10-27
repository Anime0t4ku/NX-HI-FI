#include "socket_write.hpp"
#include <cassert>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
int main() {
    int sockets[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    int small = 2048;
    setsockopt(sockets[0], SOL_SOCKET, SO_SNDBUF, &small, sizeof small);
    assert(fcntl(sockets[0], F_SETFL, fcntl(sockets[0], F_GETFL, 0) | O_NONBLOCK) == 0);
    std::vector<char> data(84933);
    for (size_t i = 0; i < data.size(); ++i)
        data[i] = char(i * 13);
    bool ok = false;
    std::thread writer([&] {
        ok = nxh::writeSocketBytes(sockets[0], data.data(), data.size());
        shutdown(sockets[0], SHUT_WR);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    std::vector<char> received;
    char buffer[1024];
    for (;;) {
        auto n = recv(sockets[1], buffer, sizeof buffer, 0);
        if (n <= 0)
            break;
        received.insert(received.end(), buffer, buffer + n);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    writer.join();
    assert(ok && received == data);
    close(sockets[0]);
    close(sockets[1]);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    setsockopt(sockets[0], SOL_SOCKET, SO_SNDBUF, &small, sizeof small);
    fcntl(sockets[0], F_SETFL, O_NONBLOCK);
    auto start = std::chrono::steady_clock::now();
    assert(!nxh::writeSocketBytes(sockets[0], data.data(), data.size(), 100));
    assert(std::chrono::steady_clock::now() - start < std::chrono::seconds(1));
    close(sockets[0]);
    close(sockets[1]);
    std::cout
        << "84,933-byte cover survives nonblocking socket backpressure; stalled writes time out.\n";
}
