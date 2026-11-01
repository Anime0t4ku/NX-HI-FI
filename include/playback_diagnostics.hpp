#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <functional>
#include <unistd.h>
namespace nxh {
// Opt-in diagnostics: stage names and numeric results only. Never log
// server addresses, paths, credentials or track metadata.
inline void playbackTrace(const char *stage, long long value = 0, bool reset = false) {
    static const bool enabled=[] {const char *value=std::getenv("PROSPERO_PLAYBACK_TRACE");if(value&&std::strcmp(value,"1")==0)return true;
#ifdef __SWITCH__
        return access("sdmc:/switch/nx-hifi/enable-playback-trace",F_OK)==0;
#else
        return access("/data/prospero-hifi/enable-playback-trace",F_OK)==0;
#endif
    }();
    if(!enabled)return;
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
#ifdef __SWITCH__
    const char *path = "sdmc:/switch/nx-hifi/playback-debug.log";
#else
    const char *path = "/data/prospero-hifi/playback-debug.log";
#endif
    if (reset) {
#ifdef __SWITCH__
        const char *previous = "sdmc:/switch/nx-hifi/playback-debug.previous.log";
#else
        const char *previous = "/data/prospero-hifi/playback-debug.previous.log";
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
    std::fclose(file);
}
} // namespace nxh
