// Independent control uses the same native pages as local playback. No screen frames
// or injected host input are requested; all actions use the host's public API.
std::unique_ptr<App> nativeTarget;
std::unique_ptr<ClientSession> independent;
bool independentDisconnect=false;
nlohmann::json hostState=nlohmann::json::object();
std::map<uint64_t,std::function<void(const ClientSession::Result &)>> hostCallbacks;
uint64_t hostConfigRevision=~uint64_t(0),hostSourceRevision=~uint64_t(0);
uint64_t hostSnapshotSequence=0;bool hostCatalogSeen=false;
uint64_t hostBrowseToken=0;std::string hostArtworkKey,hostArtworkReadyKey;
struct HostArtwork{std::string key;std::shared_ptr<SDL_Surface> surface;std::vector<unsigned char> encoded;};
std::future<HostArtwork> hostArtworkWork;
std::string hostArtworkPendingKey;std::vector<unsigned char> hostArtworkPendingBytes;
void pumpHostArtwork(){
 if(hostArtworkWork.valid()&&hostArtworkWork.wait_for(std::chrono::seconds(0))==std::future_status::ready){
  auto image=hostArtworkWork.get();
  if(image.key==hostArtworkKey&&image.surface){
   auto *replacement=ui.coverPrepared(image.surface.get(),image.encoded);
   if(replacement){auto *old=artTexture;artTexture=replacement;if(old)ui.releaseCover(old);hostArtworkReadyKey=image.key;remoteDiagnostic("artwork-ready");}
  }
 }
 if(!hostArtworkWork.valid()&&!hostArtworkPendingBytes.empty()){
  auto key=std::move(hostArtworkPendingKey);auto bytes=std::move(hostArtworkPendingBytes);hostArtworkPendingBytes.clear();
  remoteDiagnostic("artwork-decode-worker");
  hostArtworkWork=std::async(std::launch::async,[key=std::move(key),bytes=std::move(bytes)]{
   auto *surface=boundedCoverSurface(bytes);
   if(surface&&std::max(surface->w,surface->h)>512){float ratio=512.f/std::max(surface->w,surface->h);auto *small=SDL_CreateRGBSurfaceWithFormat(0,std::max(1,int(surface->w*ratio)),std::max(1,int(surface->h*ratio)),32,SDL_PIXELFORMAT_RGBA32);if(!small){SDL_FreeSurface(surface);surface=nullptr;}else{SDL_SetSurfaceBlendMode(surface,SDL_BLENDMODE_NONE);if(SDL_BlitScaled(surface,nullptr,small,nullptr)<0){SDL_FreeSurface(small);small=nullptr;}SDL_FreeSurface(surface);surface=small;}}
   auto prepared=std::shared_ptr<SDL_Surface>(surface,SDL_FreeSurface);auto encoded=remoteCoverJpeg(surface);return HostArtwork{key,std::move(prepared),std::move(encoded)};
  });
 }
}
std::map<std::string,std::string> hostArtworkRequests;std::map<std::string,Uint64> hostArtworkRetry;
std::string hostArtPath(const std::string &key,int revision)const{return parent(settingsPath)+"/album-art/remote-"+std::to_string(std::hash<std::string>{}(independent->config.host+"/"+key))+"-"+std::to_string(revision)+".jpg";}
uint64_t hostSend(std::string method,std::string path,nlohmann::json body={},std::function<void(const ClientSession::Result&)> callback={}){if(!independent)return 0;auto id=independent->request(method,path,body);if(callback&&id)hostCallbacks[id]=std::move(callback);if(!id)status="Remote client is busy. Try again.";return id;}
void hostCommand(std::string op,nlohmann::json body=nlohmann::json::object()){body["op"]=op;hostSend("POST","/api/cmd",body,[this,op](const auto &reply){if(op=="play")remoteDiagnostic(reply.ok?"play-replied":"play-rejected");if(!reply.ok||reply.data.value("ok",true)==false)status=reply.error.empty()?reply.data.value("error",std::string("Remote action failed.")):reply.error;else status=reply.data.value("message",std::string{});});}
void hostMutate(std::string op,nlohmann::json body,std::function<void(nlohmann::json)> complete){body["op"]=op;body["catalog_revision"]=catalog["revision"];hostSend("POST","/api/cmd",body,[this,complete](const auto &reply){if(!reply.ok||!reply.data.value("ok",true)){status=reply.data.is_object()?reply.data.value("error",reply.error):reply.error;return;}auto result=reply.data;hostSend("GET","/api/state",{},[this,complete,result](const auto &state){if(state.ok){hostState=state.data;catalog=state.data.value("catalog",catalog);complete(result);}});});}
int hostSourceIndex(const Source &s)const{if(s.smb){for(int i=0;i<int(settings.shares.size());i++){const auto &v=settings.shares[i];if(v.server==s.share.server&&v.share==s.share.share&&v.user==s.share.user&&v.folder==s.root&&v.domain==s.share.domain&&v.guest==s.share.guest)return i;}}else{if(s.root==DiscRoot)return -2;for(const auto &d:storageDevices_)if(storageFolder(settings,d)==s.root||d.mount==s.root)return d.id;if(s.root==settings.musicRoot)return -1;}return -9999;}
void remoteDiagnostic(const char *stage,bool reset=false)const{
 auto path=parent(ConfigPath)+"/remote-debug.log";
 std::ofstream file(path,reset?std::ios::trunc:std::ios::app);
 if(file)file<<SDL_GetTicks64()<<" "<<stage<<'\n';
}
void beginNativeRemote(RemoteClientConfig config){
 remoteDiagnostic("connect-begin nx201-folder-2",true);
 nativeTarget=std::make_unique<App>(ui,renderer,player.local(),settings,"",parent(settingsPath)+"/remote-cache/config.json");auto &target=*nativeTarget;target.independent=std::make_unique<ClientSession>(config,false);target.player.independent=true;target.player.command=[&target](auto op,auto body){target.hostCommand(op,body);};target.player.playRemote=[&target](auto tracks,int index){target.remoteDiagnostic("play-enqueue");if(index<0||index>=int(tracks.size()))return;nlohmann::json paths=nlohmann::json::array();for(auto &t:tracks)paths.push_back(t.path);target.hostCommand("play",{{"source",target.hostSourceIndex(tracks[index].source)},{"tracks",paths},{"index",index},{"sources_revision",target.sourcesRevision}});};target.remoteAddress="http://"+config.host+":"+std::to_string(config.port);target.status="Connecting to "+config.name+"...";
}
void closeNativeRemote(){if(!nativeTarget)return;auto *client=nativeTarget->independent.release();if(client)client->stop();nativeTarget.reset();retiredClients.push_back(std::async(std::launch::async,[client]{delete client;}));setAppearance(settings.theme,settings.accent,settings.menuOpacity,settings.look);}
void hostBrowse(Source s,std::string path){source=std::move(s);sourceSelected=true;folder=path;loading=true;entries.clear();selected=scroll=0;page=1;uint64_t token=++hostBrowseToken;hostSend("POST","/api/browse",{{"source",hostSourceIndex(source)},{"path",path},{"sources_revision",sourcesRevision}},[this,token](const auto &reply){if(token!=hostBrowseToken)return;loading=false;if(!reply.ok||reply.data.value("ok",true)==false){status=reply.error.empty()?reply.data.value("error",std::string("Browse failed.")):reply.error;return;}folder=reply.data.value("path",folder);for(const auto &e:reply.data.value("entries",nlohmann::json::array()))entries.push_back({e.value("name",std::string{}),e.value("path",std::string{}),e.value("directory",false)});status.clear();});}
void hostFetchArtwork(std::string key,std::string url,std::string path){if(hostArtworkRequests[key]==url||SDL_GetTicks64()<hostArtworkRetry[key])return;hostArtworkRequests[key]=url;std::ifstream cached(path,std::ios::binary);if(cached&&cached.peek()!=std::ifstream::traits_type::eof())return;auto request=hostSend("GET",url,{},[this,key,path](const auto &reply){if(!reply.ok||reply.bytes.empty()){hostArtworkRequests.erase(key);hostArtworkRetry[key]=SDL_GetTicks64()+3000;return;}std::ofstream file(path,std::ios::binary);file.write(reply.bytes.data(),reply.bytes.size());file.close();if(!file){hostArtworkRequests.erase(key);hostArtworkRetry[key]=SDL_GetTicks64()+3000;return;}auto it=albumTextures.find(key);if(it!=albumTextures.end())it->second.first=0;});if(!request){hostArtworkRequests.erase(key);hostArtworkRetry[key]=SDL_GetTicks64()+1000;}}

