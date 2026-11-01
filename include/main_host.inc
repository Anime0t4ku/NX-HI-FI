int main(int argc,char **argv) {
    (void)argc;(void)argv;
#ifndef __PROSPERO__
    const char *screenshot=argc>2 && std::string(argv[1])=="--screenshot" ? argv[2] : nullptr;
    SDL_setenv("SDL_AUDIODRIVER","dummy",0);
#endif
    initPlatform();
    Settings settings;settings.musicRoot="/data/music";
    std::string error;loadSettings(ConfigPath,settings,error);
    settings.remoteClients.clear();
#ifndef __PROSPERO__
    if(screenshot)settings.remoteEnabled=false;
#endif
    if(!storageAccessReady())error="Storage access unavailable. Start your local ELF loader (port 9021), then reopen NX Hi-Fi.";
    playbackTrace("boot.nx-2.0.0-agc",0,true);
#ifdef __PROSPERO__
    SDL_SetHint(SDL_HINT_RENDER_DRIVER,"ps5agc");
#endif
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_EVENTS|SDL_INIT_GAMECONTROLLER)<0){std::fprintf(stderr,"SDL: %s\n",SDL_GetError());return 1;}
    SDL_DisplayMode output{};
    if(SDL_GetCurrentDisplayMode(0,&output)!=0||output.w<640||output.h<480){output.w=1920;output.h=1080;}
#ifndef __PROSPERO__
    if(screenshot){output.w=argc>3?std::atoi(argv[3]):1920;output.h=argc>4?std::atoi(argv[4]):1080;}
#endif
    Uint32 windowFlags=SDL_WINDOW_SHOWN|SDL_WINDOW_ALLOW_HIGHDPI;
#ifdef __PROSPERO__
    windowFlags|=SDL_WINDOW_FULLSCREEN_DESKTOP;
#endif
    SDL_Window *window=SDL_CreateWindow("NX Hi-Fi",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,output.w,output.h,windowFlags);
    // Native AGC is part of the title: no software-renderer fallback is silently selected.
#ifdef __PROSPERO__
    Uint32 rendererFlags=SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC;
#else
    Uint32 rendererFlags=SDL_RENDERER_SOFTWARE;
#endif
    SDL_Renderer *renderer=window?SDL_CreateRenderer(window,-1,rendererFlags):nullptr;
    if(!renderer||TTF_Init()!=0){
        const char *message=SDL_GetError();std::fprintf(stderr,"Graphics: %s\n",message);
#ifdef __PROSPERO__
        std::ofstream log("/data/prospero-hifi/graphics-error.txt");if(log)log<<"Prospero v"<<Version<<" (native AGC): "<<message<<'\n';
#endif
        SDL_Quit();return 2;
    }
    std::ifstream file(std::string(assetRoot())+"font.ttf",std::ios::binary);
    std::vector<char> font((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
    if(font.empty()){std::fprintf(stderr,"Missing assets/font.ttf\n");SDL_Quit();return 3;}
    SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);IMG_Init(IMG_INIT_JPG|IMG_INIT_PNG);
    {
        UI ui(renderer,font.data(),font.size());ui.setDocked(true);refreshDisplay(window,renderer,true);ui.refreshRasterScale();
        Player player(settings);if(!player.valid())error="Audio output unavailable: "+std::string(SDL_GetError());
        App app(ui,renderer,player,settings,error);
        bindKeyboard(&ui,renderer,[&]{app.update();if(refreshDisplay(window,renderer))ui.refreshRasterScale();});
        PadState pad{};
        while(app.alive()&&!platformQuitRequested()){
            Uint64 start=SDL_GetTicks64();
            auto frameStart=SDL_GetPerformanceCounter();
            if(refreshDisplay(window,renderer)){ui.refreshRasterScale();app.displayChanged();}
            auto displayDone=SDL_GetPerformanceCounter();
            app.input(pad);auto inputDone=SDL_GetPerformanceCounter();
            app.update();auto updateDone=SDL_GetPerformanceCounter();
#ifndef __PROSPERO__
            if(screenshot&&SDL_getenv("PROSPERO_PREVIEW_FULLSCREEN")){PadState empty{};app.input(empty,HidNpadButton_StickR,HidNpadButton_StickR,true);}
#endif
            app.draw();auto drawDone=SDL_GetPerformanceCounter();
            frameDiagnostics.record(renderer,frameStart,displayDone,inputDone,updateDone,drawDone);
#ifndef __PROSPERO__
            if(screenshot){
                auto *surface=SDL_CreateRGBSurfaceWithFormat(0,output.w,output.h,32,SDL_PIXELFORMAT_RGBA32);
                SDL_RenderReadPixels(renderer,nullptr,surface->format->format,surface->pixels,surface->pitch);
                IMG_SavePNG(surface,screenshot);SDL_FreeSurface(surface);break;
            }
#endif
#ifndef __PROSPERO__
            auto elapsed=SDL_GetTicks64()-start;if(elapsed<16)SDL_Delay(16-elapsed);
#endif
        }
        bindKeyboard(nullptr,nullptr,{});
    }
    shutdownSMB();shutdownDisc();closePlatform();IMG_Quit();TTF_Quit();SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();return 0;
}
