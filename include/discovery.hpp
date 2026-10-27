#pragma once
#include "core.hpp"
#include <atomic>
#include <mutex>
#include <thread>
namespace nxh {
struct ProbeResult {
    bool online = false, screen = false;
    RemoteClientConfig client;
    std::string error;
};
nlohmann::json remoteHttp(const RemoteClientConfig &, const std::string &method,
                          const std::string &path, const nlohmann::json &body,
                          const std::atomic<bool> &cancel, int timeoutMs = 1500);
ProbeResult probeRemote(RemoteClientConfig, const std::atomic<bool> &, int timeoutMs = 1200);
std::vector<std::string> subnetHosts(const std::string &ip, const std::string &mask);
std::vector<std::string> localSubnetHosts();
class ClientScanner {
  public:
    ~ClientScanner();
    void start(std::vector<std::string> hosts);
    void cancel();
    bool running() const {
        return running_;
    }
    int completed() const {
        return completed_;
    }
    int total() const {
        return total_;
    }
    std::vector<ProbeResult> results();

  private:
    std::atomic<bool> cancel_{false}, running_{false};
    std::atomic<int> completed_{0}, total_{0};
    std::thread worker_;
    std::mutex mutex_, lifecycle_;
    std::vector<ProbeResult> results_;
};
} // namespace nxh
