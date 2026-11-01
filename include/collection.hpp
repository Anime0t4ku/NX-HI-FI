#pragma once
#include "storage.hpp"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <functional>
#include <set>
namespace nxh {
using CollectionJSON=nlohmann::json;
CollectionJSON sourceReference(const Source &);
bool resolveReference(const CollectionJSON &,const Settings &,Source &,std::string &);
int collectionHTTP(const std::string &,std::vector<unsigned char> &,size_t limit=4*1024*1024,bool probe=false);
class CollectionStore {
public:
 using Fetch=std::function<int(const std::string &,std::vector<unsigned char> &,size_t,bool)>;
private:
 Fetch fetch;
 std::string directory;mutable std::mutex mutex;std::condition_variable cv;bool quit=false;std::thread thread;
 std::set<std::string> requestedImages;CollectionJSON data;std::deque<std::function<void()>> jobs;uint64_t serial=0;std::string message;
 std::string nextID();bool saveLocked(std::string &);void loop();void enqueue(std::function<void()>);
 void scan(std::string,Source,std::string);void match(std::string,std::string,std::string,bool automatic=false);void artwork(std::string,std::string);
public:
 explicit CollectionStore(std::string,Fetch=collectionHTTP);~CollectionStore();
 uint64_t revision()const;CollectionJSON snapshot()const;CollectionJSON command(const CollectionJSON &,const Settings &);
 std::string coverPath(const std::string &)const;
 std::string imagePath(const std::string &,const std::string &)const;
 void fetchImage(std::string,std::string,std::string);
 void ensureImage(const std::string &,const std::string &);
};
}
