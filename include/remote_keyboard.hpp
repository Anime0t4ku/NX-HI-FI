#pragma once
#include <string>
#include <cstdint>
namespace nxh {
struct RemoteKeyboard {
    bool intercept = false, pending = false, answerReady = false, accepted = false, secret = false;
    uint64_t id = 0;
    std::string owner, label, initial, answer;
    void cancel() {
        pending = false;
        answerReady = false;
        owner.clear();
    }
    bool request(const std::string &title, std::string &value, bool password) {
        if (answerReady) {
            answerReady = false;
            if (accepted)
                value = answer;
            return accepted;
        }
        pending = true;
        ++id;
        label = title;
        initial = password ? "" : value;
        secret = password;
        return false;
    }
};
inline RemoteKeyboard remoteKeyboard;
} // namespace nxh
