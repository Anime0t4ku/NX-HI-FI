#pragma once
#include "player.hpp"
#include <functional>
namespace nxh {
// The native UI shares one transport API for local and independent remote playback.
class PlayerEndpoint {
 Player &local_;
public:
 using J=nlohmann::json;bool independent=false;Playback remote;
 std::function<void(std::string,J)> command;
 std::function<void(std::vector<Track>,int)> playRemote;
 explicit PlayerEndpoint(Player &p):local_(p){}
 Player &local(){return local_;}
 Playback snapshot(){return independent?remote:local_.snapshot();}
 void settings(const Settings &s){if(!independent)local_.settings(s);}
 void metadata(const TrackDetails &d,const Source &s,const std::string &p){if(!independent)local_.metadata(d,s,p);}
 void play(std::vector<Track> q,int i){if(independent){if(playRemote)playRemote(std::move(q),i);}else local_.play(std::move(q),i);}
 void stop(){if(independent)command("stop",J::object());else local_.stop();}
 void pause(){if(independent)command("playpause",J::object());else local_.pause();}
 void next(bool manual=true){if(independent)command("next",J::object());else local_.next(manual);}
 void previous(){if(independent)command("previous",J::object());else local_.previous();}
 void seek(double value){if(snapshot().track.source.root==RadioRoot)return;if(independent)command("seek",{{"seconds",value}});else local_.seek(value);}
 void tick(){if(!independent)local_.tick();}
 std::vector<Track> queue(){return independent?std::vector<Track>{}:local_.queue();}
};
}
