#pragma once
#include "json.hpp"
#include <string>
#include <cmath>
namespace nxh {
inline std::string misterSettingsCategory(const nlohmann::json &f) {
 const auto p=f.value("path",std::string{}),g=f.value("group",std::string{});
 if(p=="fps_target"||p=="gba_music_enabled"||p=="gapless_playback")return "Experimental";
 if(p=="confirm_on_exit"||p=="show_web_address"||p=="web_remote_enabled"||p=="web_remote_port")return "System";
 if(g=="Display")return "Appearance";
 if(g=="Controls and navigation")return "Controls";
 return "Playback";
}
inline nlohmann::json animatedRemoteSpectrum(double seconds,bool playing) {
 auto out=nlohmann::json::array();
 for(int i=0;i<32;++i)out.push_back(playing ? .12+.72*std::abs(std::sin(seconds*2.4+i*.39)*std::cos(seconds*.8-i*.16)) : 0.0);
 return out;
}
} // namespace nxh
