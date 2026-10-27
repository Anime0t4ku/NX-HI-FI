#pragma once
#include "core.hpp"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <deque>
namespace nxh {
class ClientSession {
  public:
    using Json = nlohmann::json;
    struct Result {
        uint64_t id;
        bool ok = false;
        Json data;
        std::string bytes, error;
    };
    explicit ClientSession(RemoteClientConfig, bool screen = false);
    ~ClientSession();
    void stop();
    uint64_t request(std::string method, std::string path, Json body = Json::object());
    bool poll(Result &);
    Json snapshot();
    bool connected() const {
        return connected_;
    }
    std::string error();
    const RemoteClientConfig config;

  private:
    struct Job {
        uint64_t id;
        std::string method, path;
        Json body;
    };
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Job> jobs_;
    std::deque<Result> results_;
    std::atomic<bool> quit_{false}, connected_{false};
    std::thread events_, commands_;
    int eventFd_ = -1, commandFd_ = -1;
    uint64_t nextId_ = 0;
    Json state_ = Json::object();
    std::string error_;
    bool screen_ = false;
    int connectSocket(bool event);
    void closeSocket(bool event, int fd);
    Result http(const Job &);
    void eventLoop();
    void commandLoop();
};
std::string urlEncode(const std::string &);
} // namespace nxh
