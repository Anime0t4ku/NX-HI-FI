std::unique_ptr<CollectionStore> collection;
nlohmann::json catalog={{"revision",0},{"libraries",nlohmann::json::array()},{"albums",nlohmann::json::array()},{"stations",nlohmann::json::array()},{"playlists",nlohmann::json::array()}};
std::string libraryID,albumContextID;

Uint64 catalogAt=0;
std::map<std::string,std::pair<int,SDL_Texture*>> albumTextures;
mutable std::vector<nlohmann::json> libraryRowsCache;
mutable std::string libraryRowsID;
mutable uint64_t libraryRowsRevision=~uint64_t(0);
const std::vector<nlohmann::json> &libraryAlbums()const{
 auto revision=catalog.value("revision",uint64_t(0));
 if(libraryRowsID!=libraryID||libraryRowsRevision!=revision){libraryRowsID=libraryID;libraryRowsRevision=revision;libraryRowsCache.clear();
 for(const auto &a:catalog["albums"]){const auto &m=a["libraries"];if(std::find(m.begin(),m.end(),nlohmann::json(libraryID))!=m.end())libraryRowsCache.push_back(a);}}
 return libraryRowsCache;
}
// One bounded worker prepares visible covers. The renderer only uploads finished surfaces.
struct TileImage{std::string key;int revision;std::shared_ptr<SDL_Surface> surface;std::vector<unsigned char> encoded;};
std::future<TileImage> tileWork;
std::map<std::string,Uint64> tileUsed;
std::map<std::string,int> tileImageRequests;
void ensureTileImage(const std::string &type,const std::string &id,int revision){if(independent){hostFetchArtwork(type+"-"+id,"/api/album-art/"+type+"-"+id+"?v="+std::to_string(revision),hostArtPath(type+"-"+id,revision));return;}auto key=type+"-"+id;if(tileImageRequests[key]!=revision){tileImageRequests[key]=revision;collection->ensureImage(type,id);}}
SDL_Texture *tileTexture(const std::string &key,int revision,std::string path,std::string fallback={}){
 if(tileWork.valid()&&tileWork.wait_for(std::chrono::milliseconds(0))==std::future_status::ready){
  auto image=tileWork.get();auto &slot=albumTextures[image.key];if(slot.second)ui.releaseCover(slot.second);
  slot={image.revision,ui.coverPrepared(image.surface.get(),image.encoded)};
 }
 auto &slot=albumTextures[key];tileUsed[key]=SDL_GetTicks64();
 if(independent){path=hostArtPath(key,revision);fallback.clear();std::ifstream ready(path,std::ios::binary);if(!ready||ready.peek()==std::ifstream::traits_type::eof())return slot.second;}
 if(slot.first!=revision&&!tileWork.valid())tileWork=std::async(std::launch::async,[this,key,revision,path=std::move(path),fallback=std::move(fallback)]{
  std::vector<unsigned char> bytes;if(!path.empty()){std::ifstream file(path,std::ios::binary);bytes.assign(std::istreambuf_iterator<char>(file),{});}
  if(bytes.empty()&&!fallback.empty())bytes=albumPages.cover(fallback);
  auto surface=std::shared_ptr<SDL_Surface>(boundedCoverSurface(bytes),SDL_FreeSurface);
  return TileImage{key,revision,surface,remoteCoverJpeg(surface.get())};
 });
 if(albumTextures.size()>48){auto oldest=tileUsed.end();for(auto it=tileUsed.begin();it!=tileUsed.end();++it)if(it->first!=key&&(oldest==tileUsed.end()||it->second<oldest->second))oldest=it;
  if(oldest!=tileUsed.end()){auto found=albumTextures.find(oldest->first);if(found!=albumTextures.end()){if(found->second.second)ui.releaseCover(found->second.second);albumTextures.erase(found);}tileUsed.erase(oldest);}}
 return slot.second;
}

