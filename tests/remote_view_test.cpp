#include "remote_view.hpp"
#include "remote.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <chrono>
#include <thread>
#include <mutex>
using namespace nxh;
namespace nxh {
struct RemoteViewTest {
 static bool text(UI &ui,const std::string &label){auto frame=ui.screenFrame();for(auto &op:frame["ops"])if(op.is_array()&&op.size()>3&&op[0]==1&&op[3]==label)return true;return false;}
 static void run(UI &ui,SDL_Renderer *renderer){
  using J=nlohmann::json;Settings settings;settings.remoteEnabled=true;
  std::mutex mutex;std::vector<J> commands;
  RemoteServer server(0,[&](const J &j){std::lock_guard<std::mutex> lock(mutex);commands.push_back(j);return J{{"ok",true}};});assert(server.valid());
  auto identity=remoteIdentity();identity["app_id"]="prospero-hifi";identity["name"]="Prospero Hi-Fi";identity["platform"]="ps5";
  J playback={{"title","Opening"},{"artist","Namco Sounds"},{"album","Dragon Ball FighterZ soundtrack"},{"badges",{"FLAC","24 bit","44.1 kHz","1755 kbps"}},{"position",29.},{"duration",129.},{"count",39},{"index",0},{"playing",true},{"paused",false},{"spectrum",std::vector<float>(32,1.f)}};
  J fixture={{"identity",identity},{"version","1.2.0"},{"revision",7},{"sources_revision",9},{"settings",settingsDocument(settings)},{"fields",settingFields()},{"playback",playback},{"local_sources",J::array({{{"id",-1},{"name","Internal Storage"},{"folder","/data/music"}},{{"id",-3},{"name","M.2 Storage"},{"folder","/mnt/ext1/Music"},{"mount","/mnt/ext1"}},{{"id",-10},{"name","USB Storage 1"},{"folder","/mnt/usb0/Music"},{"mount","/mnt/usb0"}},{{"id",-2},{"name","Physical Disc"}}})}};
  fixture["settings"]["shares"]=J::array({{{"name","NAS"},{"server","host"},{"share","Music"}}});server.publish(fixture,settings,9);
  RemoteView view(ui,renderer,{"PS5","127.0.0.1","nx",server.port()},[](const std::string &,std::string &value,bool){value="/mnt/ext1/Albums";return true;},{});
  auto await=[&](auto predicate){auto end=SDL_GetTicks64()+5000;while(!predicate()){assert(SDL_GetTicks64()<end);view.update();SDL_Delay(5);}};
  await([&]{return view.ready();});assert(view.hostType_=="prospero"&&view.hostVersion_=="1.2.0");assert(view.sources_.size()==5&&view.sources_[1]["id"]==-3&&view.sources_[4]["id"]==0);
  auto last=[&](){std::lock_guard<std::mutex> lock(mutex);return commands.empty()?J::object():commands.back();};
  auto count=[&](){std::lock_guard<std::mutex> lock(mutex);return commands.size();};
  auto press=[&](unsigned mask){view.buttons(mask,mask);view.buttons(0,0);};
  view.selected_=1;press(RemoteView::X);await([&]{return last().value("op",std::string{})=="settings";});assert(last()["patch"]["storage_folders"]["/mnt/ext1"]=="/mnt/ext1/Albums");assert(last()["revision"]==7);
  view.selected_=4;press(RemoteView::X);assert(view.formOpen_&&view.formIndex_==0);view.back();
  view.changePage(3);auto category=std::find(view.categories_.begin(),view.categories_.end(),"Listening Mode")-view.categories_.begin();view.selected_=category;view.groupFields();view.action();assert(view.settingsCategory_==int(category));
  bool hostMode=false;for(int i=0;i<int(view.fields_.size());i++)if(view.fields_[i]["key"]=="@host_listening"){view.selected_=i;view.action();hostMode=true;break;}assert(hostMode);await([&]{return last().value("op",std::string{})=="listening_mode";});assert(last()["enabled"]==true);
  view.back();assert(view.settingsCategory_==-1&&view.selected_==int(category));
  press(RemoteView::Full);assert(view.fullscreen_&&view.focus().action(true)==1);int page=view.page_;
  for(auto [button,op]:{std::pair<unsigned,const char *>{RemoteView::L,"previous"},{RemoteView::R,"next"},{RemoteView::ZL,"seek"},{RemoteView::ZR,"seek"}}){auto before=count();press(button);await([&]{return count()>before;});assert(last()["op"]==op);if(button==RemoteView::ZL)assert(last()["seconds"]==-5);if(button==RemoteView::ZR)assert(last()["seconds"]==5);assert(view.page_==page&&view.fullscreen_);}
  press(RemoteView::Listen);assert(view.listening_);ui.recordScreen(true);view.draw();assert(!text(ui,"NX Hi-Fi")&&!text(ui,"Volume")&&!text(ui,"FLAC"));
  auto before=count();press(RemoteView::Plus);await([&]{return count()>before;});assert(view.listening_&&last()["op"]=="playpause");
  before=count();press(RemoteView::A);assert(!view.listening_&&view.fullscreen_);SDL_Delay(30);assert(count()==before);
  ui.recordScreen(true);view.draw();assert(text(ui,"FLAC")&&text(ui,"Volume"));
  press(RemoteView::B);assert(!view.fullscreen_);
  for(bool docked:{false,true}){ui.setDocked(docked);for(bool full:{false,true}){if(view.fullscreen_!=full)view.toggleFullscreen();ui.recordScreen(true);view.draw();assert(text(ui,"FLAC")&&text(ui,"44.1 kHz"));
    if(docked&&std::getenv("NX_HIFI_PREVIEW_DIR")){auto *surface=SDL_CreateRGBSurfaceWithFormat(0,1920,1080,32,SDL_PIXELFORMAT_RGBA32);assert(surface);assert(SDL_RenderReadPixels(renderer,nullptr,surface->format->format,surface->pixels,surface->pitch)==0);std::string file=std::string(std::getenv("NX_HIFI_PREVIEW_DIR"))+(full?"/remote-full.png":"/remote-mini.png");assert(IMG_SavePNG(surface,file.c_str())==0);SDL_FreeSurface(surface);}
  }}
  view.raw_["identity"]["capabilities"]["source_management"]=false;view.normalize();assert(!view.canManageSources());
  auto legacy=fixture;legacy.erase("identity");legacy.erase("local_sources");legacy["physical_disc"]=true;view.hostType_="nx";view.raw_=legacy;view.normalize();assert(view.sources_[0]["name"]=="SD card"&&view.sources_.size()==3);for(const auto &field:view.allFields_)assert(field["key"]!="@host_listening");
  view.mister_=true;view.hostType_="mister";view.raw_={{"state","playing"},{"duration",10},{"volume",50},{"format","FLAC"}};view.appDoc_={{"config",{{"eq",{{"enabled",true}}}}},{"fields",J::array({{{"path","eq.enabled"},{"label","Equalizer"},{"type","bool"}}})}};view.normalize();assert(view.protocolReady_&&view.settings_["volume"]==50&&!view.categories_.empty());assert(!view.allFields_.empty());
  std::cout<<"Independent remote: Prospero identity, dynamic source IDs, M.2 folder patches, SMB indices, categorized host settings/mode, isolated player shortcuts, Listening Mode, legacy NX and MiSTer normalization passed.\n";
 }
};
}
int main(){SDL_setenv("SDL_VIDEODRIVER","dummy",1);SDL_setenv("SDL_AUDIODRIVER","dummy",1);assert(SDL_Init(SDL_INIT_VIDEO)==0&&TTF_Init()==0);auto *window=SDL_CreateWindow("Remote tests",0,0,1920,1080,0);auto *renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);assert(renderer);std::ifstream f("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",std::ios::binary);std::vector<char> font((std::istreambuf_iterator<char>(f)),{});{UI ui(renderer,font.data(),font.size());RemoteViewTest::run(ui,renderer);}SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);TTF_Quit();SDL_Quit();}
