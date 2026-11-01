#pragma once
#include "storage.hpp"
#include "discovery.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <memory>
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
    bool wantsLive()const{return liveClients_.load()>0||std::chrono::steady_clock::now().time_since_epoch()/std::chrono::milliseconds(1)-lastAccess_.load()<2000;}
    int port() const {
        return port_;
    }
    std::string error() const {
        return error_;
    }
    void publish(Json state, const Settings &settings, uint64_t sourceRevision);
    void albumArtwork(std::function<std::vector<unsigned char>(const std::string &)> fn){std::lock_guard<std::mutex> l(mutex_);albumArt_=std::move(fn);}
    void artwork(std::vector<unsigned char> bytes);


  private:
    int listener_ = -1, port_ = 0;
    std::string error_;
    std::atomic<bool> quit_{false};
    std::atomic<unsigned> liveClients_{0};
    std::atomic<int64_t> lastAccess_{0};
    std::thread acceptor_;
    std::vector<std::thread> workers_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<int> clients_;
    std::vector<int> active_;
    std::shared_ptr<const Json> state_=std::make_shared<const Json>(Json::object());
    std::shared_ptr<const Json> live_=std::make_shared<const Json>(Json::object());
    ClientScanner scanner_;

    Settings settings_;
    uint64_t sourcesRevision_ = 1, coverRevision_ = 0;
    std::vector<unsigned char> cover_;
    Handler handler_;
    std::function<std::vector<unsigned char>(const std::string &)> albumArt_;
    void acceptLoop();
    void worker();
    bool serve(int fd);
    struct Stream {
        int fd = -1;
        bool delta = false;
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
