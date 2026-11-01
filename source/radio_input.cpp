#include "extended_audio.hpp"
#include "radio_playlist.hpp"
#include <set>
#include <curl/curl.h>
#include "platform.hpp"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <chrono>
namespace nxh {
bool httpURL(const std::string &s){return s.size()<4097&&(s.rfind("https://",0)==0||s.rfind("http://",0)==0)&&s.find_first_of("\r\n\t ")==std::string::npos;}
class RadioInput final:public Input {
 std::thread worker;mutable std::mutex mutex;std::condition_variable cv;std::deque<unsigned char> bytes;bool done=false;std::atomic<bool> cancel{false};
 const std::atomic<uint64_t> *generation;uint64_t expected,pos=0;std::string failure,title,metadata;size_t metaInterval=0,audioLeft=0,metaLeft=0;bool metaLength=false;
 bool stopped()const{return cancel||(generation&&*generation!=expected);}
 struct PlaylistBody{std::string text;bool tooLarge=false;};
 static size_t playlistWrite(char *p,size_t a,size_t b,void *opaque){auto &body=*static_cast<PlaylistBody*>(opaque);size_t n=a*b;if(n>65536-body.text.size()){body.tooLarge=true;return 0;}body.text.append(p,n);return n;}
 bool resolvePlaylist(std::string &url,std::string &error){std::set<std::string> visited;
  for(int depth=0;radioPlaylistURL(url);depth++){
   if(stopped())return false;if(depth>=4||!visited.insert(url).second){error="Radio playlist links are circular or nested too deeply.";return false;}
   auto *c=curl_easy_init();if(!c){error="Cannot open radio playlist.";return false;}PlaylistBody body;auto ca=std::string(assetRoot())+"cacert.pem";
   curl_easy_setopt(c,CURLOPT_CAINFO,ca.c_str());curl_easy_setopt(c,CURLOPT_URL,url.c_str());curl_easy_setopt(c,CURLOPT_PROTOCOLS,long(CURLPROTO_HTTP|CURLPROTO_HTTPS));curl_easy_setopt(c,CURLOPT_REDIR_PROTOCOLS,long(CURLPROTO_HTTP|CURLPROTO_HTTPS));curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,1L);curl_easy_setopt(c,CURLOPT_MAXREDIRS,5L);curl_easy_setopt(c,CURLOPT_FAILONERROR,1L);curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT,10L);curl_easy_setopt(c,CURLOPT_TIMEOUT,15L);curl_easy_setopt(c,CURLOPT_NOSIGNAL,1L);curl_easy_setopt(c,CURLOPT_USERAGENT,"NX-Hi-Fi/2.0.0");curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,playlistWrite);curl_easy_setopt(c,CURLOPT_WRITEDATA,&body);curl_easy_setopt(c,CURLOPT_XFERINFOFUNCTION,progress);curl_easy_setopt(c,CURLOPT_XFERINFODATA,this);curl_easy_setopt(c,CURLOPT_NOPROGRESS,0L);
   auto result=curl_easy_perform(c);char *effective=nullptr;curl_easy_getinfo(c,CURLINFO_EFFECTIVE_URL,&effective);std::string base=effective?effective:url;curl_easy_cleanup(c);
   if(result!=CURLE_OK){error=body.tooLarge?"Radio playlist is too large.":"Cannot load radio playlist: "+std::string(curl_easy_strerror(result));return false;}
   auto target=radioPlaylistTarget(body.text,base,error);if(target.empty())return false;url=std::move(target);
  }return !stopped();
 }
 static int progress(void *p,curl_off_t,curl_off_t,curl_off_t,curl_off_t){return static_cast<RadioInput*>(p)->stopped();}
 static size_t write(char *p,size_t a,size_t b,void *opaque){auto &s=*static_cast<RadioInput*>(opaque);size_t n=a*b;std::unique_lock<std::mutex> l(s.mutex);
 while(s.bytes.size()+n>512*1024&&!s.stopped())s.cv.wait_for(l,std::chrono::milliseconds(100));if(s.stopped())return 0;
 for(size_t i=0;i<n;i++){
 if(!s.metaInterval){s.bytes.push_back(p[i]);continue;}
 if(s.audioLeft){s.bytes.push_back(p[i]);if(!--s.audioLeft)s.metaLength=true;}
 else if(s.metaLength){s.metaLeft=(unsigned char)p[i]*16;s.metaLength=false;s.metadata.clear();if(!s.metaLeft)s.audioLeft=s.metaInterval;}
 else{ s.metadata+=p[i];if(!--s.metaLeft){auto begin=s.metadata.find("StreamTitle='");if(begin!=std::string::npos){begin+=13;auto end=s.metadata.find("';",begin);if(end!=std::string::npos)s.title=s.metadata.substr(begin,end-begin);}s.audioLeft=s.metaInterval;}}
 }s.cv.notify_all();return n;}
 static size_t header(char *p,size_t a,size_t b,void *opaque){auto &s=*static_cast<RadioInput*>(opaque);std::string value(p,a*b);auto key=lower(value);if(key.rfind("icy-metaint:",0)==0){std::lock_guard<std::mutex> l(s.mutex);try{s.metaInterval=std::stoul(value.substr(12));if(s.metaInterval>16*1024*1024)s.metaInterval=0;}catch(...){s.metaInterval=0;}s.audioLeft=s.metaInterval;}return a*b;}
