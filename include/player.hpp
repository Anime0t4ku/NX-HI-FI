#pragma once
#include "storage.hpp"
#include "metadata.hpp"
#include "audio_analysis.hpp"
#include <deque>
#include <SDL.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <future>
namespace nxh {
struct Playback {
    Track track;
    TrackDetails details;
    int index = 0, count = 0;
    double position = 0, duration = 0;
    bool busy = false, playing = false, paused = false, ended = false;
    std::string error;
    uint64_t analysisFrame=0,analysisEpoch=0;
    std::array<float, 32> spectrum{};
    // Local visualizers only: real post-EQ stereo PCM, never sent to remote clients.
    std::array<float,256> waveLeft{}, waveRight{};
};
struct PreparedAudio;
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
    struct QueuedTrack{Playback playback;uint64_t start,end;};
    std::deque<QueuedTrack> audibleTracks_;uint64_t submittedTotal_=0,trackStart_=0;
    AudioVisualQueue visualQueue_;
    uint64_t visualEpoch_=0;
    void resetVisualsLocked(uint64_t origin=0){
        visualQueue_.reset(origin);
        state_.analysisEpoch=++visualEpoch_;state_.analysisFrame=origin;
        state_.spectrum={};state_.waveLeft={};state_.waveRight={};
    }
    void publishVisualsLocked(std::vector<AudioVisualFrame> frames,uint64_t submitted){
        visualQueue_.publish(std::move(frames),submitted);
    }
    std::future<std::shared_ptr<PreparedAudio>> prepared_;
    Track preparedTrack_;uint64_t preparedGeneration_=0;bool handoff_=false;
    int audibleIndexLocked();
    void work();
    void decode(Track, uint64_t);
    void decodeDisc(Track, uint64_t);
    void requestLocked();

  public:
    explicit Player(const Settings &);
    ~Player();
    bool valid() const { return device_ != 0; }
    void settings(const Settings &);
    void metadata(const TrackDetails &,const Source &,const std::string &);
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
