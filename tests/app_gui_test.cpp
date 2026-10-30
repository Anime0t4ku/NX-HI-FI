#include "host_switch.hpp"
#define NXH_APP_TEST
#include "../source/main.cpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
namespace fs=std::filesystem;
static bool hasText(UI &ui,const std::string &text){
    auto frame=ui.screenFrame();for(auto &op:frame["ops"])if(op.is_array()&&op.size()>3&&op[0]==1&&op[3].is_string()&&op[3].get<std::string>().find(text)!=std::string::npos)return true;return false;
}
static int textCount(UI &ui,const std::string &text){int count=0;auto frame=ui.screenFrame();for(auto &op:frame["ops"])if(op.is_array()&&op.size()>3&&op[0]==1&&op[3]==text)++count;return count;}
static void spectrumSeparation(UI &ui,bool full,bool art) {
    const auto &l=ui.layout();
    const auto band=playerVisualizerRect(full,l.extraHeight(),art?560:64,art?656:1152);
    const SDL_Rect bounds{l.x(band.x),band.y,l.x(band.x+band.w)-l.x(band.x),band.h};
    std::vector<SDL_Rect> bars,metadata;
    int badges=0;
    const double scale=l.docked?1.25:1.;
    const auto frame=ui.screenFrame();
    for(const auto &op:frame["ops"]) {
        if(op[0]==0&&op.size()==9&&op[5]==ACCENT.r&&op[6]==ACCENT.g&&op[7]==ACCENT.b) {
            SDL_Rect box{op[1],op[2],op[3],op[4]};
            if(box.x>=bounds.x&&box.x<bounds.x+bounds.w&&box.y>=bounds.y&&box.y<bounds.y+bounds.h) {
                assert(box.y+box.h<=bounds.y+bounds.h);bars.push_back(box);
            }
        }
        if(op[0]==1&&op.size()>4&&op[3].is_string()) {
            const std::string text=op[3];
            if(text=="Opening"||text=="Namco Sounds"||text.find("Dragon Ball FighterZ")==0||text=="1 / 39") {
                int w=0,h=0;TTF_SizeUTF8(ui.font(op[4]),text.c_str(),&w,&h);
                metadata.push_back({op[1],op[2],int(std::ceil(w/scale)),int(std::ceil(h/scale))});
            }
            if(text=="FLAC"||text=="24 bit"||text=="44.1 kHz"||text=="1755 kbps") {
                ++badges;int w=0,h=0;TTF_SizeUTF8(ui.font(op[4]),text.c_str(),&w,&h);
                metadata.push_back({op[1],op[2],int(std::ceil(w/scale)),int(std::ceil(h/scale))});
            }
        }
    }
    assert(bars.size()==32&&badges==4);
    for(const auto &bar:bars)for(const auto &text:metadata)assert(!SDL_HasIntersection(&bar,&text));
}
static void durationEdge(UI &ui,const std::string &value,int right,int size){
    int width=0;TTF_SizeUTF8(ui.font(size),value.c_str(),&width,nullptr);
    double scale=ui.layout().docked?1.25:1.;int logicalWidth=int(std::ceil(width/scale));bool found=false;
    auto frame=ui.screenFrame();for(auto &op:frame["ops"])if(op.is_array()&&op.size()>3&&op[0]==1&&op[3]==value){assert(op[1].get<int>()+logicalWidth==ui.layout().x(right));found=true;}assert(found);
}
static void saveFrame(SDL_Renderer *r,const std::string &name,int w,int h){
    const char *dir=SDL_getenv("NX_HIFI_PREVIEW_DIR");if(!dir)return;fs::create_directories(dir);
    auto *s=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_RGBA32);assert(s);
    assert(SDL_RenderReadPixels(r,nullptr,s->format->format,s->pixels,s->pitch)==0);
    assert(IMG_SavePNG(s,(std::string(dir)+"/"+name+".png").c_str())==0);SDL_FreeSurface(s);
}
int main(){
    SDL_setenv("SDL_VIDEODRIVER","dummy",1);SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO)==0&&TTF_Init()==0);
    const char *fontPath=SDL_getenv("NX_HIFI_TEST_FONT");if(!fontPath)fontPath="/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
    std::ifstream fontFile(fontPath,std::ios::binary);std::vector<char> font((std::istreambuf_iterator<char>(fontFile)),{});assert(!font.empty());
    char directory[]="/tmp/nx-hifi-gui-XXXXXX";assert(mkdtemp(directory));std::string root=directory;
    {std::ofstream wav(root+"/Track 1.wav",std::ios::binary);auto le=[&](unsigned n,int count){for(int i=0;i<count;i++)wav.put(char(n>>(i*8)));};
     constexpr int bytes=176400*5;wav.write("RIFF",4);le(36+bytes,4);wav.write("WAVEfmt ",8);le(16,4);le(1,2);le(2,2);le(44100,4);le(176400,4);le(4,2);le(16,2);wav.write("data",4);le(bytes,4);std::string silence(bytes,'\0');wav.write(silence.data(),silence.size());}
    for(int i:{2,10})fs::copy_file(root+"/Track 1.wav",root+"/Track "+std::to_string(i)+".wav");
    for(bool docked:{false,true})for(bool automatic:{false,true}){
        Layout layout{docked};auto *window=SDL_CreateWindow("NX GUI test",0,0,layout.outputWidth(),layout.outputHeight(),0);
        auto *renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);assert(renderer);
        {
            UI ui(renderer,font.data(),font.size());ui.setDocked(docked);
            for(int size:{14,18})for(auto value:{"--:--","1:23","~12:34","123:45:56"}){
                ui.recordScreen(true);ui.begin();ui.rightText(1228,100,value,size,MUTED);
                durationEdge(ui,value,1228,size);
            }
            Settings settings;settings.musicRoot=root;settings.autoFullscreen=automatic;settings.visualizer=false;
            settings.remoteClients.push_back({"Saved client","127.0.0.1","nx",8182});
            Player player(settings);assert(player.valid());App app(ui,renderer,player,settings,"",root+"/config.json");PadState pad{};
            auto press=[&](u64 button){app.input(pad,button,button,true);app.input(pad,0,0,true);};
            auto draw=[&]{ui.recordScreen(true);app.draw();};
            draw();assert(hasText(ui,"Sources")&&hasText(ui,"Remote")&&hasText(ui,"SD card"));
            durationEdge(ui,"--:--",1228,14);
            press(HidNpadButton_L);draw();assert(hasText(ui,"REMOTE CLIENTS")&&hasText(ui,"Saved client"));
            press(HidNpadButton_X);draw();assert(hasText(ui,"EDIT REMOTE CLIENT"));press(HidNpadButton_B);
            press(HidNpadButton_L);draw();assert(hasText(ui,"CATEGORIES")&&hasText(ui,"Accent colour")&&!hasText(ui,"Appearance /"));
            if(!automatic)saveFrame(renderer,docked?"settings-docked":"settings-handheld",layout.outputWidth(),layout.outputHeight());
            press(HidNpadButton_Right);draw();assert(!hasText(ui,"OLED"));
            press(HidNpadButton_A);press(HidNpadButton_A);draw();assert(hasText(ui,"OLED"));
            press(HidNpadButton_B);draw();assert(hasText(ui,"Categories")&&!hasText(ui,"OLED"));
            press(HidNpadButton_Down);press(HidNpadButton_B);press(HidNpadButton_Down);draw();
            assert(hasText(ui,"Show artwork")&&!hasText(ui,"Accent colour"));
            press(HidNpadButton_A);draw();int on=textCount(ui,"On");
            press(HidNpadButton_A);draw();assert(textCount(ui,"On")==on-1);
            press(HidNpadButton_A);draw();assert(textCount(ui,"On")==on);
            press(HidNpadButton_Minus);draw();assert(hasText(ui,"Browse"));press(HidNpadButton_B);draw();assert(hasText(ui,"Categories"));
            press(HidNpadButton_B);press(HidNpadButton_Up);press(HidNpadButton_A);press(HidNpadButton_A);draw();
            assert(hasText(ui,"Save colour"));press(HidNpadButton_B);press(HidNpadButton_B);
            press(HidNpadButton_StickR);draw();assert(!hasText(ui,"Sources")&&textCount(ui,"Play/Pause")==2);
            durationEdge(ui,"--:--",1216,18);
            if(!automatic)saveFrame(renderer,docked?"fullscreen-docked":"fullscreen-handheld",layout.outputWidth(),layout.outputHeight());
            Source local{false,{},root};std::vector<Track> tracks{{local,"Track 1.wav","One"},{local,"Track 2.wav","Two"},{local,"Track 10.wav","Ten"}};
            player.play(tracks,1);press(HidNpadButton_L);assert(player.snapshot().index==0);
            press(HidNpadButton_R);assert(player.snapshot().index==1);
            press(HidNpadButton_ZL|HidNpadButton_L);assert(player.snapshot().index==0);
            press(HidNpadButton_ZR|HidNpadButton_R);assert(player.snapshot().index==1);
            draw();assert(!hasText(ui,"Sources"));
            press(HidNpadButton_Up);draw();assert(hasText(ui,"Volume"));press(HidNpadButton_Up);draw();assert(hasText(ui,"Seek 10s"));
            press(HidNpadButton_Down);press(HidNpadButton_Down);for(int i=0;i<3;i++)press(HidNpadButton_Right);
            press(HidNpadButton_A);draw();assert(hasText(ui,"Equalizer preset")&&!hasText(ui,"Sources"));
            press(HidNpadButton_B);draw();assert(!hasText(ui,"Equalizer preset")&&!hasText(ui,"Sources"));
            press(HidNpadButton_B);draw();assert(hasText(ui,"CATEGORIES"));player.stop();
            press(HidNpadButton_R);press(HidNpadButton_R);draw();assert(hasText(ui,"MUSIC SOURCES"));
            press(HidNpadButton_A);for(int i=0;i<100;i++){app.update();SDL_Delay(5);}draw();
            assert(hasText(ui,"Track 1.wav")&&hasText(ui,"Track 10.wav"));
            press(HidNpadButton_A);draw();assert(hasText(ui,"Sources")!=automatic);
            if(automatic)press(HidNpadButton_B);
            player.next();app.update();draw();assert(hasText(ui,"Sources"));
            if(!automatic)saveFrame(renderer,docked?"player-docked":"player-handheld",layout.outputWidth(),layout.outputHeight());
            // A/B categories also work through the handheld touch route / Screen Control geometry.
            for(int i=0;i<2;i++)press(HidNpadButton_R); // Library -> Equalizer -> Settings.
            press(HidNpadButton_Minus);
            auto touch=[&](float x,float y){SDL_Event e{};e.type=SDL_FINGERDOWN;e.tfinger.fingerId=1;e.tfinger.x=x/1280;e.tfinger.y=y/layout.height();SDL_PushEvent(&e);e.type=SDL_FINGERUP;SDL_PushEvent(&e);app.input(pad);};
            touch(100,182+3*48+20);draw();assert(hasText(ui,"Auto Fullscreen")&&hasText(ui,"Categories"));
            touch(400,202);draw();
            {std::ifstream file(root+"/config.json");auto config=nlohmann::json::parse(file);assert(config["auto_fullscreen"]==!automatic);assert(config["remote_clients"].size()==1);}
            press(HidNpadButton_B);draw();assert(hasText(ui,"Select"));
            // Dock/undock with playback and the selected category intact.
            int index=player.snapshot().index;Layout next{!docked};SDL_SetWindowSize(window,next.outputWidth(),next.outputHeight());ui.setDocked(!docked);app.displayChanged();draw();
            assert(ui.screenFrame()["w"]==next.width()&&player.snapshot().index==index&&hasText(ui,"Auto Fullscreen"));
            player.stop();
        }
        SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);
    }
    // The earlier silent/visualizer-off fixture missed collisions with real metadata.
    // Exercise the actual App draw path with all technical fields and maximum bars.
    for(bool docked:{false,true})for(bool artwork:{false,true}) {
        Layout l{docked};auto *window=SDL_CreateWindow("Spectrum layout",0,0,l.outputWidth(),l.outputHeight(),0);
        auto *renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);assert(renderer);
        {
            UI ui(renderer,font.data(),font.size());ui.setDocked(docked);
            Settings settings;settings.musicRoot=root;settings.visualizer=true;settings.showArtwork=artwork;settings.theme="oled";
            Player player(settings);App app(ui,renderer,player,settings,"",root+"/config.json");
            Playback fixture;fixture.track.path="Opening.flac";fixture.details.title="Opening";
            fixture.details.artist="Namco Sounds";fixture.details.album="Dragon Ball FighterZ (Re-Engineered Soundtrack)";
            fixture.details.format="FLAC";fixture.details.bitDepth=24;fixture.details.sampleRate=44100;fixture.details.bitRate=1755;
            fixture.playing=true;fixture.position=29;fixture.duration=129;fixture.count=39;
            PadState pad{};
            for(bool full:{false,true}) {
                if(full)app.input(pad,HidNpadButton_StickR,HidNpadButton_StickR,true);
                for(float level:{1.f,0.f}) {
                    fixture.spectrum.fill(level);ui.recordScreen(true);app.draw(fixture);spectrumSeparation(ui,full,artwork);
                    if(level==1&&artwork)saveFrame(renderer,std::string("spectrum-")+(full?"full-":"mini-")+(docked?"docked":"handheld"),l.outputWidth(),l.outputHeight());
                }
            }
        }
        SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);
    }
    fs::remove_all(root);TTF_Quit();SDL_Quit();
    std::cout<<"NX app GUI: handheld/docked players, isolated fullscreen, shoulder/trigger actions, Settings A/B/touch, persistence, Auto Fullscreen and remote-client preservation passed.\n";
    std::cout<<"Visualizer/technical metadata separation: zero/maximum spectrum, both players, handheld/docked, artwork shown/hidden passed.\n";
}
