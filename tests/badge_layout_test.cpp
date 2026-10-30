#include "ui.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
using namespace nxh;
int main(){
    SDL_setenv("SDL_VIDEODRIVER","dummy",1);assert(SDL_Init(SDL_INIT_VIDEO)==0);assert(TTF_Init()==0);
    const char *path=SDL_getenv("NX_HIFI_TEST_FONT");if(!path)path="/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
    std::ifstream f(path,std::ios::binary);std::vector<char> font((std::istreambuf_iterator<char>(f)),{});assert(!font.empty());
    setAppearance("dark","#EF4444");
    for(auto width:{1280,1920}){
        int height=width*9/16;auto *window=SDL_CreateWindow("Badge pixels",0,0,width,height,0);auto *renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);assert(renderer);
        {
            UI ui(renderer,font.data(),font.size());ui.setDocked(width==1920);
            for(int size:{12,16})for(auto label:{"FLAC","16-bit","44.1 kHz","WAV","MP3"}){
                ui.begin();ui.badge(100,100,200,40,label,size);SDL_RenderFlush(renderer);
                auto *pixels=SDL_CreateRGBSurfaceWithFormat(0,width,height,32,SDL_PIXELFORMAT_RGBA32);assert(pixels);
                assert(SDL_RenderReadPixels(renderer,nullptr,pixels->format->format,pixels->pixels,pixels->pitch)==0);
                int start=100*width/1280,end=300*width/1280,top=100*height/ui.layout().height(),bottom=140*height/ui.layout().height();
                int minX=end,maxX=-1;
                // Include antialiased glyph edges; exclude the badge's border itself.
                for(int y=top+2;y<bottom-2;y++)for(int x=start+3;x<end-3;x++){
                    auto *rgba=(Uint8*)pixels->pixels+y*pixels->pitch+x*4;
                    if(rgba[0]>60&&rgba[1]>60&&rgba[2]>60){minX=std::min(minX,x);maxX=std::max(maxX,x);}
                }
                assert(maxX>=minX);int left=minX-start,right=end-1-maxX;
                if(std::abs(left-right)>2){std::cerr<<width<<" "<<size<<" "<<label<<" margins "<<left<<"/"<<right<<"\n";assert(false);}
                SDL_FreeSurface(pixels);
            }
        }
        SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);
    }
    TTF_Quit();SDL_Quit();std::cout<<"Visible badge glyph margins centered at 720p and 1080p for mini/fullscreen font sizes.\n";
}
