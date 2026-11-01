#pragma once
#include <string>
#include <cstdlib>
namespace nxh {
inline bool validLocalRoot(const std::string &s){return s.rfind("sdmc:/",0)==0&&s.size()<=1024&&s.find('\\')==s.npos&&s.find("/../")==s.npos&&s.find("/./")==s.npos&&(s.size()<3||s.substr(s.size()-3)!="/..");}
inline bool validStorageFolder(const std::string &mount,const std::string &folder){return mount=="sdmc:"&&validLocalRoot(folder);}
inline const char *configPath(){
#ifdef __SWITCH__
return "sdmc:/switch/nx-hifi/config.json";
#else
const char *value=std::getenv("NX_HIFI_CONFIG");return value?value:"/tmp/nx-hifi/config.json";
#endif
}
}
