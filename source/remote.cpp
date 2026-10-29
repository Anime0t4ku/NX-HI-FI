#include "playback_diagnostics.hpp"
#include "remote.hpp"
#include "disc.hpp"
#include "socket_write.hpp"
#include "websocket.hpp"
#include <sys/select.h>
#include "web_ui.hpp"
#include "screen_ui.hpp"
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
namespace nxh {
namespace {
void timeout(int fd) {
    timeval tv{2, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
}
bool sendAll(int fd, const void *data, size_t count) {
    return writeSocketBytes(fd, data, count);
}
void response(int fd, int code, const std::string &type, const void *bytes, size_t size,
              const std::string &extra = "") {
    const char *label = code == 200   ? "OK"
                        : code == 304 ? "Not Modified"
                        : code == 403 ? "Forbidden"
                        : code == 404 ? "Not Found"
                        : code == 413 ? "Payload Too Large"
                        : code == 503 ? "Service Unavailable"
                                      : "Bad Request";
    auto header = "HTTP/1.1 " + std::to_string(code) + " " + label + "\r\nContent-Type: " + type +
                  "\r\nContent-Length: " + std::to_string(size) +
                  "\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: "
                  "nosniff\r\nReferrer-Policy: same-origin\r\n" +
                  extra + "\r\n";
    if (sendAll(fd, header.data(), header.size()) && size)
        sendAll(fd, bytes, size);
}
void jsonResponse(int fd, const nlohmann::json &value, int code = 200) {
    auto body = value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    response(fd, code, "application/json; charset=utf-8", body.data(), body.size());
}
nlohmann::json failure(const std::string &error) {
    return {{"ok", false}, {"error", error}};
}
} // namespace
RemoteServer::RemoteServer(int port, Handler handler) : port_(port), handler_(std::move(handler)) {
    listener_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listener_ < 0) {
        error_ = "Cannot create remote socket.";
        return;
    }
    int yes = 1;
    setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    if (bind(listener_, reinterpret_cast<sockaddr *>(&address), sizeof address) < 0 ||
        listen(listener_, 8) < 0) {
        error_ = "Cannot open remote port " + std::to_string(port) + ".";
        close(listener_);
        listener_ = -1;
        return;
    }
    socklen_t size = sizeof address;
    if (getsockname(listener_, reinterpret_cast<sockaddr *>(&address), &size) == 0)
        port_ = ntohs(address.sin_port);
    fcntl(listener_, F_SETFL, fcntl(listener_, F_GETFL, 0) | O_NONBLOCK);
    for (int i = 0; i < 3; i++)
        workers_.emplace_back(&RemoteServer::worker, this);
    acceptor_ = std::thread(&RemoteServer::acceptLoop, this);
}
void RemoteServer::beginShutdown() {
    quit_ = true;
    condition_.notify_all();
    if (acceptor_.joinable())
        acceptor_.join();
    if (listener_ >= 0) {
        close(listener_);
        listener_ = -1;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    for (int fd : clients_)
        close(fd);
    clients_.clear();
    for (auto &s : streams_)
        if (s->fd >= 0)
            shutdown(s->fd, SHUT_RDWR);
    for (int fd : active_)
        shutdown(fd, SHUT_RDWR);
}
RemoteServer::~RemoteServer() {
    beginShutdown();
    for (auto &thread : workers_)
        if (thread.joinable())
            thread.join();
    for (auto &s : streams_)
        if (s->thread.joinable())
            s->thread.join();
}
void RemoteServer::publish(Json state, const Settings &s, uint64_t revision) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = std::move(state);
    settings_ = s;
    sourcesRevision_ = revision;
    ++sequence_;
    condition_.notify_all();
}
void RemoteServer::artwork(std::vector<unsigned char> bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    cover_ = std::move(bytes);
    ++coverRevision_;
    ++sequence_;
    condition_.notify_all();
}
void RemoteServer::screenFrame(Json frame, std::vector<unsigned char> cover) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (cover != screenCover_) {
        screenCover_ = std::move(cover);
        ++screenCoverRevision_;
    }
    screen_ = std::move(frame);
    screen_["cover_revision"] = screenCoverRevision_;
    screen_["has_artwork"] = !screenCover_.empty();
    ++sequence_;
}
void RemoteServer::acceptLoop() {
    while (!quit_) {
        int fd = accept(listener_, nullptr, nullptr);
        if (fd < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
            continue;
        }
        // Match Linux accept semantics on BSD/libnx too: HTTP workers use
        // blocking reads with a timeout; the listener alone is nonblocking.
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags >= 0)
            fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
        timeout(fd);
        std::lock_guard<std::mutex> lock(mutex_);
        if (clients_.size() >= 12 || quit_)
            close(fd);
        else {
            clients_.push_back(fd);
            condition_.notify_one();
        }
    }
}
void RemoteServer::worker() {
    while (!quit_) {
        int fd;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [&] { return quit_ || !clients_.empty(); });
            if (quit_)
                return;
            fd = clients_.front();
            clients_.pop_front();
            active_.push_back(fd);
        }
        bool transferred = false;
        try {
            transferred = serve(fd);
        } catch (...) {
            jsonResponse(fd, failure("Invalid request."), 400);
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            active_.erase(std::remove(active_.begin(), active_.end(), fd), active_.end());
            if (!transferred)
                close(fd);
        }
    }
}
RemoteServer::Json RemoteServer::libraryRequest(const Json &j, bool test) {
    Settings settings;
    uint64_t revision;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        settings = settings_;
        revision = sourcesRevision_;
    }
    if (!j.contains("sources_revision") ||
        (!j["sources_revision"].is_number_unsigned() &&
         !j["sources_revision"].is_number_integer()) ||
        j["sources_revision"].get<uint64_t>() != revision)
        return failure("Sources changed; refresh and try again.");
    int index = j.value("source", -1);
    if (index < (test ? -1 : -2) || index >= int(settings.shares.size()))
        return failure("Source no longer exists.");
    Source source{false, {}, settings.musicRoot};
    std::string error, path = j.value("path", std::string{});
    if (path.size() > 4096 || !safeRelative(path))
        return failure("Invalid folder path.");
    if (test) {
        Share share;
        if (!readShare(j.at("share"), index >= 0 ? &settings.shares[index] : nullptr, share, error))
            return failure(error);
        source = {true, share, share.folder};
        path = "";
    } else if (index == -2)
        source = {false, {}, DiscRoot};
    else if (index >= 0)
        source = {true, settings.shares[index], settings.shares[index].folder};
    std::vector<Entry> entries;
    if (!listDirectory(source, path, entries, error))
        return failure(error);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (revision != sourcesRevision_)
            return failure("Sources changed; refresh and try again.");
    }
    if (test)
        return {{"ok", true}, {"message", "SMB connected: folder is readable."}};
    Json rows = Json::array();
    for (auto &entry : entries)
        rows.push_back(
            {{"name", entry.name}, {"path", entry.path}, {"directory", entry.directory}});
    return {{"ok", true},
            {"source", index},
            {"path", path},
            {"entries", rows},
            {"sources_revision", revision}};
}
bool RemoteServer::serve(int fd) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
    std::string request;
    char buffer[4096];
    size_t split;
    while ((split = request.find("\r\n\r\n")) == std::string::npos) {
        if (std::chrono::steady_clock::now() > deadline)
            return false;
        auto n = recv(fd, buffer, sizeof buffer, 0);
        if (n <= 0)
            return false;
        request.append(buffer, n);
        if (request.size() > 16384) {
            jsonResponse(fd, failure("Request header too large."), 413);
            return false;
        }
    }
    auto first = request.find("\r\n");
    auto line = request.substr(0, first);
    auto space = line.find(' '), last = line.rfind(' ');
    if (space == std::string::npos || last == space) {
        jsonResponse(fd, failure("Invalid HTTP request."), 400);
        return false;
    }
    auto method = line.substr(0, space), path = line.substr(space + 1, last - space - 1);
    std::map<std::string, std::string> headers;
    for (size_t p = first + 2; p < split;) {
        auto end = request.find("\r\n", p), colon = request.find(':', p);
        if (colon == std::string::npos || colon > end) {
            jsonResponse(fd, failure("Invalid header."), 400);
            return false;
        }
        auto key = lower(request.substr(p, colon - p)),
             value = request.substr(colon + 1, end - colon - 1);
        auto start = value.find_first_not_of(" \t");
        value = start == std::string::npos ? "" : value.substr(start);
        if (headers.count(key)) {
            jsonResponse(fd, failure("Duplicate header."), 400);
            return false;
        }
        headers[key] = value;
        p = end + 2;
    }
    if (headers.count("origin") && headers["origin"] != "http://" + headers["host"] &&
        headers["origin"] != "https://" + headers["host"]) {
        jsonResponse(fd, failure("Use the NX Hi-Fi remote address directly."), 403);
        return false;
    }
    if (headers.count("sec-fetch-site") && headers["sec-fetch-site"] == "cross-site") {
        jsonResponse(fd, failure("Cross-site requests are not accepted."), 403);
        return false;
    }
    if (method == "GET" && (path == "/ws" || path == "/ws?screen=1")) {
        if (lower(headers["upgrade"]) != "websocket" ||
            lower(headers["connection"]).find("upgrade") == std::string::npos ||
            headers["sec-websocket-version"] != "13" || headers["sec-websocket-key"].size() != 24) {
            jsonResponse(fd, failure("Invalid WebSocket upgrade."), 400);
            return false;
        }
        return startStream(fd, headers["sec-websocket-key"],
                           path.find("screen=1") != std::string::npos);
    }
    if (method == "GET" && (path == "/" || path == "/index.html")) {
        response(fd, 200, "text/html; charset=utf-8", WebUI, sizeof(WebUI) - 1,
                 "Content-Security-Policy: default-src 'self'; script-src 'self' 'unsafe-inline'; "
                 "style-src 'self' 'unsafe-inline'; img-src 'self' data:; connect-src 'self'; "
                 "frame-ancestors 'none'\r\n");
        return false;
    }
    if (method == "GET" && path == "/screen") {
        response(
            fd, 200, "text/html; charset=utf-8", ScreenUI, sizeof(ScreenUI) - 1,
            "Content-Security-Policy: default-src 'none'; script-src 'unsafe-inline'; style-src "
            "'unsafe-inline'; img-src 'self'; connect-src 'self'; frame-ancestors 'none'\r\n");
        return false;
    }
    if (method == "GET" && path == "/api/info") {
        jsonResponse(fd, {{"app", "NX Hi-Fi"},
                          {"name", "NX Hi-Fi"},
                          {"version", Version},
                          {"port", 8182},
                          {"screen_control", true}});
        return false;
    }
    if (method == "GET" && path == "/api/discovery") {
        Json rows = Json::array();
        for (auto &r : scanner_.results())
            rows.push_back({{"name", r.client.name},
                            {"host", r.client.host},
                            {"type", r.client.type},
                            {"port", 8182},
                            {"screen_control", r.screen}});
        jsonResponse(fd, {{"ok", true},
                          {"running", scanner_.running()},
                          {"completed", scanner_.completed()},
                          {"total", scanner_.total()},
                          {"clients", rows}});
        return false;
    }
    if (method == "GET" && path.rfind("/api/screen/art", 0) == 0) {
        std::vector<unsigned char> bytes;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            bytes = screenCover_;
        }
        if (bytes.empty())
            jsonResponse(fd, failure("No screen artwork."), 404);
        else
            response(fd, 200, "image/jpeg", bytes.data(), bytes.size());
        return false;
    }
    if (method == "GET" && path == "/api/state") {
        Json state;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state = state_;
            state["cover_revision"] = coverRevision_;
            state["has_artwork"] = !cover_.empty();
        }
        jsonResponse(fd, state);
        return false;
    }
    if (method == "GET" && path.substr(0, 8) == "/api/art") {
        std::vector<unsigned char> cover;
        uint64_t revision;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            cover = cover_;
            revision = coverRevision_;
        }
        playbackTrace("http.artwork.request", cover.size());
        if (cover.empty()) {
            jsonResponse(fd, failure("No artwork."), 404);
            return false;
        }
        std::string tag = "\"" + std::to_string(revision) + "\"";
        if (headers["if-none-match"] == tag)
            response(fd, 304, "image/jpeg", nullptr, 0, "ETag: " + tag + "\r\n");
        else
            response(fd, 200, "image/jpeg", cover.data(), cover.size(), "ETag: " + tag + "\r\n");
        return false;
    }
    if (method != "POST" || (path != "/api/cmd" && path != "/api/browse" && path != "/api/test" &&
                             path != "/api/discovery" && path != "/api/probe")) {
        jsonResponse(fd, failure("Not found."), 404);
        return false;
    }
    if (headers["x-nx-hifi"] != "1" || headers["content-type"].find("application/json") != 0 ||
        headers.count("transfer-encoding")) {
        jsonResponse(fd, failure("A JSON remote request is required."), 403);
        return false;
    }
    auto count = headers["content-length"];
    if (count.empty() || count.find_first_not_of("0123456789") != std::string::npos ||
        count.size() > 7) {
        jsonResponse(fd, failure("Invalid content length."), 400);
        return false;
    }
    size_t length = std::stoul(count);
    if (length > 262144) {
        jsonResponse(fd, failure("Request too large."), 413);
        return false;
    }
    std::string body = request.substr(split + 4);
    while (body.size() < length) {
        if (std::chrono::steady_clock::now() > deadline)
            return false;
        auto n = recv(fd, buffer, std::min(sizeof buffer, length - body.size()), 0);
        if (n <= 0)
            return false;
        body.append(buffer, n);
    }
    if (body.size() != length) {
        jsonResponse(fd, failure("Invalid body length."), 400);
        return false;
    }
    auto j = Json::parse(body, nullptr, false);
    if (!j.is_object()) {
        jsonResponse(fd, failure("Invalid JSON."), 400);
        return false;
    }
    if (path == "/api/discovery") {
        if (j.value("cancel", false))
            scanner_.cancel();
        else if (!scanner_.running())
            scanner_.start(localSubnetHosts());
        jsonResponse(fd, {{"ok", true}});
        return false;
    }
    if (path == "/api/probe") {
        RemoteClientConfig c;
        c.host = j.value("host", std::string{});
        c.name = j.value("name", std::string{});
        c.type = j.value("type", std::string("nx"));
        std::string error;
        if (!validateRemoteClient(c, error)) {
            jsonResponse(fd, failure(error));
            return false;
        }
        auto result = probeRemote(c, quit_);
        jsonResponse(fd, {{"ok", result.online},
                          {"error", result.error},
                          {"screen_control", result.screen},
                          {"type", result.client.type}});
        return false;
    }
    if (path == "/api/browse" || path == "/api/test")
        jsonResponse(fd, libraryRequest(j, path == "/api/test"));
    else if (j.value("op", std::string{}) == "play_folder") {
        playbackTrace("http.play-folder.list.begin");
        auto listing = libraryRequest(j, false);
        playbackTrace("http.play-folder.list.end");
        if (!listing.value("ok", false)) {
            jsonResponse(fd, listing);
            return false;
        }
        auto tracks = Json::array();
        int selected = -1;
        auto chosen = j.value("track", std::string{});
        for (auto &row : listing["entries"])
            if (!row["directory"].get<bool>()) {
                if (row["path"] == chosen)
                    selected = tracks.size();
                tracks.push_back(row["path"]);
            }
        if (selected < 0) {
            jsonResponse(fd, failure("Track no longer exists."));
            return false;
        }
        playbackTrace("http.play-folder.submit", tracks.size());
        jsonResponse(fd, handler_({{"op", "play"},
                                   {"source", listing["source"]},
                                   {"sources_revision", listing["sources_revision"]},
                                   {"tracks", tracks},
                                   {"index", selected}}));
        playbackTrace("http.play-folder.response.sent");
    } else
        jsonResponse(fd, handler_(j));
    return false;
}

