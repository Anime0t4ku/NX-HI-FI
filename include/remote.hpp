#pragma once
#include "storage.hpp"
#include "discovery.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
namespace nxh {
class RemoteServer {
  public:
    using Json = nlohmann::json;
    using Handler = std::function<Json(const Json &)>;
    RemoteServer(int port, Handler handler);
    ~RemoteServer();
    void beginShutdown();
    bool valid() const {
        return listener_ >= 0;
    }
    int port() const {
        return port_;
    }
    std::string error() const {
        return error_;
    }
    void publish(Json state, const Settings &settings, uint64_t sourceRevision);
    void artwork(std::vector<unsigned char> bytes);
    void screenFrame(Json frame, std::vector<unsigned char> cover);

  private:
    int listener_ = -1, port_ = 0;
    std::string error_;
    std::atomic<bool> quit_{false};
    std::thread acceptor_;
    std::vector<std::thread> workers_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<int> clients_;
    std::vector<int> active_;
    Json state_, screen_ = Json::object();
    ClientScanner scanner_;
    std::vector<unsigned char> screenCover_;
    uint64_t screenCoverRevision_ = 0;
    Settings settings_;
    uint64_t sourcesRevision_ = 1, coverRevision_ = 0;
    std::vector<unsigned char> cover_;
    Handler handler_;
    void acceptLoop();
    void worker();
    bool serve(int fd);
    struct Stream {
        int fd = -1;
        bool screen = false;
        std::atomic<bool> done{false};
        std::thread thread;
    };
    std::vector<std::unique_ptr<Stream>> streams_;
    uint64_t sequence_ = 0;
    bool startStream(int fd, const std::string &key, bool screen = false);
    void streamLoop(Stream *stream);
    Json libraryRequest(const Json &, bool test);
};
} // namespace nxh
