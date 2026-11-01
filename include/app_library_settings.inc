std::string libraryViewLabel(const std::string &key)const{return "View: "+std::string(settingsDocument(settings)[key]=="grid"?"Grid":"List");}
void libraryViewChoice(std::string key){choose("View",{"list","grid"},{"List","Grid"},settingsDocument(settings)[key],[this,key](auto value){applySettings({{key,value}});selected=std::clamp(selected,0,maxSelection());scroll=0;});}
void emptyLibraryActions(std::string key){auto label=key=="collections_view"?"Create collection":key=="playlists_view"?"Create playlist":"Add radio station";choose("Manage",{0,1},{label,libraryViewLabel(key)},0,[this,key](auto value){if(value.template get<int>()==1)libraryViewChoice(key);else if(key=="collections_view")createLibrary();else if(key=="playlists_view")createPlaylist();else editStation();});}
void beginShareEdit(int index){editIndex=index;edit=index<0?Share{}:settings.shares[index];editing=true;editSourcesRevision=sourcesRevision;page=0;selected=scroll=0;}
void sourceActions(){
 bool valid=selected>=0&&selected<int(storageDevices_.size()+settings.shares.size());
 std::vector<nlohmann::json> values;std::vector<std::string> labels;
 if(valid){values.push_back(0);labels.push_back(selected<int(storageDevices_.size())?"Configure music folder":"Edit SMB share");}
 values.push_back(1);labels.push_back("Add SMB share");int index=selected;
 choose("Source actions",values,labels,valid?0:1,[this,index](auto v){if(v.template get<int>()==1)beginShareEdit(-1);else if(index<int(storageDevices_.size()))editStorageFolder(storageDevices_[index]);else beginShareEdit(index-int(storageDevices_.size()));});if(valid)optionSeparator=1;
}
void radioActions(){if(catalog["stations"].empty()){emptyLibraryActions("radio_view");return;}auto station=catalog["stations"][selected];choose("Manage radio station",{0,1,2,3,4},{"Edit station","Test station","Delete station","Add radio station",libraryViewLabel("radio_view")},0,[this,station](auto v){int n=v.template get<int>();if(n==4){libraryViewChoice("radio_view");return;}if(n==0)editStation(station);else if(n==1)catalogCommand({{"op","station_test"},{"id",station["id"]}});else if(n==2)choose("Delete this station?",{false,true},{"Cancel","Delete"},false,[this,station](auto yes){if(yes.template get<bool>())catalogCommand({{"op","station_remove"},{"id",station["id"]}});});else editStation();});optionSeparator=3;}

void librarySettings(std::string key){
 std::vector<nlohmann::json> values;std::vector<std::string> labels;
 bool shares=key=="manage_shares";const char *list=key=="manage_collections"?"libraries":key=="manage_playlists"?"playlists":"stations";
 if(!shares){values.push_back(-2);auto view=key=="manage_collections"?settings.collectionsView:key=="manage_playlists"?settings.playlistsView:settings.radioView;labels.push_back("View: "+std::string(view=="grid"?"Grid":"List"));}
 if(key=="manage_playlists"){values.push_back(-3);labels.push_back("Use playlist artwork as player art: "+std::string(settings.playlistArtwork?"On":"Off"));}
 int count=shares?int(settings.shares.size()):int(catalog[list].size());
 for(int i=0;i<count;i++){values.push_back(i);labels.push_back(shares?(settings.shares[i].name.empty()?settings.shares[i].server:settings.shares[i].name):catalog[list][i].value("name",std::string{}));}
 values.push_back(-1);labels.push_back(shares?"Add SMB share":key=="manage_collections"?"Create collection":key=="manage_playlists"?"Create playlist":"Add radio station");
 choose(shares?"SMB shares":key=="manage_collections"?"Collections":key=="manage_playlists"?"Playlists":"Online Radio",values,labels,values.front(),[this,key,shares](auto v){int n=v.template get<int>();if(n==-3){choose("Use playlist artwork as player art",{false,true},{"Off","On"},settings.playlistArtwork,[this,key](auto value){applySettings({{"playlist_artwork",value}});librarySettings(key);});return;}if(n==-2){auto viewKey=key=="manage_collections"?"collections_view":key=="manage_playlists"?"playlists_view":"radio_view";auto current=settingsDocument(settings)[viewKey];choose("View",{"list","grid"},{"List","Grid"},current,[this,key,viewKey](auto view){applySettings({{viewKey,view}});librarySettings(key);});return;}if(shares){beginShareEdit(n);return;}if(n==-1){if(key=="manage_collections")createLibrary();else if(key=="manage_playlists")createPlaylist();else editStation();return;}if(key=="manage_playlists")managePlaylist(catalog["playlists"][n]);else if(key=="manage_radio")editStation(catalog["stations"][n]);else manageLibrary(catalog["libraries"][n]);});optionSeparator=shares&&count==0?-1:int(labels.size())-1;
}
