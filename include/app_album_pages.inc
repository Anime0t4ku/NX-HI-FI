AlbumPages albumPages;
std::vector<std::string> openedCollectionAlbums;
std::string collectionAlbumID,discSignature;
bool discConnected=false;
int collectionTileSelected=0,collectionTileScroll=0,collectionRootSelected=0;
std::future<std::string> discProbe;
Uint64 discProbeAt=0;
const nlohmann::json *collectionAlbum(const std::string &id)const{for(auto &a:catalog["albums"])if(a["id"]==id)return &a;return nullptr;}
bool requestAlbumPage(const std::string &id,bool refresh=false){if(independent){hostCommand("album_open",{{"id",id},{"refresh",refresh}});return true;}auto a=collectionAlbum(id);if(!a){status="Album no longer exists.";return false;}if(std::find(openedCollectionAlbums.begin(),openedCollectionAlbums.end(),id)==openedCollectionAlbums.end()){openedCollectionAlbums.push_back(id);if(openedCollectionAlbums.size()>8)openedCollectionAlbums.erase(openedCollectionAlbums.begin());}lastPublish=0;if(refresh&&!a->value("busy",false))catalogCommand({{"op","album_refresh"},{"id",id}});return true;}
mutable std::map<std::string,nlohmann::json> albumViewCache;
mutable uint64_t albumViewRevision=~uint64_t(0),albumCatalogRevision=~uint64_t(0);
const nlohmann::json &albumPage(const std::string &key)const{if(movingItems&&movingKind=="playlist"&&key=="playlist:"+movingID){auto p=savedPlaylist(movingID);movingPage={{"title",p?p->value("name",std::string("Playlist")):"Playlist"},{"artist","Moving track"},{"tracks",movingRows},{"ready",true},{"art_revision",p?p->value("image_revision",0)+1:1}};return movingPage;}if(independent&&key.rfind("playlist:",0)!=0){auto &value=albumViewCache[key];value=key=="disc"?hostState.value("disc_view",nlohmann::json::object()):hostState.value("album_views",nlohmann::json::object()).value(key,nlohmann::json::object());return value;}
 auto viewRevision=albumPages.revision(),catalogRevision=catalog.value("revision",uint64_t(0));
 if(viewRevision!=albumViewRevision||catalogRevision!=albumCatalogRevision){albumViewCache.clear();albumViewRevision=viewRevision;albumCatalogRevision=catalogRevision;}
 auto it=albumViewCache.find(key);if(it!=albumViewCache.end())return it->second;
 auto make=[&]()->nlohmann::json{if(key.rfind("playlist:",0)==0){auto p=savedPlaylist(key.substr(9));if(!p)return nlohmann::json::object();double duration=0;for(auto &t:(*p)["tracks"])duration+=t.value("duration",0.);return {{"title",(*p)["name"]},{"artist","Playlist"},{"tracks",(*p)["tracks"]},{"ready",true},{"duration",duration},{"duration_complete",true},{"art_revision",p->value("image_revision",0)+1}};}auto data=albumPages.page(key);if(key!="disc")if(auto a=collectionAlbum(key)){data=*a;data["ready"]=!a->value("busy",false);data["error"]=a->value("available",true)?std::string{}:a->value("status",std::string{});data["art_revision"]=a->value("cover_revision",0);double duration=0;for(auto &t:data["tracks"]){t["title"]=t.value("title",t.value("name",std::string{}));duration+=t.value("duration",0.);}data["duration"]=duration;}if(key!="disc")if(auto a=collectionAlbum(key)){data["title"]=a->value("title",std::string{});data["artist"]=a->value("artist",std::string{});data["year"]=a->value("year",std::string{});if(a->value("cover_revision",0)>0)data["has_artwork"]=true;}return data;};
 return albumViewCache.emplace(key,make()).first->second;
}
std::vector<std::pair<std::string,int>> mainTabs()const{std::vector<std::pair<std::string,int>> tabs={{"Files",0},{"Collections",5},{"Playlists",9},{"Radio",6}};if(discConnected)tabs.push_back({"Disc",8});tabs.push_back({"Remote",4});tabs.push_back({"Settings",3});return tabs;}
int albumHeaderBottom()const{return 194+std::max(layout().x(132),156)+14;}
int albumTrackTop()const{return albumHeaderBottom()+12;}
int albumVisibleRows()const{return std::max(1,(155+layout().panelHeight()-albumTrackTop()-8)/46);}
void playAlbumPage(const std::string &key,int index=0,bool shuffle=false){if(independent){if(key=="disc")hostCommand("disc_play",{{"index",index},{"shuffle",shuffle}});else if(key.rfind("playlist:",0)==0)hostCommand("playlist_play",{{"id",key.substr(9)},{"index",index},{"shuffle",shuffle}});else hostCommand("album_track",{{"id",key},{"index",index},{"shuffle",shuffle}});return;}auto data=albumPage(key);auto rows=data.value("tracks",nlohmann::json::array());if(rows.empty()){status="No tracks available yet.";return;}Source source;
 if(key.rfind("playlist:",0)==0){std::vector<Track> queue;for(auto &row:rows){Source src;std::string error;if(!resolveReference(row["source"],settings,src,error)){status=error;return;}queue.push_back({src,row["path"],row["title"],key.substr(9)});}if(index<0||index>=int(queue.size()))return;if(shuffle){std::mt19937 engine{unsigned(SDL_GetTicks64())};std::shuffle(queue.begin(),queue.end(),engine);index=0;}player.play(std::move(queue),index);if(settings.autoFullscreen&&!fullscreen)toggleFullscreen();return;}
 if(key=="disc"){if(!discConnected||discSignature.empty()){status="Insert audio CD";return;}source={false,{},DiscRoot};}else{auto a=collectionAlbum(key);std::string error;if(!a||!resolveReference((*a)["source"],settings,source,error)){status=error.empty()?"Album unavailable.":error;return;}}
 std::vector<Track> tracks;for(auto &row:rows)tracks.push_back({source,row.at("path"),row.at("title")});if(index<0||index>=int(tracks.size()))return;if(shuffle){std::mt19937 engine{unsigned(SDL_GetTicks64())};std::shuffle(tracks.begin(),tracks.end(),engine);index=0;}player.play(std::move(tracks),index);if(settings.autoFullscreen&&!fullscreen)toggleFullscreen();}
