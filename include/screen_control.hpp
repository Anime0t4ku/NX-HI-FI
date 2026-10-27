#pragma once
#include <map>
#include <string>
#include <cstdint>
namespace nxh {
// UI-thread ownership. A lease expires if a controller disappears without closing.
class ScreenSessions {
    std::map<std::string, uint64_t> leases_;
    uint64_t next_ = 0;

  public:
    void expire(uint64_t now) {
        for (auto i = leases_.begin(); i != leases_.end();)
            if (now - i->second > 180000)
                i = leases_.erase(i);
            else
                ++i;
    }
    std::string open(uint64_t now) {
        expire(now);
        if (leases_.size() >= 4)
            return {};
        auto id = std::to_string(now) + "-" + std::to_string(++next_);
        leases_[id] = now;
        return id;
    }
    bool touch(const std::string &id, uint64_t now) {
        expire(now);
        auto i = leases_.find(id);
        if (i == leases_.end())
            return false;
        i->second = now;
        return true;
    }
    void close(const std::string &id) {
        leases_.erase(id);
    }
    bool active(uint64_t now) {
        expire(now);
        return !leases_.empty();
    }
};
// Minus is forwarded only on release; a hold opens a local menu without
// generating a short press on the target.
struct MinusHold {
    uint64_t started = 0;
    bool pressed = false, consumed = false;
    int update(bool held, uint64_t now) {
        if (held) {
            if (!pressed) {
                pressed = true;
                consumed = false;
                started = now;
            }
            if (!consumed && now - started >= 3000) {
                consumed = true;
                return 2;
            }
        } else if (pressed) {
            pressed = false;
            if (!consumed)
                return 1;
        }
        return 0;
    }
};
} // namespace nxh
