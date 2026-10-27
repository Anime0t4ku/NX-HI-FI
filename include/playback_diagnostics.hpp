#pragma once
#include <cstdio>
#include <mutex>
#include <thread>
#include <functional>
#include <unistd.h>
namespace nxh {
// Temporary diagnostic build: stage names and numeric results only. Never log
// server addresses, paths, credentials or track metadata.
inline void playbackTrace(const char *stage, long long value = 0, bool reset = false) {
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
#ifdef __SWITCH__
    const char *path = "sdmc:/switch/nx-hifi/playback-debug.log";
#else
    const char *path = "playback-debug.log";
#endif
    if (reset) {
#ifdef __SWITCH__
        const char *previous = "sdmc:/switch/nx-hifi/playback-debug.previous.log";
#else
        const char *previous = "playback-debug.previous.log";
#endif
        std::remove(previous);
        std::rename(path, previous);
    }
    FILE *file = std::fopen(path, reset ? "w" : "a");
    if (!file)
        return;
    auto thread =
        static_cast<unsigned long long>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    std::fprintf(file, "%llx %s %lld\n", thread, stage, value);
    std::fflush(file);
    fsync(fileno(file));
    std::fclose(file);
}
} // namespace nxh
