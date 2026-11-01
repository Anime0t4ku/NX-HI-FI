#pragma once
#include "metadata.hpp"
#include "storage.hpp"
#include <mutex>
#include <condition_variable>
#include <thread>
#include <deque>
#include <atomic>
namespace nxh {
// Independent album pages shared by native and web views; no file-browser state.
class AlbumPages {
 struct Job{std::string key,path;Source source;uint64_t generation;};
 mutable std::mutex mutex;std::condition_variable condition;bool quit=false;std::thread worker;
 std::deque<Job> jobs;std::map<std::string,uint64_t> generations;
 std::map<std::string,nlohmann::json> pages;std::map<std::string,std::vector<unsigned char>> covers;
 uint64_t serial=0;std::atomic<uint64_t> revision_{0};
 void loop();void load(const Job &);bool cancelled(const Job &);void publish(const Job &,nlohmann::json);
public:
 uint64_t revision()const{return revision_.load();}
 AlbumPages();~AlbumPages();void request(std::string,Source,std::string,bool refresh=false);void invalidate(const std::string &);
 nlohmann::json page(const std::string &)const;nlohmann::json snapshot()const;std::vector<unsigned char> cover(const std::string &)const;
};
}