void refreshCatalog(){if(independent)return;if(!collection||catalog["revision"]==collection->revision())return;catalog=collection->snapshot();if(!playlistID.empty()&&!savedPlaylist(playlistID)){playlistID.clear();if(page==10){page=9;selected=scroll=0;}}selected=std::clamp(selected,0,maxSelection());}
bool catalogCommand(nlohmann::json request){if(independent){request["catalog_revision"]=catalog["revision"];auto op=request.at("op").get<std::string>();request.erase("op");hostCommand(op,request);return true;}refreshCatalog();request["catalog_revision"]=catalog["revision"];auto response=collection->command(request,settings);status=response.value("message",response.value("error",std::string{}));refreshCatalog();if(response.value("ok",false)&&request.value("op",std::string{})=="album_refresh")requestAlbumPage(request.at("id"),true);lastPublish=0;return response.value("ok",false);}
int albumSourceIndex()const{if(independent)return hostSourceIndex(source);if(source.smb){for(int i=0;i<int(settings.shares.size());i++)if(settings.shares[i].server==source.share.server&&settings.shares[i].share==source.share.share&&settings.shares[i].user==source.share.user)return i;}else for(auto &d:storageDevices_)if(storageFolder(settings,d)==source.root)return d.id;return -99999;}
void createLibrary(){std::string name;if(keyboard("Collection name",name))catalogCommand({{"op","library_save"},{"name",name}});}
void memberships(nlohmann::json request,std::shared_ptr<std::vector<std::string>> chosen={}){
 if(!chosen)chosen=std::make_shared<std::vector<std::string>>();
 std::vector<nlohmann::json> values={-1,-2};std::vector<std::string> labels={"Done","+ Create collection"};
 for(int i=0;i<int(catalog["libraries"].size());i++){auto &lib=catalog["libraries"][i];std::string id=lib["id"];values.push_back(i);labels.push_back((std::find(chosen->begin(),chosen->end(),id)!=chosen->end()?"[x] ":"[ ] ")+lib["name"].get<std::string>());}
 choose("Choose collections",values,labels,-1,[this,request,chosen](auto value)mutable{int row=value.template get<int>();if(row==-1){request["libraries"]=*chosen;catalogCommand(request);return;}if(row==-2){if(independent){std::string name;if(keyboard("Collection name",name))hostMutate("library_save",{{"name",name}},[this,request,chosen](auto){memberships(request,chosen);});return;}createLibrary();}else if(row<int(catalog["libraries"].size())){std::string id=catalog["libraries"][row]["id"];auto it=std::find(chosen->begin(),chosen->end(),id);if(it==chosen->end())chosen->push_back(id);else chosen->erase(it);}memberships(request,chosen);});
}
void addAlbum(){if(source.root==DiscRoot){status="Choose a folder album from storage or SMB.";return;}auto path=folder;if(selected>=0&&selected<int(entries.size())&&entries[selected].directory)path=entries[selected].path;memberships({{"op","album_add"},{"source",albumSourceIndex()},{"path",path}});}
void editStation(nlohmann::json station=nlohmann::json::object()){
 std::vector<nlohmann::json> values={0,1,2,3,4,5,6};std::vector<std::string> labels={"Name: "+station.value("name",std::string{}),"Stream URL: "+station.value("url",std::string{}),"Genre: "+station.value("genre",std::string{}),"Logo URL: "+station.value("logo",std::string{}),"Save station","Test saved station","Delete saved station"};
 choose("Radio station",values,labels,0,[this,station](auto value)mutable{int row=value.template get<int>();const char *keys[]={"name","url","genre","logo"};if(row<4){auto text=station.value(keys[row],std::string{});if(keyboard(keys[row],text))station[keys[row]]=text;editStation(station);return;}
 if(row==4){station["op"]="station_save";catalogCommand(station);}else if(station.contains("id")){if(row==5)catalogCommand({{"op","station_test"},{"id",station["id"]}});else choose("Delete this station?",{false,true},{"Cancel","Delete"},false,[this,station](auto yes){if(yes.template get<bool>())catalogCommand({{"op","station_remove"},{"id",station["id"]}});});}else status="Save the station first.";});
}
void playStation(const nlohmann::json &station,bool focusPlayer=true){if(independent){hostCommand("station_play",{{"id",station.at("id")}});if(focusPlayer&&!fullscreen)toggleFullscreen();return;}Source src{false,{},RadioRoot};src.share.folder=station.value("logo",std::string{});src.share.domain=collection->imagePath("station",station.at("id"));player.play({{src,station.at("url"),station.at("name")}},0);status.clear();if(focusPlayer&&!fullscreen)toggleFullscreen();}
bool switchRadioStation(int direction){if(independent){auto p=player.snapshot();if(p.track.source.root==RadioRoot){hostCommand(direction>0?"next":"previous");return true;}return false;}auto playback=player.snapshot();if(playback.track.source.root!=RadioRoot)return false;refreshCatalog();const auto &stations=catalog["stations"];if(stations.empty()){status="No saved radio stations.";return true;}int current=-1;for(int i=0;i<int(stations.size());i++)if(collection->imagePath("station",stations[i].at("id"))==playback.track.source.share.domain){current=i;break;}if(current<0)for(int i=0;i<int(stations.size());i++)if(stations[i].at("url")==playback.track.path){current=i;break;}if(stations.size()==1&&current==0)return true;int target=current<0?(direction>0?0:int(stations.size())-1):(current+direction+int(stations.size()))%int(stations.size());playStation(stations[target],false);return true;}
void nextTrack(){if(!switchRadioStation(1))player.next();}
void previousTrack(){if(!switchRadioStation(-1))player.previous();}

