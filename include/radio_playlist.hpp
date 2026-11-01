#pragma once
#include "extended_audio.hpp"
#include <curl/curl.h>
#include <sstream>
#include <map>
namespace nxh {
inline std::string playlistTrim(std::string value){auto first=value.find_first_not_of(" \t\r\n"),last=value.find_last_not_of(" \t\r\n");return first==value.npos?"":value.substr(first,last-first+1);}
inline bool radioPlaylistURL(std::string url){url=lower(url.substr(0,url.find_first_of("?#")));return (url.size()>=4&&(url.substr(url.size()-4)==".pls"||url.substr(url.size()-4)==".m3u"))||(url.size()>=5&&url.substr(url.size()-5)==".m3u8");}
inline std::string radioPlaylistTarget(std::string body,const std::string &base,std::string &error){
 if(body.size()>65536){error="Radio playlist is too large.";return {};}
 if(body.compare(0,3,"\xef\xbb\xbf")==0)body.erase(0,3);
 if(body.find("#EXT-X-")!=std::string::npos){error="HLS radio playlists are not supported. Use a direct MP3/AAC stream.";return {};}
 std::istringstream input(body);std::string line;std::map<int,std::string> entries;std::vector<std::string> m3u;
 while(std::getline(input,line)){line=playlistTrim(line);if(line.empty()||line[0]=='#'||line[0]==';'||line[0]=='[')continue;auto eq=line.find('=');auto key=lower(playlistTrim(line.substr(0,eq)));
  if(eq!=line.npos&&key.rfind("file",0)==0){auto number=key.substr(4);if(!number.empty()&&number.size()<6&&number.find_first_not_of("0123456789")==number.npos)entries.emplace(std::stoi(number),playlistTrim(line.substr(eq+1)));}
  else if(eq==line.npos||line.rfind("http://",0)==0||line.rfind("https://",0)==0)m3u.push_back(line);
 }
 std::vector<std::string> candidates;for(auto &entry:entries)candidates.push_back(entry.second);if(entries.empty())candidates=std::move(m3u);
 for(auto &candidate:candidates){if(candidate.empty()||candidate.size()>4096||candidate.find_first_of("\r\n\t ")!=candidate.npos)continue;
  auto *url=curl_url();char *resolved=nullptr;std::string target;
  if(url&&curl_url_set(url,CURLUPART_URL,base.c_str(),0)==CURLUE_OK&&curl_url_set(url,CURLUPART_URL,candidate.c_str(),0)==CURLUE_OK&&curl_url_get(url,CURLUPART_URL,&resolved,0)==CURLUE_OK)target=resolved;
  curl_free(resolved);if(url)curl_url_cleanup(url);if(httpURL(target))return target;
 }
 error="Radio playlist contains no supported HTTP(S) stream URL.";return {};
}
}
