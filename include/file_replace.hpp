#pragma once
#include <string>
#include <cstdio>
#include <cerrno>
#include <sys/stat.h>
namespace nxh {
// Switch SD rename does not replace existing destinations. Keep the previous
// file as a recovery backup and restore it if installing the staged file fails.
inline bool replaceStoredFile(const std::string &staged,const std::string &path){
 struct stat info{};bool exists=stat(path.c_str(),&info)==0;
 if(!exists&&errno!=ENOENT)return false;
 if(exists&&!S_ISREG(info.st_mode)){errno=EISDIR;return false;}
 auto backup=path+".bak";
 if(exists){if(std::remove(backup.c_str())!=0&&errno!=ENOENT)return false;if(std::rename(path.c_str(),backup.c_str())!=0)return false;}
 if(std::rename(staged.c_str(),path.c_str())==0)return true;
 int failure=errno;if(exists)std::rename(backup.c_str(),path.c_str());errno=failure;return false;
}
}
