#pragma once
#include "core.hpp"
namespace nxh {
inline std::string hostString(const nlohmann::json &doc,const char *key,const std::string &fallback=""){
    if(!doc.is_object())return fallback;
    auto it=doc.find(key);return it!=doc.end()&&it->is_string()?it->get<std::string>():fallback;
}
inline std::string hostType(const nlohmann::json &doc,const std::string &fallback="nx"){
    if(!doc.is_object())return fallback;
    auto identity=doc.find("identity");
    auto id=identity!=doc.end()?hostString(*identity,"app_id",hostString(doc,"app_id")):hostString(doc,"app_id");
    if(id=="prospero-hifi")return "prospero";
    if(id=="nx-hifi")return "nx";
    if(id=="mister-hifi")return "mister";
    auto name=hostString(doc,"name",hostString(doc,"app"));
    if(name=="Prospero Hi-Fi"||hostString(doc,"platform")=="ps5")return "prospero";
    if(name=="MiSTer Hi-Fi")return "mister";
    return fallback;
}
inline std::string hostName(const std::string &type){return type=="prospero"?"Prospero Hi-Fi":type=="mister"?"MiSTer Hi-Fi":"NX Hi-Fi";}
inline bool hostCapability(const nlohmann::json &doc,const std::string &key,bool legacy=false){
    if(!doc.is_object())return legacy;
    auto identity=doc.find("identity");if(identity==doc.end()||!identity->is_object())return legacy;
    auto caps=identity->find("capabilities");if(caps==identity->end()||!caps->is_object())return legacy;
    auto flag=caps->find(key);return flag!=caps->end()&&flag->is_boolean()?flag->get<bool>():legacy;
}
}
