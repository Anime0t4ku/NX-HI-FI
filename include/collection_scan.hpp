#pragma once
#include "metadata.hpp"
#include "extended_audio.hpp"
#include <set>
namespace nxh {
inline bool collectAlbumFiles(const Source &source,const std::string &path,std::vector<Entry> &tracks,std::string &error,const std::function<bool()> &cancel,int depth=0){
 if(cancel())return false;if(depth>8){error="Album folders exceed 8 levels.";return false;}
 std::vector<Entry> entries;if(!listDirectory(source,path,entries,error))return false;
 for(const auto &entry:entries){if(cancel())return false;if(entry.directory){if(!collectAlbumFiles(source,entry.path,tracks,error,cancel,depth+1))return false;}else if(audioFile(entry.path)){if(tracks.size()>=5000){error="Album exceeds 5000 tracks.";return false;}tracks.push_back(entry);}}
 return true;
}
inline int discFolderNumber(std::string folder,int fallback){folder=lower(filename(folder));if(folder.rfind("cd",0)!=0&&folder.rfind("disc",0)!=0&&folder.rfind("disk",0)!=0)return fallback;auto p=folder.find_first_of("0123456789");return p==folder.npos?fallback:std::max(1,std::atoi(folder.c_str()+p));}
inline nlohmann::json scanAlbumTracks(const Source &source,const std::string &path,TrackDetails &album,std::string &error,const std::function<bool()> &cancel){
 std::vector<Entry> entries;if(!collectAlbumFiles(source,path,entries,error,cancel))return nullptr;
 nlohmann::json rows=nlohmann::json::array();std::map<std::string,int> folders;std::map<int,int> numbers;
 for(const auto &entry:entries){auto dir=parent(entry.path);if(dir==path)folders[dir]=1;}
 for(const auto &entry:entries){if(cancel())return nullptr;auto dir=parent(entry.path);if(!folders.count(dir))folders[dir]=discFolderNumber(dir,int(folders.size())+1);TrackDetails m;auto input=openInput(source,entry.path,error);if(input){m=readTrackDetails(*input,entry.path);if(lower(filename(entry.path)).find(".m4a")!=std::string::npos){ExtendedAudio decoder;std::string ignored;decoder.open(*input,false,m,ignored);}}else return nullptr;
 int disc=m.discNumber>0?m.discNumber:folders[dir];int number=m.trackNumber>0?m.trackNumber:numbers[disc]+1;numbers[disc]=std::max(numbers[disc],number);auto name=m.title.empty()?title(entry.name):m.title;
 if(rows.empty()){album=m;if(!m.albumArtist.empty())album.artist=m.albumArtist;}
 rows.push_back({{"path",entry.path},{"name",name},{"title",name},{"artist",m.artist},{"duration",m.duration},{"disc",disc},{"disc_title",dir==path?"Disc "+std::to_string(disc):filename(dir)},{"number",number}});
 }
 std::stable_sort(rows.begin(),rows.end(),[](const auto &a,const auto &b){return a["disc"]==b["disc"]?a["number"]<b["number"]:a["disc"]<b["disc"];});return rows;
}
inline void applyAlbumOverrides(nlohmann::json &album){
 auto overrides=album.value("overrides",nlohmann::json::object());for(auto key:{"title","artist","year","genre"})if(overrides.contains(key))album[key]=overrides[key];
 auto tracks=overrides.value("tracks",nlohmann::json::object());for(auto &row:album["tracks"]){auto path=row["path"].template get<std::string>();if(tracks.contains(path)){for(auto key:{"title","artist","disc","disc_title","number"})if(tracks[path].contains(key))row[key]=tracks[path][key];}row["name"]=row.value("title",row.value("name",std::string{}));}
 std::stable_sort(album["tracks"].begin(),album["tracks"].end(),[](const auto &a,const auto &b){int ad=a.value("disc",1),bd=b.value("disc",1);return ad==bd?a.value("number",1)<b.value("number",1):ad<bd;});
}
}
