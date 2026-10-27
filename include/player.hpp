#pragma once
#include "storage.hpp"
#include "metadata.hpp"
#include <SDL.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
namespace nxh {
struct Playback {
    Track track;
    TrackDetails details;
    int index = 0, count = 0;
    double position = 0, duration = 0;
    bool busy = false, playing = false, paused = false, ended = false;
    std::string error;
    std::array<float, 32> spectrum{};
};
class Player {
    SDL_AudioDeviceID device_ = 0;
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::atomic<uint64_t> generation_{0};
    std::atomic<bool> quitting_{false};
    bool pending_ = false;
    double seek_ = -1;
    std::vector<Track> queue_;
    int index_ = 0;
    Settings settings_;
    Playback state_;
    std::vector<std::array<float, 256>> cosines_, sines_;
    void work();
    void decode(Track, uint64_t);
    void requestLocked();

  public:
    explicit Player(const Settings &);
    ~Player();
    bool valid() const { return device_ != 0; }
    void settings(const Settings &);
    void play(std::vector<Track>, int);
    void stop();
    void pause();
    void next(bool manual = true);
    void previous();
    void seek(double);
    void tick();
    Playback snapshot();
    std::vector<Track> queue();
};
} // namespace nxh
