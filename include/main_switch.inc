#if defined(__SWITCH__) && !defined(NXH_APP_TEST)
int main(int, char **) {
    Result net = socketInitializeDefault();
    romfsInit();
    Result fonts = plInitialize(PlServiceType_User);
    mkdir("sdmc:/switch/nx-hifi", 0777);
    Settings settings;
    settings.musicRoot = "sdmc:/music";
    std::string error;
    playbackTrace("boot.gui-2.0.0", 0, true);
    loadSettings(ConfigPath, settings, error);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) < 0) {
        consoleInit(nullptr);
        printf("NX Hi-Fi could not initialize SDL: %s\nUse HOME to leave.", SDL_GetError());
        PadState pad;
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        padInitializeDefault(&pad);
        while (appletMainLoop()) {
            padUpdate(&pad);

            consoleUpdate(nullptr);
        }
        consoleExit(nullptr);
        if (R_SUCCEEDED(fonts))
            plExit();
        if (R_SUCCEEDED(net))
            socketExit();
        return 1;
    }
    Layout initialLayout{appletGetOperationMode() == AppletOperationMode_Console};
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"linear");
    SDL_Window *window = SDL_CreateWindow("NX Hi-Fi", 0, 0, initialLayout.outputWidth(),
                                          initialLayout.outputHeight(), SDL_WINDOW_SHOWN);
    SDL_Renderer *renderer =
        window
            ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC)
            : nullptr;
    if(renderer){SDL_RendererInfo info{};SDL_GetRendererInfo(renderer,&info);std::ofstream report("sdmc:/switch/nx-hifi/renderer.json");if(report)report<<nlohmann::json{{"version",Version},{"renderer",info.name?info.name:"unknown"},{"gpu_accelerated",bool(info.flags&SDL_RENDERER_ACCELERATED)},{"vsync",bool(info.flags&SDL_RENDERER_PRESENTVSYNC)}}.dump(2)<<'\n';}
    PlFontData font{};
    bool ready = R_SUCCEEDED(fonts) &&
                 R_SUCCEEDED(plGetSharedFontByType(&font, PlSharedFontType_Standard)) &&
                 TTF_Init() == 0;
    if (!renderer || !ready) {
        if (renderer)
            SDL_DestroyRenderer(renderer);
        if (window)
            SDL_DestroyWindow(window);
        SDL_Quit();
        consoleInit(nullptr);
        printf("NX Hi-Fi: graphics or system font unavailable.\nLaunch through title override and "
               "use HOME to leave.\n");
        PadState pad;
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        padInitializeDefault(&pad);
        while (appletMainLoop()) {
            padUpdate(&pad);

            consoleUpdate(nullptr);
        }
        consoleExit(nullptr);
        if (R_SUCCEEDED(fonts))
            plExit();
        if (R_SUCCEEDED(net))
            socketExit();
        return 1;
    }
    SDL_RenderSetLogicalSize(renderer, 1280, 720);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG);
    {
        UI ui(renderer, font.address, font.size);
        ui.setDocked(initialLayout.docked);
        Player player(settings);
        if (!player.valid())
            error = "Audio output unavailable: " + std::string(SDL_GetError());
        if (R_FAILED(net))
            error = "Network service unavailable; SD playback remains available.";
        auto application=std::make_unique<App>(ui, renderer, player, settings, error);
        auto &app=*application;
        PadState pad;
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        padInitializeDefault(&pad);
        while (appletMainLoop() && app.alive()) {
            // Poll on the UI thread: libnx refreshes operation mode in appletMainLoop.
            // No audio/network objects are recreated when changing display mode.
            bool docked = appletGetOperationMode() == AppletOperationMode_Console;
            if (docked != ui.layout().docked) {
                Layout next{docked};
                SDL_SetWindowSize(window, next.outputWidth(), next.outputHeight());
                ui.setDocked(docked);
                app.displayChanged();
                playbackTrace(docked ? "display.docked" : "display.handheld", next.outputWidth());
            }
            auto start=SDL_GetPerformanceCounter();auto display=SDL_GetPerformanceCounter();
            app.input(pad);auto input=SDL_GetPerformanceCounter();
            app.update();auto update=SDL_GetPerformanceCounter();
            app.draw();auto draw=SDL_GetPerformanceCounter();frameDiagnostics.record(renderer,start,display,input,update,draw);
        }
    }
    stopStorageDetection();
    shutdownDisc();
    IMG_Quit();
    TTF_Quit();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (R_SUCCEEDED(fonts))
        plExit();
    if (R_SUCCEEDED(net))
        socketExit();
    romfsExit();
    return 0;
}
#elif !defined(NXH_APP_TEST)
#include "main_host.inc"
#endif
