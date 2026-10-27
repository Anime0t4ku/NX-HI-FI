#include "mister_remote.hpp"
#include <cassert>
int main(){using namespace nxh;
 assert(misterSettingsCategory({{"path","fps_target"}})=="Experimental");
 assert(misterSettingsCategory({{"path","gba_music_enabled"}})=="Experimental");
 assert(misterSettingsCategory({{"path","accent_colour"},{"group","Display"}})=="Appearance");
 assert(misterSettingsCategory({{"path","show_web_address"},{"group","Display"}})=="System");
 assert(misterSettingsCategory({{"group","Controls and navigation"}})=="Controls");
 assert(misterSettingsCategory({{"group","ListenBrainz"}})=="Playback");
 auto a=animatedRemoteSpectrum(1,true),b=animatedRemoteSpectrum(2,true),silent=animatedRemoteSpectrum(1,false);
 assert(a.size()==32&&a!=b);for(auto &x:a)assert(x>=0&&x<=1);for(auto &x:silent)assert(x==0);
}
