#include "remote_view.hpp"
namespace nxh {
namespace {
using J=nlohmann::json;
J serverConfig(J values) {
 for(auto key:{"login","cancel_login","signed_in"})values.erase(key);
 return values;
}
J serverField(const char *key,const char *label,const char *kind="text") {
 return {{"key",key},{"label",label},{"kind",kind}};
}
}
void RemoteView::mediaServerForm(std::string kind) {
 formIndex_=-1;
 auto values=document_.value("config",Json::object());
 if(!values.contains("insecure"))values["insecure"]=false;
 if(!values.contains("max_track_mb"))values["max_track_mb"]=128;
 if(!values.contains("scrobbling"))values["scrobbling"]="server";
 values["login"]=document_.value("signed_in",false)?"Signed in - sign in again":"Sign in";
 values["cancel_login"]="Cancel pending Quick Connect";
 Json fields=Json::array({serverField("server","Server URL"),serverField("library","Music library ID (optional)"),serverField("insecure","Allow insecure TLS","bool"),serverField("max_track_mb","Maximum track size (MB)","number"),serverField("scrobbling","Scrobbling","choice"),serverField("login",kind=="jellyfin"?"Quick Connect":"Emby sign-in","action")});
 fields[3]["min"]=1;fields[3]["max"]=1024;
 fields[4]["choices"]=Json::array({"server","lastfm","listenbrainz","both","none"});fields[4]["labels"]=Json::array({"Server history","Last.fm","ListenBrainz","Both","Off"});
 fields[5]["server_kind"]=kind;
 if(kind=="jellyfin") {auto cancel=serverField("cancel_login","Cancel Quick Connect","action");cancel["server_kind"]="jellyfin-cancel";fields.push_back(cancel);}
 openForm(kind=="jellyfin"?"JELLYFIN":"EMBY",values,fields,
  [this,kind](bool){send("POST","/api/"+kind+"/config",serverConfig(form_),[this](auto &r){if(r.ok){formOpen_=false;documentKind_.clear();refreshSources();status_="Server settings saved.";}});},
  [this,kind]{send("POST","/api/settings/test",{{"kind",kind},{kind,serverConfig(form_)}},[this](auto &r){if(r.ok)status_=r.data.value("message",std::string("Connection successful."));});});
}
void RemoteView::mediaServerLogin(std::string kind) {
 if(kind=="jellyfin-cancel") {
  quickConnectPolling_=false;
  send("POST","/api/jellyfin/quickconnect",{{"action","cancel"}},[this](auto &r){if(r.ok)status_="Jellyfin sign-in cancelled.";});return;
 }
 if(kind!="jellyfin"&&kind!="emby")return;
 std::string user,password;
 if(kind=="emby") {
  if(!keyboard_("Emby username",user,false)||user.empty())return;
  if(!keyboard_("Emby password",password,true))return;
 }
 auto config=serverConfig(form_);
 send("POST","/api/"+kind+"/config",config,[this,kind,user,password](auto &r){
  if(!r.ok)return;
  if(kind=="jellyfin") {
   send("POST","/api/jellyfin/quickconnect",{{"action","start"}},[this](auto &r){if(r.ok){quickConnectPolling_=true;lastQuickPoll_=SDL_GetTicks64();status_="Jellyfin Quick Connect: "+r.data.value("code",std::string{})+" - approve in Jellyfin";}});
  } else {
   send("POST","/api/emby/signin",{{"user",user},{"password",password}},[this](auto &r){if(r.ok){status_="Emby signed in.";refreshSources();}});
  }
 });
}
} // namespace nxh
