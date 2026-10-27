#include "discovery.hpp"
#include "remote.hpp"
#include "screen_control.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
namespace nxh {
bool listDirectory(const Source &, const std::string &, std::vector<Entry> &, std::string &) {
    return false;
}
} // namespace nxh
using namespace nxh;
int main() {
    auto hosts = subnetHosts("192.168.23.183", "255.255.255.0");
    assert(hosts.size() == 253);
    assert(std::find(hosts.begin(), hosts.end(), "192.168.23.183") == hosts.end());
    assert(subnetHosts("192.168.23.183", "255.255.0.0").size() == 253);
    assert(subnetHosts("192.168.1.1", "255.0.255.0").empty());
    assert(subnetHosts("192.168.1.1", "0.0.0.0").empty());
    assert(subnetHosts("192.168.1.5", "255.255.255.252").size() == 1);
    RemoteServer server(8182, [](auto &) { return nlohmann::json{{"ok", true}}; });
    assert(server.valid());
    std::atomic<bool> cancel{false};
    RemoteClientConfig c{"Test", "127.0.0.1", "nx", 8182};
    auto r = probeRemote(c, cancel);
    assert(r.online && r.screen && r.client.port == 8182 && r.client.type == "nx");
    auto page = remoteHttp(c, "GET", "/api/info", {}, cancel);
    assert(page["version"] == Version && page["screen_control"] == true);
    ClientScanner scan;
    scan.start({"127.0.0.1"});
    for (int i = 0; i < 200 && scan.running(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(!scan.running() && scan.completed() == 1 && scan.results().size() == 1);
    c.host = "127.0.0.2";
    server.beginShutdown();
    assert(!probeRemote(c, cancel, 50).online);
    cancel = true;
    assert(!probeRemote(c, cancel, 50).online);
    ScreenSessions sessions;
    auto token = sessions.open(0);
    assert(!token.empty() && sessions.touch(token, 100));
    sessions.close(token);
    assert(!sessions.touch(token, 101));
    token = sessions.open(0);
    assert(!sessions.touch(token, 180001));
    assert(!sessions.active(180001));
    MinusHold minus;
    assert(minus.update(true, 0) == 0);
    assert(minus.update(true, 2999) == 0);
    assert(minus.update(true, 3000) == 2);
    assert(minus.update(true, 4000) == 0);
    assert(minus.update(false, 4001) == 0);
    assert(minus.update(true, 5000) == 0);
    assert(minus.update(false, 5200) == 1);
    assert(minus.update(false, 5300) == 0);
    std::cout << "Discovery/protocol verification, offline/cancel, subnet bounds, leases and exact "
                 "3-second Minus handling passed.\n";
}
