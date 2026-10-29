#include "player.hpp"
#include "disc.hpp"
#include "disc_input.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
using namespace nxh;
static std::atomic<bool> readFails{false};
static std::atomic<int> clearCalls{0};
extern "C" void __real_SDL_ClearQueuedAudio(SDL_AudioDeviceID);
extern "C" void __wrap_SDL_ClearQueuedAudio(SDL_AudioDeviceID d) { ++clearCalls;__real_SDL_ClearQueuedAudio(d); }
namespace nxh {
std::unique_ptr<Input> openInput(const Source &,const std::string &,std::string &,const std::atomic<uint64_t>*,uint64_t) { return {}; }
std::unique_ptr<Input> openDisc(const std::string &path,std::string &,const std::atomic<uint64_t>*g,uint64_t e) {
    int sectors=path=="long"?375:30;
    return std::make_unique<CDDAInput>([](int,int n,unsigned char*b){
        if(readFails)return false;
        for(int i=0;i<n*2352;i+=2){b[i]=0;b[i+1]=8;}
        return true;
    },DiscTrack{1,0,sectors},g,e);
}
TrackDetails discDetails(const std::string &path) {
    TrackDetails d;d.title=path;d.album="Test CD";d.format="CDDA";d.sampleRate=44100;d.bitDepth=16;d.channels=2;d.bitRate=1411;return d;
}
}
template<class F> bool until(F f,int ms=2500) {
    auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(ms);
    do {if(f())return true;SDL_Delay(5);}while(std::chrono::steady_clock::now()<end);
    return false;
}
int main() {
    SDL_setenv("SDL_AUDIODRIVER","dummy",1);assert(SDL_Init(SDL_INIT_AUDIO|SDL_INIT_TIMER)==0);
    {
        Settings s;s.gaplessPlayback=true;s.visualizer=false;
        Player player(s);assert(player.valid());
        Source cd{false,{},DiscRoot};
        player.play({{cd,"one","one"},{cd,"two","two"}},0);
        assert(until([&]{return player.snapshot().playing;}));
        assert(player.snapshot().index==0);
        int clears=clearCalls;
        assert(until([&]{return player.snapshot().index==1;}));
        assert(clearCalls==clears); // No SDL flush between gapless tracks.
        auto state=player.snapshot();assert(state.details.title=="two"&&state.position<.15);
        player.pause();SDL_Delay(40);double position=player.snapshot().position;SDL_Delay(80);
        assert(std::abs(player.snapshot().position-position)<.03);
        player.seek(.08);assert(until([&]{return clearCalls>clears;}));
        player.pause();assert(until([&]{return player.snapshot().ended;}));
        player.stop();assert(!player.snapshot().playing);
        // Non-gapless: EOF is exposed to tick(), which advances the queue normally.
        s.gaplessPlayback=false;player.settings(s);player.play({{cd,"one","one"},{cd,"two","two"}},0);
        assert(until([&]{return player.snapshot().ended;}));assert(player.snapshot().index==0);
        player.tick();assert(until([&]{return player.snapshot().playing;}));assert(player.snapshot().index==1);
        // Repeat-one keeps the PCM device running and loops the same queue index.
        player.stop();s.gaplessPlayback=true;s.repeat=2;player.settings(s);
        player.play({{cd,"one","one"}},0);assert(until([&]{return player.snapshot().playing;}));
        clears=clearCalls;SDL_Delay(950);state=player.snapshot();assert(state.playing&&!state.ended&&state.index==0&&clearCalls==clears);
        // Unplug/read errors are terminal, never silently advanced as EOF.
        player.stop();s.repeat=0;player.settings(s);player.play({{cd,"long","long"}},0);
        assert(until([&]{return player.snapshot().playing;}));readFails=true;
        assert(until([&]{return !player.snapshot().error.empty();}));
        state=player.snapshot();assert(!state.playing&&!state.busy&&!state.ended);
        readFails=false;
    }
    SDL_Quit();std::cout<<"CDDA player: gapless timing, pause/seek, normal advance, repeat and read errors passed\n";
}
