#pragma once
#include "storage.hpp"
#include <condition_variable>
#include <mutex>
#include <thread>
namespace nxh {
struct LoadResult {
    int kind = 0;
    bool ready = false, ok = false, preferFolder = false;
    Source source;
    std::string path, error;
    std::vector<Entry> entries;
    std::vector<unsigned char> cover;
};
class Loader {
    std::mutex mutex_;
    std::condition_variable cond_;
    std::thread thread_;
    bool quit_ = false, pending_ = false;
    uint64_t generation_ = 0;
    LoadResult job_, result_;
    void loop() {
        for (;;) {
            LoadResult job;
            uint64_t gen;
            {
                std::unique_lock<std::mutex> l(mutex_);
                cond_.wait(l, [&] { return quit_ || pending_; });
                if (quit_)
                    return;
                job = job_;
                gen = generation_;
                pending_ = false;
            }
            if (job.kind == 2) {
                job.cover = loadCover(job.source, job.path, job.preferFolder);
                job.ok = true;
            } else {
                job.ok = listDirectory(job.source, job.path, job.entries, job.error);
            }
            job.ready = true;
            {
                std::lock_guard<std::mutex> l(mutex_);
                if (gen == generation_)
                    result_ = std::move(job);
            }
        }
    }

  public:
    Loader() { thread_ = std::thread(&Loader::loop, this); }
    ~Loader() {
        {
            std::lock_guard<std::mutex> l(mutex_);
            quit_ = true;
        }
        cond_.notify_one();
        thread_.join();
    }
    void request(Source source, std::string path, int kind = 0, bool preferFolder = false) {
        std::lock_guard<std::mutex> l(mutex_);
        generation_++;
        job_ = {};
        job_.kind = kind;
        job_.preferFolder = preferFolder;
        job_.source = std::move(source);
        job_.path = std::move(path);
        result_ = {};
        pending_ = true;
        cond_.notify_one();
    }
    void cancel() {
        std::lock_guard<std::mutex> l(mutex_);
        ++generation_;
        pending_ = false;
        result_ = {};
    }
    bool poll(LoadResult &out) {
        std::lock_guard<std::mutex> l(mutex_);
        if (!result_.ready)
            return false;
        out = std::move(result_);
        result_ = {};
        return true;
    }
};
} // namespace nxh