void openAlbum(const nlohmann::json &album){if(!requestAlbumPage(album.at("id")))return;if(page==5){collectionTileSelected=selected;collectionTileScroll=scroll;}collectionAlbumID=album.at("id");page=7;selected=scroll=0;}
void manageAlbum(nlohmann::json album){
 choose(album.value("title",std::string("Album")),{0,1,2,3,4,5,6},{"Open album","Collections...","Edit metadata","Search metadata","Fix match / artwork","Refresh folder","Remove album from collections"},0,[this,album](auto value)mutable{int row=value.template get<int>();std::string id=album["id"];
 if(row==0)openAlbum(album);
 else if(row==1){auto members=std::make_shared<std::vector<std::string>>(album["libraries"].template get<std::vector<std::string>>());memberships({{"op","album_members"},{"id",id}},members);}
 else if(row==2||row==3){std::string name=album["title"],artist=album["artist"],year=album.value("year",std::string{});if(!keyboard("Album title",name)||!keyboard("Artist",artist))return;if(row==2&&!keyboard("Year",year))return;catalogCommand({{"op",row==2?"album_edit":"album_search"},{"id",id},{"title",name},{"artist",artist},{"year",year}});}
 else if(row==4){std::vector<nlohmann::json> values;std::vector<std::string> labels;for(auto &m:album.value("matches",nlohmann::json::array())){values.push_back(m["id"]);labels.push_back(m.value("artist",std::string{})+" - "+m.value("title",std::string{})+" ("+m.value("year",std::string{})+")");}if(values.empty()){status="Use Search metadata first.";return;}choose("Choose album edition",values,labels,values.front(),[this,id](auto release){catalogCommand({{"op","album_match"},{"id",id},{"release",release}});});}
 else if(row==5)catalogCommand({{"op","album_refresh"},{"id",id}});
 else choose("Remove collection entry? Files are kept.",{false,true},{"Cancel","Remove"},false,[this,id](auto yes){if(yes.template get<bool>())catalogCommand({{"op","album_remove"},{"id",id}});});});
}
void collectionAction(){
 if(page==9){if(selected<int(catalog["playlists"].size()))openPlaylist(catalog["playlists"][selected]);else createPlaylist();return;}
 if(page==6){if(selected<int(catalog["stations"].size()))playStation(catalog["stations"][selected]);else editStation();return;}
 if(libraryID.empty()){if(selected<int(catalog["libraries"].size())){collectionRootSelected=selected;libraryID=catalog["libraries"][selected]["id"];selected=scroll=0;catalogCommand({{"op","library_check"},{"id",libraryID}});}else createLibrary();}
 else{auto rows=libraryAlbums();if(selected<int(rows.size()))openAlbum(rows[selected]);}
}
void manageLibrary(nlohmann::json lib){choose("Manage collection",{0,1,2,3},{"Rename collection","Delete collection (keep album files)","Create collection",libraryViewLabel("collections_view")},0,[this,lib](auto v){if(v.template get<int>()==3){libraryViewChoice("collections_view");return;}if(v.template get<int>()==2){createLibrary();return;}if(v.template get<int>()==0){std::string name=lib["name"];if(keyboard("Collection name",name))catalogCommand({{"op","library_save"},{"id",lib["id"]},{"name",name}});}else choose("Delete this collection?",{false,true},{"Cancel","Delete"},false,[this,lib](auto yes){if(yes.template get<bool>())catalogCommand({{"op","library_remove"},{"id",lib["id"]}});});});optionSeparator=2;}
void collectionManage(){if(page==9){if(selected<int(catalog["playlists"].size()))managePlaylist(catalog["playlists"][selected]);else emptyLibraryActions("playlists_view");return;}if(page==6){radioActions();return;}if(!libraryID.empty()){auto rows=libraryAlbums();if(selected<int(rows.size()))manageAlbum(rows[selected]);return;}if(selected>=int(catalog["libraries"].size())){emptyLibraryActions("collections_view");return;}manageLibrary(catalog["libraries"][selected]);}
int albumTileHeight()const{return layout().x(184)+64;}
bool collectionGrid()const{return page==5?(!libraryID.empty()||settings.collectionsView=="grid"):page==9?settings.playlistsView=="grid":page==6&&settings.radioView=="grid";}
int collectionPageSize()const{return std::max(1,(layout().panelHeight()-55)/albumTileHeight())*4;}
int collectionFirst()const{return selected/collectionPageSize()*collectionPageSize();}
void collectionVertical(int direction){int count=maxSelection()+1,capacity=collectionPageSize(),first=collectionFirst(),col=selected%4,target=selected+direction*4;
 if(direction>0&&target>=std::min(count,first+capacity)){if(first+capacity<count)target=std::min(count-1,first+capacity+col);else target=settings.wrapNavigation?std::min(count-1,col):selected;}
 if(direction<0&&target<first){if(first>0)target=first-4+col;else target=settings.wrapNavigation?std::min(count-1,((count-1)/4)*4+col):selected;}
 selected=std::clamp(target,0,count-1);}