public:
 RadioInput(std::string url,const std::atomic<uint64_t> *g,uint64_t e):generation(g),expected(e){worker=std::thread([this,url]{
 std::string resolved=url,playlistError;if(!resolvePlaylist(resolved,playlistError)){std::lock_guard<std::mutex> lock(mutex);if(!stopped())failure=playlistError;done=true;cv.notify_all();return;}
 auto *c=curl_easy_init();CURLcode result=CURLE_FAILED_INIT;if(c){std::string ca=std::string(assetRoot())+"cacert.pem";curl_easy_setopt(c,CURLOPT_CAINFO,ca.c_str());curl_easy_setopt(c,CURLOPT_URL,resolved.c_str());curl_easy_setopt(c,CURLOPT_PROTOCOLS,long(CURLPROTO_HTTP|CURLPROTO_HTTPS));curl_easy_setopt(c,CURLOPT_REDIR_PROTOCOLS,long(CURLPROTO_HTTP|CURLPROTO_HTTPS));curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,1L);curl_easy_setopt(c,CURLOPT_MAXREDIRS,5L);curl_easy_setopt(c,CURLOPT_FAILONERROR,1L);curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT,10L);curl_easy_setopt(c,CURLOPT_LOW_SPEED_LIMIT,1L);curl_easy_setopt(c,CURLOPT_LOW_SPEED_TIME,10L);curl_easy_setopt(c,CURLOPT_NOSIGNAL,1L);curl_easy_setopt(c,CURLOPT_USERAGENT,"NX-Hi-Fi/2.0.0");curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,write);curl_easy_setopt(c,CURLOPT_WRITEDATA,this);curl_easy_setopt(c,CURLOPT_XFERINFOFUNCTION,progress);curl_easy_setopt(c,CURLOPT_XFERINFODATA,this);curl_easy_setopt(c,CURLOPT_NOPROGRESS,0L);curl_easy_setopt(c,CURLOPT_HEADERFUNCTION,header);curl_easy_setopt(c,CURLOPT_HEADERDATA,this);auto *headers=curl_slist_append(nullptr,"Icy-MetaData: 1");curl_easy_setopt(c,CURLOPT_HTTPHEADER,headers);result=curl_easy_perform(c);curl_slist_free_all(headers);curl_easy_cleanup(c);}
 std::lock_guard<std::mutex> l(mutex);if(result!=CURLE_OK&&!stopped())failure="Radio connection interrupted: "+std::string(curl_easy_strerror(result));done=true;cv.notify_all();});}
 ~RadioInput(){cancel=true;cv.notify_all();if(worker.joinable())worker.join();}
 size_t read(void *dst,size_t n)override{std::unique_lock<std::mutex> l(mutex);while(bytes.empty()&&!done&&!stopped())cv.wait_for(l,std::chrono::milliseconds(100));n=std::min(n,bytes.size());auto *out=(unsigned char*)dst;for(size_t i=0;i<n;i++){out[i]=bytes.front();bytes.pop_front();}pos+=n;cv.notify_all();return n;}
 bool seek(int64_t,int)override{return false;}uint64_t size()const override{return 0;}uint64_t position()const override{return pos;}
 std::string streamTitle()const override{std::lock_guard<std::mutex> l(mutex);return title;}
 std::string error()const override{std::lock_guard<std::mutex> l(mutex);return failure;}
};
std::unique_ptr<Input> openRadio(const std::string &url,std::string &error,const std::atomic<uint64_t> *g,uint64_t e){if(!httpURL(url)){error="Enter an HTTP(S) audio stream or PLS/M3U playlist URL.";return {};}return std::make_unique<RadioInput>(url,g,e);}
}
