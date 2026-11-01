#include "storage_devices.hpp"
#include "disc.hpp"
#include <thread>
#include <mutex>
#include <condition_variable>
namespace nxh {
bool mountedStorageVolume(const std::string &path){return path=="sdmc:";}
std::vector<StorageDevice> storageInventory(const std::vector<std::string>&,bool optical){std::vector<StorageDevice> devices{{-1,"SD card","sdmc:"}};if(optical)devices.push_back({-2,"Physical Disc",DiscRoot});return devices;}
namespace {
struct Detection{std::mutex mutex;std::condition_variable wake;bool quit=false;std::vector<StorageDevice> devices{{-1,"SD card","sdmc:"}};std::thread worker;
Detection():worker([this]{for(;;){auto found=storageInventory({},usbOpticalDriveDetected());std::unique_lock<std::mutex> lock(mutex);devices=std::move(found);if(wake.wait_for(lock,std::chrono::seconds(3),[this]{return quit;}))break;}}){}
void stop(){{std::lock_guard<std::mutex> lock(mutex);quit=true;}wake.notify_all();if(worker.joinable())worker.join();}~Detection(){stop();}};
Detection &detection(){static Detection d;return d;}
}
std::vector<StorageDevice> detectedStorageDevices(){auto &d=detection();std::lock_guard<std::mutex> lock(d.mutex);return d.devices;}
void stopStorageDetection(){detection().stop();}
std::string storageFolder(const Settings &s,const StorageDevice &d){return d.id==-2?DiscRoot:s.musicRoot;}
bool resolveStorageSource(const Settings &s,int id,Source &source){if(id!=-1&&id!=-2)return false;source={false,{},id==-2?DiscRoot:s.musicRoot};return true;}
}