bool RemoteServer::startStream(int fd, const std::string &key, bool screen) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = streams_.begin(); it != streams_.end();) {
        if ((*it)->done) {
            (*it)->thread.join();
            it = streams_.erase(it);
        } else
            ++it;
    }
    if (quit_ || streams_.size() >= 6) {
        jsonResponse(fd, failure("Too many remote clients."), 503);
        return false;
    }
    auto header = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: "
                  "Upgrade\r\nSec-WebSocket-Accept: " +
                  websocketAccept(key) + "\r\n\r\n";
    if (!sendAll(fd, header.data(), header.size()))
        return false;
    auto stream = std::make_unique<Stream>();
    stream->fd = fd;
    stream->screen = screen;
    auto raw = stream.get();
    stream->thread = std::thread(&RemoteServer::streamLoop, this, raw);
    streams_.push_back(std::move(stream));
    return true;
}
void RemoteServer::streamLoop(Stream *stream) {
    auto fd = stream->fd;
    uint64_t seen = ~uint64_t(0);
    std::string previousTrack;
    auto last = std::chrono::steady_clock::now();
    while (!quit_) {
        Json state;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (seen != sequence_) {
                seen = sequence_;
                state = state_;
                state["cover_revision"] = coverRevision_;
                state["has_artwork"] = !cover_.empty();
                if (stream->screen)
                    state["screen"] = screen_;
            }
        }
        if (!state.is_null()) {
            std::string track =
                state.value("playback", Json::object()).value("track_key", std::string{});
            bool changed = track != previousTrack;
            if (changed)
                playbackTrace("websocket.track-state.encode.begin");
            auto payload = state.dump(-1, ' ', false, Json::error_handler_t::replace);
            if (changed)
                playbackTrace("websocket.track-state.encode.end", payload.size());
            if (!websocketWrite(fd, 1, payload))
                break;
            if (changed)
                playbackTrace("websocket.track-state.sent");
            previousTrack = std::move(track);
        }
        if (std::chrono::steady_clock::now() - last > std::chrono::seconds(10)) {
            if (!websocketWrite(fd, 9, "nx"))
                break;
            last = std::chrono::steady_clock::now();
        }
        fd_set r;
        FD_ZERO(&r);
        FD_SET(fd, &r);
        timeval wait{0, 50000};
        int ready = select(fd + 1, &r, nullptr, nullptr, &wait);
        if (ready < 0)
            break;
        if (ready > 0) {
            unsigned opcode;
            std::string data;
            if (!websocketRead(fd, opcode, data, true))
                break;
            if (opcode == 8)
                break;
            if (opcode == 9 && !websocketWrite(fd, 10, data))
                break;
        }
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        close(fd);
        stream->fd = -1;
    }
    stream->done = true;
}
} // namespace nxh
