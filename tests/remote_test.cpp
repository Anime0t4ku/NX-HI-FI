#include "remote.hpp"
#include <arpa/inet.h>
#include <cassert>
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>
using namespace nxh;
// HTTP tests isolate transport and dispatch from the platform SMB implementation.
namespace nxh {
bool listDirectory(const Source &source, const std::string &path, std::vector<Entry> &rows,
                   std::string &error) {
    if (source.root == "cdda:") {
        rows = {{"CD song", "disc-id/Track01.wav", false}};
        return true;
    }
    if (path == "unreadable") {
        error = "Unreadable folder.";
        return false;
    }
    rows = {
        {"Album", "Album", true}, {"One.flac", "One.flac", false}, {"Two.mp3", "Two.mp3", false}};
    return true;
}
} // namespace nxh
static std::string http(int port, const std::string &method, const std::string &path,
                        const std::string &body = "",
                        const std::string &extra = "X-NX-HiFi: 1\r\n") {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);
    timeval timeout{4, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    assert(connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof address) == 0);
    std::string request =
        method + " " + path + " HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) +
        "\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) +
        "\r\n" + extra + "Connection: close\r\n\r\n" + body;
    size_t done = 0;
    while (done < request.size()) {
        auto n = send(fd, request.data() + done, request.size() - done, 0);
        assert(n > 0);
        done += n;
    }
    std::string response;
    char buffer[4096];
    for (;;) {
        auto n = recv(fd, buffer, sizeof buffer, 0);
        if (n <= 0)
            break;
        response.append(buffer, n);
    }
    close(fd);
    return response;
}
static nlohmann::json body(const std::string &response) {
    auto at = response.find("\r\n\r\n");
    assert(at != std::string::npos);
    return nlohmann::json::parse(response.substr(at + 4));
}
int main() {
    std::mutex mutex;
    nlohmann::json command;
    RemoteServer server(0, [&](const auto &j) {
        std::lock_guard<std::mutex> lock(mutex);
        command = j;
        return nlohmann::json{{"ok", true}};
    });
    assert(server.valid());
    int port = server.port();
    Settings settings;
    settings.musicRoot = "sdmc:/music";
    Share share;
    share.server = "NAS";
    share.share = "Music";
    share.password = "private-example-secret";
    settings.shares = {share};
    server.publish({{"settings", settingsDocument(settings)}, {"revision", 4}}, settings, 9);
    auto state = http(port, "GET", "/api/state");
    assert(state.find("200 OK") != std::string::npos);
    assert(state.find("private-example-secret") == std::string::npos);
    assert(body(state)["settings"]["shares"][0]["password_set"] == true);
    assert(http(port, "GET", "/").find("NX Hi-Fi") != std::string::npos);
    assert(body(http(port, "POST", "/api/browse",
                     R"({"source":-1,"path":"","sources_revision":9})"))["entries"]
               .size() == 3);
    assert(body(http(port, "POST", "/api/browse",
                     R"({"source":-1,"path":"../secret","sources_revision":9})"))["ok"] == false);
    assert(body(http(port, "POST", "/api/browse",
                     R"({"source":-1,"path":"","sources_revision":8})"))["ok"] == false);
    assert(body(http(port, "POST", "/api/browse",
                     R"({"source":9,"path":"","sources_revision":9})"))["ok"] == false);
    assert(body(http(port, "POST", "/api/browse",
                     R"({"source":-1,"path":"unreadable","sources_revision":9})"))["ok"] == false);
    auto disc = body(http(port, "POST", "/api/browse",
                          R"({"source":-2,"path":"","sources_revision":9})"));
    assert(disc["ok"] == true && disc["source"] == -2 && disc["entries"][0]["name"] == "CD song");
    assert(body(http(port, "POST", "/api/cmd",
                     R"({"op":"play_folder","source":-2,"path":"","track":"disc-id/Track01.wav","sources_revision":9})"))["ok"] == true);
    { std::lock_guard<std::mutex> lock(mutex); assert(command["source"] == -2 && command["index"] == 0); }
    auto play = body(http(
        port, "POST", "/api/cmd",
        R"({"op":"play_folder","source":-1,"path":"","track":"Two.mp3","sources_revision":9})"));
    assert(play["ok"] == true);
    {
        std::lock_guard<std::mutex> lock(mutex);
        assert(command["op"] == "play" && command["index"] == 1 && command["tracks"].size() == 2);
    }
    assert(
        body(http(
            port, "POST", "/api/cmd",
            R"({"op":"play_folder","source":-1,"path":"","track":"missing.mp3","sources_revision":9})"))
            ["ok"] == false);
    assert(http(port, "POST", "/api/cmd", R"({"op":"stop"})", "").find("403 Forbidden") !=
           std::string::npos);
    assert(http(port, "POST", "/api/cmd", R"({"op":"stop"})",
                "X-NX-HiFi: 1\r\nOrigin: http://other.example\r\n")
               .find("403 Forbidden") != std::string::npos);
    assert(http(port, "POST", "/api/cmd", "invalid").find("400 Bad Request") != std::string::npos);
    assert(http(port, "POST", "/api/cmd", "{}", "X-NX-HiFi: 1\r\nContent-Length: 2\r\n")
               .find("400 Bad Request") != std::string::npos);
    server.artwork({255, 216, 255, 217});
    assert(http(port, "GET", "/api/art").find("Content-Type: image/jpeg") != std::string::npos);
    assert(http(port, "GET", "/api/art", "", "If-None-Match: \"1\"\r\n").find("304 Not Modified") !=
           std::string::npos);
    server.beginShutdown();
    assert(!server.valid());
    std::cout << "Remote HTTP routes, browse/play dispatch, source revisions, secret redaction, "
                 "request validation, artwork and shutdown passed.\n";
}
