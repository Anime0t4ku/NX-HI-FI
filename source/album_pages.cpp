#include "album_pages.hpp"
#include "extended_audio.hpp"
#include "decoder.hpp"
#include "disc.hpp"
namespace nxh {
using J=nlohmann::json;
static ma_result albumRead(ma_decoder *d,void *out,size_t n,size_t *got){auto &input=*static_cast<Input*>(d->pUserData);*got=input.read(out,n);return *got?MA_SUCCESS:MA_AT_END;}
static ma_result albumSeek(ma_decoder *d,ma_int64 offset,ma_seek_origin origin){return static_cast<Input*>(d->pUserData)->seek(offset,origin==ma_seek_origin_start?SEEK_SET:origin==ma_seek_origin_current?SEEK_CUR:SEEK_END)?MA_SUCCESS:MA_BAD_SEEK;}
static TrackDetails summary(Input &input,const std::string &path){auto m=readTrackDetails(input,path);auto dot=path.find_last_of('.');auto ext=dot==path.npos?"":lower(path.substr(dot));
 if(ext==".m4a"){ExtendedAudio audio;std::string error;audio.open(input,false,m,error);}
 else if(m.duration<=0){auto decoder=std::make_unique<ma_decoder>();auto config=ma_decoder_config_init(ma_format_f32,2,44100);if(ma_decoder_init(albumRead,albumSeek,&input,&config,decoder.get())==MA_SUCCESS){ma_uint64 length=0;if(ma_decoder_get_length_in_pcm_frames(decoder.get(),&length)==MA_SUCCESS)m.duration=length/44100.;ma_decoder_uninit(decoder.get());}}
 return m;
}
AlbumPages::AlbumPages(){worker=std::thread(&AlbumPages::loop,this);}
AlbumPages::~AlbumPages(){{std::lock_guard<std::mutex> lock(mutex);quit=true;jobs.clear();}condition.notify_all();worker.join();}
void AlbumPages::request(std::string key,Source source,std::string path,bool refresh){std::lock_guard<std::mutex> lock(mutex);if(!refresh&&pages.count(key)&&pages[key].value("error",std::string{}).empty())return;
 ++revision_;auto token=++serial;generations[key]=token;pages[key]={{"busy",true},{"ready",false},{"tracks",J::array()},{"title",source.root==DiscRoot?"Audio CD":filename(path.empty()?source.root:path)},{"artist",""},{"duration",0.},{"duration_complete",false},{"error",""},{"art_revision",token*2},{"has_artwork",false}};
 jobs.erase(std::remove_if(jobs.begin(),jobs.end(),[&](const Job &job){return job.key==key;}),jobs.end());jobs.push_back({key,path,std::move(source),token});
 while(pages.size()>8){auto it=pages.begin();if(it->first==key)++it;generations.erase(it->first);covers.erase(it->first);pages.erase(it);}condition.notify_one();}
void AlbumPages::invalidate(const std::string &key){std::lock_guard<std::mutex> lock(mutex);++revision_;generations[key]=++serial;pages.erase(key);covers.erase(key);}
bool AlbumPages::cancelled(const Job &job){std::lock_guard<std::mutex> lock(mutex);return quit||!generations.count(job.key)||generations[job.key]!=job.generation;}
void AlbumPages::publish(const Job &job,J value){std::lock_guard<std::mutex> lock(mutex);if(!quit&&generations.count(job.key)&&generations[job.key]==job.generation){pages[job.key]=std::move(value);++revision_;}}
J AlbumPages::page(const std::string &key)const{std::lock_guard<std::mutex> lock(mutex);auto it=pages.find(key);return it==pages.end()?J::object():it->second;}
J AlbumPages::snapshot()const{std::lock_guard<std::mutex> lock(mutex);return pages;}
std::vector<unsigned char> AlbumPages::cover(const std::string &key)const{std::lock_guard<std::mutex> lock(mutex);auto it=covers.find(key);return it==covers.end()?std::vector<unsigned char>{}:it->second;}
void AlbumPages::loop(){for(;;){Job job;{std::unique_lock<std::mutex> lock(mutex);condition.wait(lock,[&]{return quit||!jobs.empty();});if(quit)return;job=std::move(jobs.front());jobs.pop_front();}try{load(job);}catch(const std::exception &e){auto value=page(job.key);value["busy"]=false;value["error"]=e.what();publish(job,std::move(value));}}}
void AlbumPages::load(const Job &job){std::vector<Entry> entries;std::string error;auto value=page(job.key);if(cancelled(job))return;
 if(!listDirectory(job.source,job.path,entries,error)){value["busy"]=false;value["error"]=error;publish(job,value);return;}
 J tracks=J::array();for(auto &entry:entries)if(!entry.directory&&audioFile(entry.path))tracks.push_back({{"path",entry.path},{"title",job.source.root==DiscRoot?entry.name:title(entry.name)},{"artist",""},{"duration",0.},{"number",int(tracks.size())+1}});
 value["tracks"]=tracks;value["ready"]=true;publish(job,value);double duration=0;bool complete=true;
 for(size_t i=0;i<tracks.size();i++){
  if(cancelled(job))return;auto path=tracks[i]["path"].get<std::string>();TrackDetails m;
  if(job.source.root==DiscRoot)m=discDetails(path);else{auto input=openInput(job.source,path,error);if(input)m=summary(*input,path);}
  if(!m.title.empty())value["tracks"][i]["title"]=m.title;value["tracks"][i]["artist"]=m.artist;value["tracks"][i]["duration"]=m.duration;
  if(i==0){if(!m.album.empty())value["title"]=m.album;value["artist"]=m.artist;auto bytes=loadCover(job.source,path,false);if(!bytes.empty()){std::lock_guard<std::mutex> lock(mutex);if(generations[job.key]==job.generation){covers[job.key]=std::move(bytes);value["has_artwork"]=true;value["art_revision"]=job.generation*2+1;}}}
  if(m.duration>0)duration+=m.duration;else complete=false;
  value["duration"]=duration;value["duration_complete"]=complete;if(i%8==0)publish(job,value);
 }
 value["busy"]=false;value["duration_complete"]=complete&&!tracks.empty();publish(job,std::move(value));
}
}
