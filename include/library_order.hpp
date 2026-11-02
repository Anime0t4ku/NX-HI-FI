#pragma once
#include "core.hpp"
#include <set>
namespace nxh {
inline bool libraryNameLess(const std::string &left,const std::string &right){
    auto a=lower(left),b=lower(right);size_t i=0,j=0;
    auto digit=[](unsigned char c){return c>='0'&&c<='9';};
    while(i<a.size()&&j<b.size()){
        if(digit(a[i])&&digit(b[j])){
            size_t endA=i,endB=j;while(endA<a.size()&&digit(a[endA]))endA++;while(endB<b.size()&&digit(b[endB]))endB++;
            size_t startA=i,startB=j;while(startA<endA&&a[startA]=='0')startA++;while(startB<endB&&b[startB]=='0')startB++;
            size_t lengthA=endA-startA,lengthB=endB-startB;
            // Compare digit strings rather than converting: arbitrarily large numbers are safe.
            if(lengthA!=lengthB)return lengthA<lengthB;
            int numeric=a.compare(startA,lengthA,b,startB,lengthB);if(numeric)return numeric<0;
            if(endA-i!=endB-j)return endA-i<endB-j; // Equal values: fewer leading zeroes first.
            i=endA;j=endB;continue;
        }
        auto ca=static_cast<unsigned char>(a[i]),cb=static_cast<unsigned char>(b[j]);
        if(ca!=cb)return ca<cb;i++;j++;
    }
    if(i!=a.size()||j!=b.size())return i==a.size();
    return left<right; // Deterministic order for case-only differences.
}
inline void normalizeLibraryOrder(nlohmann::json &data){
 auto clock=data.value("order_clock",uint64_t(0));
 for(auto &p:data["playlists"]){if(!p.contains("sort"))p["sort"]="custom";for(auto &t:p["tracks"])if(!t.contains("added"))t["added"]=++clock;}
 for(auto &lib:data["libraries"]){if(!lib.contains("sort"))lib["sort"]="alphabetical";auto &added=lib["album_added"];if(!added.is_object())added=nlohmann::json::object();std::set<std::string> members;
 for(auto &a:data["albums"])if(std::find(a["libraries"].begin(),a["libraries"].end(),lib["id"])!=a["libraries"].end()){auto id=a["id"].get<std::string>();members.insert(id);if(!added.contains(id))added[id]=++clock;}
 for(auto it=added.begin();it!=added.end();)if(!members.count(it.key()))it=added.erase(it);else ++it;
 auto old=lib.value("order",nlohmann::json::array()),order=nlohmann::json::array();for(auto &id:old)if(id.is_string()&&members.erase(id.get<std::string>()))order.push_back(id);
 for(auto &a:data["albums"])if(members.erase(a["id"].get<std::string>()))order.push_back(a["id"]);lib["order"]=order;
 }data["order_clock"]=clock;
}
inline nlohmann::json orderedLibraryCatalog(nlohmann::json data){
 for(auto &p:data["playlists"]){auto mode=p.value("sort",std::string("custom"));if(mode!="custom")std::stable_sort(p["tracks"].begin(),p["tracks"].end(),[&](const auto &a,const auto &b){if(mode=="alphabetical")return libraryNameLess(a.value("title",std::string{}),b.value("title",std::string{}));return mode=="added_newest"?a.value("added",uint64_t(0))>b.value("added",uint64_t(0)):a.value("added",uint64_t(0))<b.value("added",uint64_t(0));});}
 for(auto &lib:data["libraries"]){auto order=lib.value("order",nlohmann::json::array());auto mode=lib.value("sort",std::string("alphabetical"));std::map<std::string,std::string> titles;for(const auto &a:data["albums"])titles[a["id"].get<std::string>()]=a.value("title",std::string{});if(mode!="custom")std::stable_sort(order.begin(),order.end(),[&](const auto &a,const auto &b){auto aid=a.template get<std::string>(),bid=b.template get<std::string>();if(mode=="alphabetical")return libraryNameLess(titles[aid],titles[bid]);auto aa=lib["album_added"].value(aid,uint64_t(0)),ba=lib["album_added"].value(bid,uint64_t(0));return mode=="added_newest"?aa>ba:aa<ba;});lib["album_order"]=order;
 }return data;
}
}