void updateIndependent(){
 try{applyIndependentState();}
 catch(const std::exception &error){remoteDiagnostic("state-error");status=std::string("Remote state error: ")+error.what();independentDisconnect=true;}
}
void applyIndependentState(){
 pumpHostArtwork();
 ClientSession::Result reply;while(independent->poll(reply)){auto it=hostCallbacks.find(reply.id);if(it!=hostCallbacks.end()){auto callback=std::move(it->second);hostCallbacks.erase(it);callback(reply);}}
 nlohmann::json state;if(!independent->snapshotIfChanged(hostSnapshotSequence,state)){if(!independent->connected()&&!independent->error().empty())status=independent->error();sleeping=false;return;}if(state.contains("update")){if(hostState.empty())return;hostState.update(state["update"]);state=hostState;}if(!state.contains("settings")) {if(!independent->error().empty())status=independent->error();return;}hostState=state;
 bool changed=hostConfigRevision!=state.value("revision",uint64_t(0))||hostSourceRevision!=state.value("sources_revision",uint64_t(0));
 if(changed){remoteDiagnostic("state-settings");hostConfigRevision=state.value("revision",uint64_t(0));hostSourceRevision=state.value("sources_revision",uint64_t(0));
 auto doc=state["settings"];for(auto it=doc.begin();it!=doc.end();++it){std::string error;patchSettings(settings,{{it.key(),it.value()}},error);}settings.musicRoot=doc.value("music_root",settings.musicRoot);settings.storageFolders=doc.value("storage_folders",std::map<std::string,std::string>{});settings.shares.clear();for(auto &v:doc.value("shares",nlohmann::json::array())){Share s;s.name=v.value("name",std::string{});s.server=v.value("server",std::string{});s.share=v.value("share",std::string{});s.folder=v.value("folder",std::string{});s.user=v.value("username",v.value("user",std::string{}));s.domain=v.value("domain",std::string{});s.guest=v.value("guest",false);settings.shares.push_back(s);}
 sourcesRevision=state.value("sources_revision",uint64_t(0));revision=state.value("revision",uint64_t(0));storageDevices_.clear();for(auto &v:state.value("local_sources",nlohmann::json::array()))if(v.value("id",-1)!=-2){StorageDevice d{v.value("id",-1),v.value("name",std::string("Storage")),v.value("mount",settings.musicRoot)};storageDevices_.push_back(d);if(d.id!=-1)settings.storageFolders[d.mount]=v.value("folder",d.mount);}
 }auto incomingCatalog=state.find("catalog");if(incomingCatalog!=state.end()&&incomingCatalog->is_object()&&(!hostCatalogSeen||incomingCatalog->value("revision",uint64_t(0))>catalog.value("revision",uint64_t(0)))){catalog=*incomingCatalog;hostCatalogSeen=true;}albumViewCache.clear();
 discConnected=state.value("disc_connected",false);discSignature=state.value("disc_signature",std::string{});discEjecting=state.value("disc_ejecting",false);discEjectError=state.value("disc_eject_error",std::string{});
 auto p=state.value("playback",nlohmann::json::object());auto &play=player.remote;if(play.track.path!=p.value("track_path",std::string{}))remoteDiagnostic("track-changed");play.details.title=p.value("title",std::string{});play.details.artist=p.value("artist",std::string{});play.details.album=p.value("album",std::string{});play.position=p.value("position",0.);play.duration=p.value("duration",0.);play.index=p.value("index",0);play.count=p.value("count",0);play.playing=p.value("playing",false);play.paused=p.value("paused",false);play.busy=p.value("busy",false);play.error=p.value("error",std::string{});play.track.path=p.value("track_path",std::string{});play.track.name=p.value("live",false)?p.value("album",play.details.title):play.details.title;auto ref=p.value("source_ref",nlohmann::json::object());play.track.source={ref.value("smb",false),{},ref.value("root",std::string{})};play.track.source.share.server=ref.value("server",std::string{});play.track.source.share.share=ref.value("share",std::string{});play.track.source.share.user=ref.value("user",std::string{});if(p.value("live",false))play.track.source.root=RadioRoot;
 auto spectrum=p.value("spectrum",nlohmann::json::array());for(size_t i=0;i<play.spectrum.size();i++)play.spectrum[i]=i<spectrum.size()&&spectrum[i].is_number()?spectrum[i].template get<float>():0;
 auto key=p.value("track_key",std::string{})+":"+std::to_string(state.value("cover_revision",uint64_t(0)));if(state.value("has_artwork",false)&&hostArtworkKey!=key){hostArtworkKey=key;remoteDiagnostic("artwork-request");hostSend("GET","/api/art?r="+std::to_string(state.value("cover_revision",uint64_t(0))),{},[this,key](const auto &image){if(hostArtworkKey!=key||!image.ok)return;hostArtworkPendingKey=key;hostArtworkPendingBytes.assign(image.bytes.begin(),image.bytes.end());remoteDiagnostic("artwork-received");});}else if(!state.value("has_artwork",false)){if(artTexture){ui.releaseCover(artTexture);artTexture=nullptr;}hostArtworkKey.clear();hostArtworkReadyKey.clear();hostArtworkPendingBytes.clear();hostArtworkPendingKey.clear();}
 setAppearance(settings.theme,settings.accent,settings.menuOpacity,settings.look);selected=std::clamp(selected,0,maxSelection());if(!independent->connected())status=independent->error();else if(status.rfind("Connecting to ",0)==0)status.clear();sleeping=false;
}
