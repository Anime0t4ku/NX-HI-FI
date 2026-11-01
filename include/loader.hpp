#pragma once
#include "storage.hpp"
#include "metadata.hpp"
#include "cover_image.hpp"
#include <fstream>
#include <condition_variable>
#include <mutex>
#include <thread>
namespace nxh {
struct LoadResult {
    int kind = 0;
    bool ready = false, ok = false, preferFolder = false;
    Source source;
    std::string path, error, playlistArtwork;bool preferPlaylistArtwork=false;
    TrackDetails details;
    std::vector<Entry> entries;
    std::vector<unsigned char> cover,encoded;
    std::shared_ptr<SDL_Surface> surface;
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
            if(job.kind==3){std::string error;auto input=openInput(job.source,job.path,error);if(input){job.details=readTrackDetails(*input,job.path);job.ok=true;}}
            else if (job.kind == 2) {
                if(job.preferPlaylistArtwork&&!job.playlistArtwork.empty()){std::ifstream f(job.playlistArtwork,std::ios::binary);job.cover.assign(std::istreambuf_iterator<char>(f),{});uint32_t w,h;if(!coverDimensions(job.cover,w,h))job.cover.clear();}
                if(job.cover.empty())job.cover = loadCover(job.source, job.path, job.preferFolder);
                if(job.cover.empty()&&!job.playlistArtwork.empty()){std::ifstream f(job.playlistArtwork,std::ios::binary);job.cover.assign(std::istreambuf_iterator<char>(f),{});}
                auto *surface=boundedCoverSurface(job.cover);
                if(surface&&std::max(surface->w,surface->h)>512){float ratio=512.f/std::max(surface->w,surface->h);auto *small=SDL_CreateRGBSurfaceWithFormat(0,std::max(1,int(surface->w*ratio)),std::max(1,int(surface->h*ratio)),32,SDL_PIXELFORMAT_RGBA32);if(small){SDL_SetSurfaceBlendMode(surface,SDL_BLENDMODE_NONE);SDL_BlitScaled(surface,nullptr,small,nullptr);SDL_FreeSurface(surface);surface=small;}}
                if(surface){job.encoded=remoteCoverJpeg(surface);job.surface=std::shared_ptr<SDL_Surface>(surface,SDL_FreeSurface);}
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
    void request(Source source, std::string path, int kind = 0, bool preferFolder = false,std::string playlistArtwork={},bool preferPlaylistArtwork=false) {
        std::lock_guard<std::mutex> l(mutex_);
        generation_++;
        job_ = {};
        job_.kind = kind;
        job_.preferFolder = preferFolder;job_.playlistArtwork=std::move(playlistArtwork);job_.preferPlaylistArtwork=preferPlaylistArtwork;
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