mutable nlohmann::json collectionRowsCache;
mutable uint64_t collectionRowsRevision=~uint64_t(0);mutable std::string collectionRowsID;
const nlohmann::json &collectionRows(bool albums)const{
 if(!albums)return page==9?catalog["playlists"]:page==6?catalog["stations"]:catalog["libraries"];
 auto revision=catalog.value("revision",uint64_t(0));if(collectionRowsRevision!=revision||collectionRowsID!=libraryID){collectionRowsCache=libraryAlbums();collectionRowsID=libraryID;collectionRowsRevision=revision;}return collectionRowsCache;
}
void drawCollections(){
 ui.rect(28,155,828,layout().panelHeight(),PANEL);std::string heading=page==9?"PLAYLISTS":page==6?"ONLINE RADIO":"COLLECTIONS";if(page==5)for(auto &lib:catalog["libraries"])if(lib["id"]==libraryID)heading=lib["name"];ui.text(46,160,heading,15,MUTED,600);
 bool albums=page==5&&!libraryID.empty();const auto &rows=collectionRows(albums);int count=int(rows.size())+(!albums&&rows.empty()?1:0);
 if(!collectionGrid()){scroll=std::clamp(scroll,std::max(0,selected-layout().rows()+1),selected);scroll=std::min(scroll,std::max(0,count-layout().rows()));int first=scroll;ui.scrollIndicator(849,190,layout().rows()*48-4,count,first,layout().rows());for(int i=first;i<std::min(count,first+layout().rows());i++)ui.button(40,190+(i-first)*48,804,44,i<int(rows.size())?rows[i].value("name",std::string{}):page==9?"+ Create playlist":page==6?"+ Add radio station":"+ Create collection",!controlsFocused&&selected==i);return;}
 int visible=collectionPageSize(),first=collectionFirst(),pages=std::max(1,(count+visible-1)/visible);if(pages>1)ui.rightText(832,160,"Page "+std::to_string(first/visible+1)+" / "+std::to_string(pages),14,MUTED);

 for(int i=first;i<std::min(count,first+visible);i++){int x=44+(i%4)*202,y=190+(i-first)/4*albumTileHeight();bool add=i>=int(rows.size());ui.roundedRect(x-2,y-2,188,albumTileHeight()-12,!controlsFocused&&selected==i?FOCUS:PANEL,10);
 std::string title=add?(page==9?"+ Create playlist":page==6?"+ Add radio station":"+ Create collection"):rows[i].value(albums?"title":"name",std::string{}),subtitle;SDL_Texture *texture=nullptr;
 if(!add){auto &a=rows[i];std::string id=a["id"],type=page==9?"playlist":page==6?"station":"collection",key=albums?id:type+"-"+id;int revision=a.value(albums?"cover_revision":"image_revision",0)+1;if(!albums)ensureTileImage(type,id,revision);else if(independent)hostFetchArtwork(id,"/api/album-art/"+id+"?v="+std::to_string(revision),hostArtPath(id,revision));texture=tileTexture(key,revision,albums?collection->coverPath(id):collection->imagePath(type,id));subtitle=albums?(a.value("busy",false)?"Loading...":a.value("available",true)?a.value("artist",std::string{}):"Unavailable"):page==9?std::to_string(a["tracks"].size())+" tracks":page==6?a.value("genre",std::string{}):"Collection";}
 bool recording=ui.recordingScreen();ui.recordScreen(false);ui.image(texture,x,y,184);ui.recordScreen(recording);UI::Clip caption(ui,x+3,y+layout().x(184)+4,178,46);ui.text(x+3,y+layout().x(184)+6,title,15,TEXT,176);ui.text(x+3,y+layout().x(184)+29,subtitle,12,MUTED,176);
 }
 if(rows.empty()&&albums)ui.text(46,215,"Add an album folder from Files using X.",18,MUTED,780);
}

