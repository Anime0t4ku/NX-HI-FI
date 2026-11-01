#pragma once
#include <SDL.h>
#include "core.hpp"
#include "json.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <future>
namespace nxh {
struct FrameDiagnostics {
    std::future<void> writer;
    const char *view="menu";
    std::string renderError;
    Uint64 flushTicks=0,presentTicks=0,first=0;
    unsigned frames=0;
    std::array<double,6> sum{},peak{};
    void record(SDL_Renderer *r,Uint64 start,Uint64 display,Uint64 input,Uint64 update,Uint64 draw){
        double factor=1000./SDL_GetPerformanceFrequency();
        double queued=double(draw-update)-double(flushTicks)-double(presentTicks);
        std::array<double,6> sample{{(display-start)*factor,(input-display)*factor,(update-input)*factor,
                                   std::max(0.,queued)*factor,flushTicks*factor,presentTicks*factor}};
        for(size_t i=0;i<sum.size();i++){sum[i]+=sample[i];peak[i]=std::max(peak[i],sample[i]);}
        frames++;Uint64 now=SDL_GetTicks64();if(!first)first=now;
        if(now-first<2000)return;
        int w=0,h=0;SDL_GetRendererOutputSize(r,&w,&h);
        nlohmann::json data={{"version",Version},{"view",view},{"width",w},{"height",h},{"fps",frames*1000./(now-first)}};
        SDL_RendererInfo info{};SDL_GetRendererInfo(r,&info);
        data["renderer"]=info.name?info.name:"unknown";data["gpu_accelerated"]=(info.flags&SDL_RENDERER_ACCELERATED)!=0;
        data["target_fps"]=60;
        if(!renderError.empty())data["render_error"]=renderError;
        const char *names[]={"display_poll_ms","input_ms","update_ms","draw_enqueue_ms","render_submit_ms","present_ms"};
        for(size_t i=0;i<sum.size();i++)data[names[i]]={{"average",sum[i]/frames},{"max",peak[i]}};
        if(!writer.valid()||writer.wait_for(std::chrono::milliseconds(0))==std::future_status::ready){
            if(writer.valid())writer.get();
            writer=std::async(std::launch::async,[data=std::move(data)]{
                const char *tmp="sdmc:/switch/nx-hifi/performance.json.tmp";
                {std::ofstream out(tmp);if(out)out<<data.dump(2)<<'\n';}
                std::rename(tmp,"sdmc:/switch/nx-hifi/performance.json");
            });
        }
        first=now;frames=0;sum.fill(0);peak.fill(0);
    }
};
inline FrameDiagnostics frameDiagnostics;
}