void refreshDiscPage(){if(independent){hostCommand("disc_refresh");return;}albumPages.invalidate("disc");discSignature.clear();discProbeAt=0;}
std::future<std::pair<bool,std::string>> discEject;
bool discEjecting=false,discardDiscProbe=false;std::string discEjectError;
void startDiscEject(){if(independent){hostCommand("disc_eject");return;}if(discEjecting)return;if(player.snapshot().track.source.root==DiscRoot)player.stop();if(fullscreen){playerSettingsOpen=false;visualizerFullscreen=false;toggleFullscreen();}selectTab(8);discEjecting=true;discardDiscProbe=discProbe.valid();discEjectError.clear();status="Ejecting disc...";discEject=std::async(std::launch::async,[]{std::string error;bool ok=ejectDisc(error);return std::make_pair(ok,error);});}
void albumActions(){if(movingItems)return;bool disc=page==8,playlist=page==10;if(disc&&discSignature.empty())return;std::string key=currentAlbumKey();std::vector<nlohmann::json> values={0,1,2,3};std::vector<std::string> labels={disc?"Play disc":playlist?"Play playlist":"Play album",disc?"Shuffle disc":playlist?"Shuffle playlist":"Shuffle album",disc?"Refresh disc":playlist?"Manage playlist":"Manage album",disc?"Eject disc":playlist?"Manage selected track":"Add selected track to playlist"};choose(disc?"Disc actions":playlist?"Playlist actions":"Album actions",values,labels,0,[this,key,disc,playlist](auto value){int action=value.template get<int>();if(action<2)playAlbumPage(key,0,action==1);else if(action==3){if(disc)startDiscEject();else if(playlist)playlistTrackActions();else addSelectedAlbumTrackToPlaylist();}else if(disc)refreshDiscPage();else if(playlist){if(auto p=savedPlaylist(playlistID))managePlaylist(*p);}else if(auto album=collectionAlbum(key))manageAlbum(*album);});}
void albumPageAction(){if(page==8&&discSignature.empty())return;std::string key=currentAlbumKey();if(!key.empty())playAlbumPage(key,selected);}
void updateDiscPage(){
 if(discEjecting){if(discEject.valid()&&discEject.wait_for(std::chrono::milliseconds(0))==std::future_status::ready){auto result=discEject.get();discEjecting=false;discEjectError=result.second;if(result.first){discSignature.clear();albumPages.invalidate("disc");status="Insert audio CD";}else status=result.second;discProbeAt=0;}else return;}
 if(discardDiscProbe){if(discProbe.valid()){if(discProbe.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready)return;discProbe.get();}discardDiscProbe=false;}
 if(!discConnected)return;
 if(discProbe.valid()&&discProbe.wait_for(std::chrono::milliseconds(0))==std::future_status::ready){auto id=discProbe.get();if(id!=discSignature){discSignature=id;albumPages.invalidate("disc");if(!id.empty())albumPages.request("disc",Source{false,{},DiscRoot},"");}}
 if(!discProbe.valid()&&(!discProbeAt||SDL_GetTicks64()-discProbeAt>=2000)){discProbeAt=SDL_GetTicks64();discProbe=std::async(std::launch::async,[]{return currentDiscID();});}
}
void drawAlbumPage(const Playback &playing){ui.rect(28,155,828,layout().panelHeight(),PANEL);bool disc=page==8;std::string key=currentAlbumKey();const auto &data=albumPage(key);
 ui.text(46,160,disc?"DISC":page==10?"PLAYLIST":"COLLECTION ALBUM",15,MUTED,790);
 if(disc&&discSignature.empty()){ui.text(58,245,"Insert audio CD",24,MUTED,750);return;}
 if(data.empty()){ui.text(58,245,"Loading album...",22,MUTED,750);return;}
 int artHeight=layout().x(132);int revision=data.value("art_revision",0);if(!disc)if(auto a=collectionAlbum(key))revision+=a->value("cover_revision",0)*100000;
 if(independent){auto path=hostArtPath("album:"+key,revision);hostFetchArtwork("album:"+key,"/api/album-art/"+(page==10?"playlist-"+playlistID:key)+"?v="+std::to_string(revision),path);}
 auto texture=tileTexture("album:"+key,revision,(disc&&!independent)?std::string{}:page==10?collection->imagePath("playlist",playlistID):collection->coverPath(key),page==10?std::string{}:key);
 if(page==10)ensureTileImage("playlist",playlistID,revision);
 bool recording=ui.recordingScreen();ui.recordScreen(false);ui.image(texture,46,194,132);ui.recordScreen(recording);
 ui.text(196,195,data.value("title",std::string("Album")),24,TEXT,630);ui.text(196,235,data.value("artist",std::string{}),18,ACCENT,630);
 const auto &tracks=data.at("tracks");std::string info=data.value("year",std::string{});if(!info.empty())info+="  /  ";info+=std::to_string(tracks.size())+" tracks";double duration=data.value("duration",0.);if(duration>0)info+="  /  "+timeLabel(duration)+(data.value("duration_complete",false)?"":"+");if(data.value("busy",false))info+="  /  Reading metadata...";ui.text(196,267,info,14,MUTED,630);
 if(!data.value("error",std::string{}).empty()){ui.text(46,albumTrackTop(),data["error"],17,MUTED,780);return;}
 if(tracks.empty()){ui.text(46,albumTrackTop(),data.value("busy",false)?"Loading tracks...":"No supported tracks in this album.",18,MUTED,780);return;}
 UI::Clip list(ui,38,albumTrackTop()-3,808,155+layout().panelHeight()-albumTrackTop());
 int first=std::clamp(scroll,0,std::max(0,int(tracks.size())-albumVisibleRows()));ui.scrollIndicator(843,albumTrackTop(),albumVisibleRows()*46-3,int(tracks.size()),first,albumVisibleRows());for(int i=first;i<std::min(int(tracks.size()),first+albumVisibleRows());i++){
 auto &track=tracks[i];int y=albumTrackTop()+(i-first)*46;bool focus=!controlsFocused&&selected==i;
 bool current=playing.playing&&playing.track.path==track["path"]&&(disc?playing.track.source.root==DiscRoot:page==10?sourceReference(playing.track.source)==track["source"]:collectionAlbum(key)&&sourceReference(playing.track.source)==(*collectionAlbum(key))["source"]);
 if(focus)ui.roundedRect(42,y,800,43,FOCUS,7);
 ui.text(54,y+11,std::to_string(i+1),14,current?ACCENT:MUTED,34);std::string artist=track.value("artist",std::string{});if(!disc&&tracks.back().value("disc",1)>1)artist="Disc "+std::to_string(track.value("disc",1))+" · "+artist;bool different=!artist.empty()&&artist!=data.value("artist",std::string{});
 ui.text(96,y+(different?2:10),track.value("title",std::string{}),17,current||focus?ACCENT:TEXT,620);if(different)ui.text(96,y+25,artist,12,MUTED,620);
 double length=track.value("duration",0.);ui.rightText(824,y+12,length>0?timeLabel(length):"--:--",14,MUTED);
 }
}

nlohmann::json publishedAlbumViews()const{auto views=albumPages.snapshot();for(auto &id:openedCollectionAlbums)if(collectionAlbum(id))views[id]=albumPage(id);return views;}