mutable uint64_t playbackAlbumRevision=~uint64_t(0);
mutable std::string playbackAlbumPath;mutable nlohmann::json playbackAlbumSource;
// Own the cached metadata: remote snapshots can replace the catalogue
// without advancing its revision, invalidating pointers into the old JSON.
mutable nlohmann::json playbackAlbumCache=nullptr;
const nlohmann::json *playbackAlbum(const Playback &p)const{
 if(p.track.path.empty()||p.track.source.root==RadioRoot)return nullptr;
 auto ref=sourceReference(p.track.source);auto revision=catalog.value("revision",uint64_t(0));
 if(revision!=playbackAlbumRevision||p.track.path!=playbackAlbumPath||ref!=playbackAlbumSource){
  playbackAlbumRevision=revision;playbackAlbumPath=p.track.path;playbackAlbumSource=std::move(ref);playbackAlbumCache=nullptr;
  auto path=parent(p.track.path);for(const auto &a:catalog["albums"])if(a["source"]==playbackAlbumSource&&a["path"]==path){playbackAlbumCache=a;break;}
 }
 return playbackAlbumCache.is_object()?&playbackAlbumCache:nullptr;
}
Playback libraryPlayback(Playback p)const{if(auto a=playbackAlbum(p)){p.details.album=a->value("title",p.details.album);if(p.details.artist.empty())p.details.artist=a->value("artist",std::string{});}return p;}
