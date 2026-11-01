#pragma once
#include "core.hpp"
namespace nxh {
struct StorageDevice {
    int id=-1;
    std::string name,mount;
    bool operator==(const StorageDevice &other)const{return id==other.id&&name==other.name&&mount==other.mount;}
};
bool mountedStorageVolume(const std::string &path);
std::vector<StorageDevice> storageInventory(const std::vector<std::string> &mounts,bool optical);
std::vector<StorageDevice> detectedStorageDevices();
void stopStorageDetection();
bool usbOpticalDriveDetected();
std::string storageFolder(const Settings &,const StorageDevice &);
bool resolveStorageSource(const Settings &,int,Source &);
}
