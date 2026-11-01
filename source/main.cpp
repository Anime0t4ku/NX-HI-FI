#include "input_timing.hpp"
#include "player_navigation.hpp"
#include "visualizer.hpp"
#include "collection.hpp"
#include "album_pages.hpp"
#include <random>
#include "extended_audio.hpp"
#include "settings_navigation.hpp"
#include "storage_devices.hpp"
#include "frame_diagnostics.hpp"
#include <fstream>
#include "playback_diagnostics.hpp"
#include "loader.hpp"
#include "player.hpp"
#include "player_endpoint.hpp"
#include "disc.hpp"
#include "ui.hpp"
#include "remote.hpp"
#include "remote_view.hpp"
#include "discovery.hpp"
#include <future>
#include <ctime>
#include <arpa/inet.h>
#include <cstring>
#include "platform.hpp"
#include "keyboard.hpp"
#include <sys/stat.h>
using namespace nxh;
static const std::string ConfigPath = configPath();
static bool keyboard(const std::string &label, std::string &value, bool password = false) {
    return switchKeyboard(label, value, password);

}
class App {
#ifdef NXH_APP_TEST
    friend struct AppIntegration;
#endif
    const Layout &layout() const {
        return ui.layout();
    }
#include "app_remote.inc"
#include "app_independent.inc"
    UI &ui;
    SDL_Renderer *renderer;
    PlayerEndpoint player;
    Settings settings;
    std::string settingsPath;
    std::unique_ptr<RemoteView> targetView;
    std::vector<std::future<void>> retiredClients;
    bool clientEditing = false;
    RemoteClientConfig editClient;
    int editClientIndex = -1;
    uint64_t editClientRevision = 0;
    double clientTouchY = -1;
    bool clientGesture = false;
    void disconnectTarget() {
        closeNativeRemote();
        if (!targetView)
            return;
        auto config = targetView->config();
        auto token = targetView->screenToken();
        auto old = targetView->detach();
        targetView.reset();
        if (old)
            retiredClients.push_back(std::async(std::launch::async, [old, config, token] {
                if (!token.empty()) {
                    std::atomic<bool> cancel{false};
                    remoteHttp(config, "POST", "/api/cmd",
                               {{"op", "screen_close"}, {"session", token}}, cancel, 700);
                }
                delete old;
            }));
        setAppearance(settings.theme, settings.accent,settings.menuOpacity,settings.look);
    }
    void connectTarget(int index) {
        if (index >= 0 && index < int(settings.remoteClients.size()))
            beginConnect(settings.remoteClients[index]);
    }
    void beginClientEdit(int index) {
        editClientRevision = revision;
        editClientIndex = index;
        editClient = index < 0 ? RemoteClientConfig{} : settings.remoteClients[index];
        clientEditing = true;
        selected = scroll = 0;
    }
    void clientAction() {
        if (!clientEditing) {
            if (selected < int(settings.remoteClients.size()))
                connectTarget(selected);
            else if (selected == int(settings.remoteClients.size()))
                beginClientEdit(-1);
            else
                startScan();
            return;
        }
        if (selected == 0) {
            keyboard("Client name", editClient.name);
            cooldown = SDL_GetTicks64() + 120;
        } else if (selected == 1) {
            choose("Client type", {"nx", "prospero", "mister"}, {"NX Hi-Fi", "Prospero Hi-Fi", "MiSTer Hi-Fi"}, editClient.type,
                   [this](auto v) {
                       editClient.type = v.template get<std::string>();
                       editClient.port = 8182;
                   });
        } else if (selected == 2) {
            keyboard("Host or IP address", editClient.host);
            cooldown = SDL_GetTicks64() + 120;
        } else if (selected == 4) {
            clientEditing = false;
            selected = scroll = 0;
        } else {
            if (editClientRevision != revision) {
                status = "Settings changed. Reopen the client editor.";
                return;
            }
            auto before = settings;
            if (selected == 3) {
                std::string error;
                if (!validateRemoteClient(editClient, error)) {
                    status = error;
                    return;
                }
                if (editClientIndex < 0) {
                    if (settings.remoteClients.size() >= 32) {
                        status = "You can save up to 32 clients.";
                        return;
                    }
                    settings.remoteClients.push_back(editClient);
                } else
                    settings.remoteClients[editClientIndex] = editClient;
            } else if (selected == 5 && editClientIndex >= 0)
                settings.remoteClients.erase(settings.remoteClients.begin() + editClientIndex);
            if (!persist()) {
                settings = before;
                return;
            }
            clientEditing = false;
            selected = scroll = 0;
        }
    }
    Loader browser, art, tags;
    std::string tagKey;
    int page = 0, selected = 0, scroll = 0;
    struct BrowsePosition {
        int selected = 0, scroll = 0;
        std::string child;
    };
    std::map<std::string, BrowsePosition> browsePositions;
    std::string browseKey() const {
        return source.share.server + "/" + source.share.share + "/" + source.root + "/" + folder;
    }
    std::vector<nlohmann::json> optionValues;
    std::vector<std::string> optionLabels;
    int optionIndex = 0;
    std::string optionTitle;
    std::function<void(nlohmann::json)> optionApply;
    void choose(std::string title, std::vector<nlohmann::json> values,
                std::vector<std::string> labels, nlohmann::json current,
                std::function<void(nlohmann::json)> apply) {
        optionSeparator = -1;
        optionTitle = std::move(title);
        optionValues = std::move(values);
        optionLabels = std::move(labels);
        optionApply = std::move(apply);
        optionIndex = 0;
        for (int i = 0; i < int(optionValues.size()); ++i)
            if (optionValues[i] == current)
                optionIndex = i;
    }
    int optionSeparator = -1;
    void drawOptions() {
        if (!optionApply)
            return;
        ui.rect(140, 100, 1000, 510 + layout().extraHeight(), {PANEL.r,PANEL.g,PANEL.b,250});
        ui.text(170, 120, optionTitle, 26, ACCENT);
        int first = layout().optionFirst(optionIndex);
        ui.scrollIndicator(1117,170,layout().optionRows()*48-6,int(optionLabels.size()),first,layout().optionRows());
        for (int i = first; i < std::min(first + layout().optionRows(), int(optionLabels.size()));
             ++i)
            {if(i==optionSeparator)ui.rect(182,168+(i-first)*48,916,1,MUTED);
            ui.button(170, 170 + (i - first) * 48, 940, 42, optionLabels[i], i == optionIndex);}
    }
    bool sourceSelected = false, controlsFocused = false;
    SettingsNavigation settingsNavigation;
    int settingsCategoryScroll=0;
    int control = 0;
    PlayerNavigation menuFocus,fullFocus{2,1};
    PlayerNavigation &playerFocus(){return fullscreen?fullFocus:menuFocus;}
    const PlayerNavigation &playerFocus()const{return fullscreen?fullFocus:menuFocus;}
    PlayerRect seekRect()const{return playerSeekRect(fullscreen,layout().extraHeight(),playerX(),playerWidth());}
    PlayerRect volumeRect()const{return playerVolumeRect(fullscreen,layout().extraHeight(),playerX(),playerWidth());}
    void syncPlayerFocus(){seekerFocused=(fullscreen||controlsFocused)&&playerFocus().seek();control=std::max(0,playerFocus().action(fullscreen));}
    void playerVertical(int delta){
        if(fullscreen&&canExpandVisualizer()&&fullFocus.row==0&&delta<0)fullFocus.row=-1;
        else if(fullscreen&&fullFocus.row==-1){if(delta>0)fullFocus={0,0};}
        else playerFocus().vertical(fullscreen,delta);
        syncPlayerFocus();
    }
    void playerHorizontal(int delta){
        if(fullscreen&&fullFocus.row==-1)cycleVisualizer(delta);
        else if(playerFocus().seek())player.seek(delta*10);
        else if(playerFocus().volume(fullscreen)){settings.volume=std::clamp(settings.volume+delta*5,0,100);persist();}
        else playerFocus().horizontal(fullscreen,delta);
        syncPlayerFocus();
    }
    void playerAction(){
        int action=playerFocus().action(fullscreen);
        if(fullscreen&&fullFocus.row==-1){if(canExpandVisualizer())visualizerFullscreen=true;return;}
        if(action==6){playerSettingsOpen=true;playerSettingsTab=0;playerSettingsRow=0;}
        else if(action==7)toggleFullscreen();
        else if(action>=0)transport(action);
    }
    bool colourPicking = false, scrubbing = false, mouseScrub = false;
    SDL_FingerID scrubFinger = 0;
    int colourSelection = 0, colourDrag = -1;
    bool mouseColourDrag = false;
    SDL_FingerID colourFinger = 0;
    std::array<int, 3> customRGB{};
    double scrubPosition = 0;
    std::string scrubTrack;
    std::string colourTarget="accent_colour";
    void colourChoices(const std::string &key){std::vector<nlohmann::json> values;std::vector<std::string> labels;if(key=="visualizer_colour"){values.push_back("theme");labels.push_back("Theme colour");}for(auto &preset:accentPresets){values.push_back(preset.hex);labels.push_back(preset.name);}values.push_back("custom");labels.push_back("Custom...");auto current=settingsDocument(settings)[key];if(current.is_string()&&current.template get<std::string>()!="theme")current=colourHex(colourRGB(current.template get<std::string>()));if(std::find(values.begin(),values.end(),current)==values.end())current="custom";choose(key=="visualizer_colour"?"Visualizer colour":"Accent colour",values,labels,current,[this,key](auto v){if(v=="custom"){colourTarget=key;openColours();}else applySettings({{key,v}});});}
    void openColours() {
        colourPicking = true;
        status = "";
        colourSelection = 0;
        customRGB = colourRGB(colourTarget=="visualizer_colour"&&settings.visualizerColour!="theme"?settings.visualizerColour:settings.accent);
        for (int i = 0; i < 10; i++)
            if (settings.accent == accentPresets[i].hex)
                colourSelection = i;
    }
    bool beginColourDrag(int x, int y) {
        if (!colourPicking)
            return false;
        for (int i = 0; i < 3; i++)
            if (inside(x-198, y, 210, 326 + i * 47, 600, 36)) {
                colourDrag = i;
                colourSelection = 10 + i;
                dragColour(x);
                return true;
            }
        return false;
    }
    void dragColour(int x) {
        if (colourDrag >= 0)
            customRGB[colourDrag] = std::clamp((x - 198 - 210) * 255 / 600, 0, 255);
    }
    void closeColours(bool save) {
        if(save){applySettings({{colourTarget,colourHex(customRGB)}});}
        colourPicking = false;
        colourDrag = -1;
        mouseColourDrag = false;
        setAppearance(settings.theme, settings.accent,settings.menuOpacity,settings.look);
    }
    void colourAction() {
        if (colourSelection < 10)
            customRGB = colourRGB(accentPresets[colourSelection].hex);
        else if (colourSelection == 13) {
            std::string hex = colourHex(customRGB);
            if (keyboard("Custom colour (#RRGGBB)", hex)) {
                if (normalizeColour(hex)) {
                    customRGB = colourRGB(hex);
                    status = "";
                } else
                    status = "Enter six hexadecimal digits, for example #EF4444.";
            }
            cooldown = SDL_GetTicks64() + 120;
        } else if (colourSelection == 14)
            closeColours(true);
        else if (colourSelection == 15)
            closeColours(false);
    }
    void colourAdjust(int dir) {
        if (colourSelection < 10)
            colourSelection = std::clamp(colourSelection + dir, 0, 9);
        else if (colourSelection < 13)
            customRGB[colourSelection - 10] =
                std::clamp(customRGB[colourSelection - 10] + dir * 5, 0, 255);
        else if (colourSelection >= 14)
            colourSelection = colourSelection == 14 ? 15 : 14;
    }
    void changeTheme(int dir) {
        const char *themes[] = {"dark", "oled", "light"};
        int index = 0;
        for (int i = 0; i < 3; i++)
            if (settings.theme == themes[i])
                index = i;
        auto before = settings.theme;
        settings.theme = themes[(index + dir + 3) % 3];
        if (!persist()) {
            settings.theme = before;
            player.settings(settings);
        }
    }
    std::string accentName() const {
        for (auto &preset : accentPresets)
            if (settings.accent == preset.hex)
                return std::string(preset.name) + "  " + settings.accent;
        return "Custom  " + settings.accent;
    }
    bool scrubHit(int x, int y) const {
        return seekRect().contains(x,y);
    }
    void previewScrub(int x) {
        auto p = player.snapshot();
        if (p.duration > 0)
            scrubPosition =
                std::clamp((x - seekRect().x) / double(seekRect().w), 0., 1.) * p.duration;
    }
    bool beginScrub(int x, int y) {
        if(listening||playerSettingsOpen||visualizerFullscreen||optionApply)return false;
        auto p = player.snapshot();
        if (!scrubHit(x, y) || p.duration <= 0 || (!p.playing && !p.busy))
            return false;
        scrubbing = true;
        playerFocus().row=0;playerFocus().column=0;
        seekerFocused = true;
        controlsFocused = true;
        scrubTrack = p.track.path;
        previewScrub(x);
        return true;
    }
    void finishScrub(int x) {
        if (!scrubbing)
            return;
        previewScrub(x);
        auto p = player.snapshot();
        if (p.track.path == scrubTrack)
            player.seek(scrubPosition - p.position);
        scrubbing = false;
        mouseScrub = false;
    }
    bool fullscreen = false;
#include "app_visualizer.inc"
#include "app_collections.inc"
#include "app_album_pages.inc"
#include "app_playlists.inc"
    bool listening=false;
    u64 listeningBlocked=0;
    void setListening(bool enabled){
        if(enabled){colourPicking=false;optionApply={};if(!fullscreen)toggleFullscreen();}
        listening=enabled;scrubbing=mouseScrub=false;wake();
        verticalRepeat={};horizontalRepeat={};
    }
    void toggleFullscreen() {
        if(colourPicking)return;
        visualizerFullscreen=false;playerSettingsOpen=false;
        fullscreen=!fullscreen;listening=false;scrubbing=mouseScrub=false;
        controlsFocused=fullscreen;
        if(fullscreen)fullFocus={2,1};
        syncPlayerFocus();
    }
    Source source;
    std::string folder, status, artKey;
    std::vector<Entry> entries;
    uint64_t revision = 1, sourcesRevision = 1, editSourcesRevision = 1;
    bool seekerFocused = false, sleeping = false, wakeGesture = false;
    Uint64 lastInteraction = SDL_GetTicks64(), lastPublish = 0, lastIpCheck = 0;
    ButtonRepeat horizontalRepeat;
    int attemptedPort = -1;
    bool attemptedRemote = false;
    std::string remoteAddress;
    std::unique_ptr<RemoteServer> remote;
    std::vector<std::future<void>> retiringRemotes;
    Uint64 reconfigureRemoteAt = 0;
    std::vector<unsigned char> remoteArt;
    struct RemoteCall {
        nlohmann::json request;
        std::promise<nlohmann::json> result;
        std::atomic<bool> cancelled{false};
    };
    std::mutex callsMutex;
    std::deque<std::shared_ptr<RemoteCall>> calls;
    int playerX() const {
        return artworkVisible() ? 560 : (fullscreen&&settings.fullPlayerCard?84:64);
    }
    int playerWidth() const {
        return 1200-(fullscreen&&settings.fullPlayerCard?20:0)-playerX();
    }
    bool artworkVisible() const {
        return settings.showArtwork && (artTexture || !settings.hideMissingArt);
    }
    std::string clock() const {
        std::time_t now=std::time(nullptr);std::tm local{};
        if(localtime_r(&now,&local))return formatClock(local.tm_hour,local.tm_min,settings.clock24h);
        return "--:--";
    }

    void header() {
        if(!(listening&&settings.listeningHideTitle)){
        ui.text(28, 19, "NX Hi-Fi", 38, ACCENT);
        ui.text(28 + ui.textWidth("NX Hi-Fi", 38) + 14, 35, std::string("v") + Version, 16, MUTED);
        }
        if (settings.showClock&&!(listening&&settings.listeningHideClock)) {
            auto label = clock();
            ui.text(1252 - ui.textWidth(label, 22), 29, label, 22, TEXT);
        }
    }
    void wake() {
        lastInteraction = SDL_GetTicks64();
        sleeping = false;
    }
    nlohmann::json submitRemote(const nlohmann::json &request) {
        playbackTrace("http.command.enqueue");
        auto call = std::make_shared<RemoteCall>();
        call->request = request;
        auto result = call->result.get_future();
        {
            std::lock_guard<std::mutex> lock(callsMutex);
            if (calls.size() >= 24)
                return {{"ok", false}, {"error", "Remote is busy. Try again."}};
            calls.push_back(call);
        }
        if (result.wait_for(std::chrono::milliseconds(1800)) != std::future_status::ready) {
            call->cancelled = true;
            return {
                {"ok", false},
                {"error", "NX Hi-Fi is busy editing. Try again after closing the input dialog."}};
        }
        return result.get();
    }
    void sourcesChanged() {
        ++sourcesRevision;
        browser.cancel();
        loading = false;
        sourceSelected = false;
        entries.clear();
        folder.clear();
        if (!editing && page == 1)
            changePage(0);
        if (fullscreen)
            controlsFocused = !seekerFocused;
        if (!editing) {
            selected = std::clamp(selected, 0, maxSelection());
            scroll = std::min(scroll, selected);
        }
    }
    bool applySettings(const nlohmann::json &patch) {
        if(independent){hostCommand("settings",{{"patch",patch},{"revision",revision}});return true;}
        auto before = settings;
        std::string error;
        if (!patchSettings(settings, patch, error)) {
            status = error;
            return false;
        }
        if (!persist()) {
            settings = before;
            player.settings(settings);
            return false;
        }
        if (settings.musicRoot != before.musicRoot || settings.storageFolders != before.storageFolders)
            sourcesChanged();
        return true;
    }
    nlohmann::json remoteCommand(const nlohmann::json &j) {
        using J = nlohmann::json;
        auto fail = [](std::string error) { return J{{"ok", false}, {"error", error}}; };
        std::string op = j.value("op", std::string{});
        if(op=="disc_eject"){if(!discConnected||discSignature.empty())return fail("Insert audio CD");startDiscEject();return {{"ok",true}};}
        if(op=="playlist_play"){auto key="playlist:"+j.at("id").get<std::string>();if(!savedPlaylist(j.at("id")))return fail("Playlist no longer exists.");playAlbumPage(key,j.value("index",0),j.value("shuffle",false));return {{"ok",true}};}
        if(op=="disc_refresh"){if(!discConnected)return fail("No USB optical drive connected.");discSignature.clear();albumPages.invalidate("disc");discProbeAt=0;return {{"ok",true}};}
        if(op=="disc_play"){if(!discConnected||discSignature.empty())return fail("Insert audio CD");playAlbumPage("disc",j.value("index",0),j.value("shuffle",false));return {{"ok",true}};}
        if(op=="album_open"){auto id=j.at("id").get<std::string>();if(!requestAlbumPage(id,j.value("refresh",false)))return fail(status);return {{"ok",true}};}
        if(op=="album_track"){auto id=j.at("id").get<std::string>();if(!collectionAlbum(id))return fail("Album no longer exists.");playAlbumPage(id,j.value("index",0),j.value("shuffle",false));return {{"ok",true}};}
        if(op.rfind("playlist_",0)==0||op.rfind("library_",0)==0||op.rfind("album_",0)==0||op.rfind("station_",0)==0||op=="catalog_get"){
            if(op=="station_play"){for(auto &station:catalog["stations"])if(station["id"]==j.at("id")){playStation(station);return {{"ok",true}};}return fail("Station no longer exists.");}
            if(op=="album_play"){for(auto &album:catalog["albums"])if(album["id"]==j.at("id")){Source src;std::string error;if(!resolveReference(album["source"],settings,src,error))return fail(error);std::vector<Track> tracks;for(auto &t:album["tracks"])tracks.push_back({src,t["path"],t["name"]});if(tracks.empty())return fail("No tracks in this album; refresh it.");player.play(std::move(tracks),0);return {{"ok",true}};}return fail("Album no longer exists.");}
            if(op=="album_add"&&(!j.contains("sources_revision")||j["sources_revision"]!=sourcesRevision))return fail("Files changed; refresh the browser.");
            auto response=collection->command(j,settings);refreshCatalog();if(response.value("ok",false)&&op=="album_refresh")requestAlbumPage(j.at("id"),true);lastPublish=0;return response;
        }
        if (op.rfind("screen_", 0) == 0)
            return fail("Screen Control has been removed.");
        auto sourceCheck = [&] {
            return j.contains("sources_revision") && j["sources_revision"].is_number_integer() &&
                   j["sources_revision"].get<uint64_t>() == sourcesRevision;
        };
        if (op == "client_save" || op == "client_remove") {
            if (!j.contains("revision") || !j["revision"].is_number_integer() ||
                j["revision"].get<uint64_t>() != revision)
                return fail("Settings changed; refresh and try again.");
            int index = j.value("index", -1);
            if (index < -1 || index >= int(settings.remoteClients.size()))
                return fail("Client no longer exists.");
            auto before = settings;
            if (op == "client_remove") {
                if (index < 0)
                    return fail("Choose a client.");
                settings.remoteClients.erase(settings.remoteClients.begin() + index);
            } else {
                auto v = j.at("client");
                RemoteClientConfig c;
                c.name = v.at("name");
                c.host = v.at("host");
                c.type = v.at("type");
                c.port = 8182;
                std::string error;
                if (!validateRemoteClient(c, error))
                    return fail(error);
                if (index < 0) {
                    if (settings.remoteClients.size() >= 32)
                        return fail("You can save up to 32 clients.");
                    settings.remoteClients.push_back(c);
                } else
                    settings.remoteClients[index] = c;
            }
            if (!persist()) {
                settings = before;
                return fail(status);
            }
        } else if(op=="listening_mode"){
            if(!j.contains("enabled")||!j["enabled"].is_boolean())throw std::runtime_error("enabled must be boolean");
            setListening(j["enabled"].get<bool>());
        } else if (op == "settings") {
            if (!j.contains("revision") || !j["revision"].is_number_integer() ||
                j["revision"].get<uint64_t>() != revision)
                return fail("Settings changed; refresh and try again.");
            if (!applySettings(j.at("patch")))
                return fail(status);
        } else if (op == "source_save" || op == "source_remove") {
            if (!sourceCheck())
                return fail("Files changed; refresh and try again.");
            int index = j.value("index", -1);
            if (index < -1 || index >= int(settings.shares.size()))
                return fail("Source no longer exists.");
            auto before = settings;
            if (op == "source_remove") {
                if (index < 0)
                    return fail("Choose an SMB share.");
                settings.shares.erase(settings.shares.begin() + index);
            } else {
                Share share;
                std::string error;
                if (!readShare(j.at("share"), index >= 0 ? &settings.shares[index] : nullptr, share,
                               error))
                    return fail(error);
                if (index < 0)
                    settings.shares.push_back(share);
                else
                    settings.shares[index] = share;
            }
            if (!persist()) {
                settings = before;
                player.settings(settings);
                return fail(status);
            }
            sourcesChanged();
        } else if (op == "play") {
            playbackTrace("main.remote-play.begin");
            if (!sourceCheck())
                return fail("Files changed; refresh and try again.");
            int index = j.value("source", -1);
            if(index>=int(settings.shares.size()))return fail("Source no longer exists.");
            Source src;
            if(index<0){if(!resolveStorageSource(settings,index,src))return fail("Storage drive is no longer available.");}
            else src={true,settings.shares[index],settings.shares[index].folder};
            const auto &rows = j.at("tracks");
            if (!rows.is_array() || rows.empty() || rows.size() > 50000)
                return fail("Invalid track list.");
            std::vector<Track> tracks;
            for (auto &row : rows) {
                auto path = row.get<std::string>();
                if (path.size() > 4096 || !safeRelative(path) || !audioFile(path))
                    return fail("Invalid track path.");
                tracks.push_back({src, path, title(filename(path))});
            }
            int selected = j.value("index", -1);
            if (selected < 0 || selected >= int(tracks.size()))
                return fail("Track no longer exists.");
            playbackTrace("main.remote-play.validated", tracks.size());
            player.play(std::move(tracks), selected);
            if(settings.autoFullscreen&&!fullscreen)toggleFullscreen();
            playbackTrace("main.remote-play.returned");
        } else if (op == "playpause")
            player.pause();
        else if (op == "previous")
            previousTrack();
        else if (op == "next")
            nextTrack();
        else if (op == "stop")
            stopPlayback();
        else if (op == "shuffle") {
            if (!applySettings({{"shuffle", !settings.shuffle}}))
                return fail(status);
        } else if (op == "repeat") {
            if (!applySettings({{"repeat", (settings.repeat + 1) % 3}}))
                return fail(status);
        } else if (op == "volume") {
            if (!applySettings({{"volume", j.at("value")}}))
                return fail(status);
        } else if (op == "seek" || op == "seek_to") {
            const auto &value = j.at(op == "seek" ? "seconds" : "position");
            if (!value.is_number())
                return fail("Invalid seek position.");
            double amount = value.get<double>();
            if (!std::isfinite(amount) || std::abs(amount) > 864000)
                return fail("Invalid seek position.");
            auto p = player.snapshot();
            if (op == "seek_to") {
                if (amount < 0)
                    return fail("Invalid seek position.");
                amount -= p.position;
            }
            player.seek(amount);
        } else
            return fail("Unknown command.");
        wake();
        lastPublish = 0;
        return {{"ok", true}, {"revision", revision}, {"sources_revision", sourcesRevision}};
    }
    std::string publishedStructure;
    nlohmann::json remoteState(bool full=true) {
        using J = nlohmann::json;
        auto p = libraryPlayback(player.snapshot());
        auto badges = J::array();
        if (!p.details.format.empty())
            badges.push_back(p.details.format);
        if (p.details.bitDepth)
            badges.push_back(std::to_string(p.details.bitDepth) + " bit");
        if (p.details.sampleRate) {
            TrackDetails rate;
            rate.sampleRate = p.details.sampleRate;
            badges.push_back(audioDetails(rate));
        }
        if (p.details.bitRate)
            badges.push_back(std::to_string(p.details.bitRate) + " kbps");
        J result={{"revision",revision},{"clock",clock()}};
        if(full){
        auto eqPresets = J::array();
        for (auto &preset : presets)
            eqPresets.push_back({{"name", preset.name}, {"gains", preset.gains}});
        auto accents = J::array();
        for (auto &preset : accentPresets)
            accents.push_back({{"name", preset.name}, {"hex", preset.hex}});
        auto localSources=J::array();
        for(auto &device:storageDevices_)localSources.push_back({{"id",device.id},{"name",device.name},{"mount",device.mount},{"folder",storageFolder(settings,device)}});
        J structure={{"identity",remoteIdentity()}, {"app_id","nx-hifi"}, {"protocol_version",1}, {"listening_mode",listening},
                {"version", Version},
                {"catalog",catalog},
                {"album_views",albumPages.snapshot()},{"disc_connected",discConnected},{"disc_ejecting",discEjecting},{"disc_eject_error",discEjectError},{"disc_signature",discSignature},{"disc_view",albumPage("disc")},
                {"local_sources",localSources},
                {"physical_disc",discConnected},
                {"revision", revision},
                {"sources_revision", sourcesRevision},
                {"settings", settingsDocument(settings)},
                {"fields", remoteSettingFields()},
                {"presets", eqPresets},
                {"accents", accents},
                {"clock", clock()},
                {"remote_address", remoteAddress}};
        if(full) result.update(structure);
        }
        result.update({
                {"playback",
                 {{"title", trackTitle(p)},
                  {"artist", p.details.artist},
                  {"album", trackAlbum(p)},
                  {"badges", badges},
                  {"index", p.index},
                  {"count", p.count},
                  {"position", p.position},
                  {"duration", p.duration},
                  {"live",p.track.source.root==RadioRoot},
                  {"duration_estimated", p.details.durationEstimated},
                  {"playing", p.playing},
                  {"paused", p.paused},
                  {"busy", p.busy},
                  {"error", p.error},
                  {"spectrum", p.spectrum},
                  {"track_path",p.track.path},{"source_ref",sourceReference(p.track.source)},
                  {"track_key", p.track.source.share.server + "/" + p.track.source.share.share +
                                    "/" + p.track.source.root + "/" + p.track.path}}}});
        return result;
    }
    void retireRemote() {
        if (!remote)
            return;
        remote->beginShutdown();
        auto *old = remote.release();
        retiringRemotes.push_back(std::async(std::launch::async, [old] { delete old; }));
    }
    void updateRemote() {
        std::deque<std::shared_ptr<RemoteCall>> pending;
        {
            std::lock_guard<std::mutex> lock(callsMutex);
            pending.swap(calls);
        }
        for (auto &call : pending) {
            if (call->cancelled)
                continue;
            try {
                playbackTrace("main.command.dispatch");
                call->result.set_value(remoteCommand(call->request));
                playbackTrace("main.command.replied");
            } catch (...) {
                call->result.set_value({{"ok", false}, {"error", "Invalid remote command."}});
            }
        }
        for (auto it = retiringRemotes.begin(); it != retiringRemotes.end();) {
            if (it->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                it->get();
                it = retiringRemotes.erase(it);
            } else
                ++it;
        }
        if (remote && (!settings.remoteEnabled || remote->port() != settings.remotePort)) {
            auto now = SDL_GetTicks64();
            if (!reconfigureRemoteAt)
                reconfigureRemoteAt = now + 350;
            if (now < reconfigureRemoteAt) {
                return;
            }
            retireRemote();
            attemptedRemote = false;
            reconfigureRemoteAt = 0;
        } else
            reconfigureRemoteAt = 0;
        if (!settings.remoteEnabled) {
            remote.reset();
            attemptedRemote = false;
            remoteAddress = "Disabled";
            return;
        }
        if (!attemptedRemote || attemptedPort != settings.remotePort) {
            remote.reset();
            attemptedRemote = true;
            attemptedPort = settings.remotePort;
            remote = std::make_unique<RemoteServer>(
                settings.remotePort, [this](const nlohmann::json &j) { return submitRemote(j); });
            if (!remote->valid()) {
                status = remote->error();
                remoteAddress = status;
                remote.reset();
                return;
            }
            remote->albumArtwork([this](const std::string &id){for(auto type:{std::string("station"),std::string("collection"),std::string("playlist")})if(id.rfind(type+"-",0)==0){std::ifstream image(collection->imagePath(type,id.substr(type.size()+1)),std::ios::binary);return std::vector<unsigned char>(std::istreambuf_iterator<char>(image),{});}if(id=="disc")return albumPages.cover("disc");std::ifstream f(collection->coverPath(id),std::ios::binary);auto bytes=std::vector<unsigned char>((std::istreambuf_iterator<char>(f)),{});return bytes.empty()?albumPages.cover(id):bytes;});
            remote->artwork(remoteArt);
            if (remoteArt.empty() && settings.showArtwork)
                artKey.clear();
            lastIpCheck = 0;
            lastPublish = 0;
        }
        if (!remote)
            return;
        auto now = SDL_GetTicks64();
        if (!lastIpCheck || now - lastIpCheck > 10000) {
            lastIpCheck = now;
            auto ip = localNetworkAddress();
            remoteAddress = !ip.empty()
                                ? "http://" + ip + ":" + std::to_string(settings.remotePort)
                                : "Remote server running on port " + std::to_string(settings.remotePort) + " (IP unavailable)";
        }
        auto structureKey=std::to_string(revision)+":"+std::to_string(sourcesRevision)+":"+catalog["revision"].dump()+":"+std::to_string(albumPages.revision())+":"+discSignature+":"+std::to_string(discConnected)+":"+std::to_string(discEjecting)+discEjectError+":"+std::to_string(listening)+remoteAddress;
        bool structural=structureKey!=publishedStructure||!lastPublish;
        if(structural||now-lastPublish>=(remote->wantsLive()?100:1000)){
            lastPublish=now;publishedStructure=std::move(structureKey);
            remote->publish(remoteState(structural),settings,sourcesRevision);
        }
    }
    bool settingDisabled(const nlohmann::json &field)const{
        if(field.value("disabled_with_oled",false)&&settings.theme=="oled")return true;
        auto required=field.value("requires_enabled",std::string{});
        if(required=="background_animation")return !settings.animatedBackground||settings.backgroundAnimation==0;
        if(required=="show_clock")return !settings.showClock;
        return false;
    }
    std::string settingText(const nlohmann::json &field) const {
        std::string key = field["key"], kind = field["kind"];
        if(key=="menu_opacity"||key=="background_speed")return std::to_string(key=="menu_opacity"?settings.menuOpacity:settings.backgroundSpeed)+"%";
        if(key=="collections_view"||key=="radio_view"||key=="playlists_view")return settingsDocument(settings)[key]=="grid"?"Grid":"List";
        if(key=="visualizer_colour")return settings.visualizerColour=="theme"?"Theme colour":settings.visualizerColour;
        if(key=="look")return settings.look=="classic"?"Classic":"Modern";
        if(key=="background_animation")return settings.theme=="oled"?"Unavailable with OLED theme":std::array<const char*,4>{{"Off","Ambient drift","Aurora","Gradient waves"}}[settings.animatedBackground?settings.backgroundAnimation:0];
        if(key=="visualizer_style")return visualizerName(settings.visualizerStyle);
        if(kind=="action"||kind=="manager")return "";
        if(key=="eq_preset")return settingsDocument(settings)[key].get<int>()<0?"Custom":presets[settingsDocument(settings)[key].get<int>()].name;
        if(kind=="gain"){char text[24];std::snprintf(text,sizeof(text),"%+.1f dB",settingsDocument(settings)[key].get<float>());return text;}
        if (kind == "info")
            return settings.remoteEnabled ? remoteAddress : "Disabled";
        if (kind == "colour")
            return accentName();
        auto values = settingsDocument(settings);
        auto value = values[key];
        if (value.is_boolean())
            return value.get<bool>() ? "On" : "Off";
        if (key == "screensaver_seconds") {
            int n = value;
            return n == 0   ? "Off"
                   : n < 60 ? std::to_string(n) + " sec"
                            : std::to_string(n / 60) + " min";
        }
        if (key == "theme")
            return settings.theme == "oled" ? "OLED" : settings.theme == "light" ? "Light" : "Dark";
        return value.is_string() ? value.get<std::string>() : value.dump();
    }
    #include "app_library_settings.inc"
    void settingAction(int direction = 1) {
        if (!settingsNavigation.entries)
            return;
        auto fields = settingFields();
        auto field = fields[settingsNavigation.fieldIndex()];
        std::string key = field["key"], kind = field["kind"];
        if(settingDisabled(field))return;
        if(kind=="manager"){librarySettings(key);return;}
        if(kind=="gain"){float gain=settingsDocument(settings)[key].get<float>();applySettings({{key,std::clamp(gain+direction*.5f,-6.f,6.f)}});return;}
        if(kind=="action"){applySettings({{key,true}});return;}
        if(key=="visualizer_style"||key=="visualizer_sensitivity"||key=="visualizer_smoothing"){visualizerChoice(key);return;}
        if (kind == "info")
            return;
        if (kind == "colour") {
            colourChoices(key);
            return;
        }
        auto values = settingsDocument(settings);
        nlohmann::json value = values[key];
        if (kind == "bool")
            value = !value.get<bool>();
        if (kind == "choice") {
            std::vector<nlohmann::json> options;
            std::vector<std::string> labels;
            for (auto &v : field["choices"]) {
                options.push_back(v);
                if(field.contains("choice_labels")){labels.push_back(field["choice_labels"][labels.size()].get<std::string>());continue;}
                labels.push_back(v.is_string()
                                     ? (v == "oled"    ? "OLED"
                                        : v == "light" ? "Light"
                                        : v == "dark"  ? "Dark"
                                                       : v.get<std::string>())
                                     : (v == 0 ? "Off"
                                        : v.get<int>() < 60
                                            ? std::to_string(v.get<int>()) + " sec"
                                            : std::to_string(v.get<int>() / 60) + " min"));
            }
            choose(field["label"], options, labels, value,
                   [this, key](auto v) { applySettings({{key, v}}); });
            return;
        }
        if (kind == "port") {
            std::string text = std::to_string(settings.remotePort);
            cooldown = SDL_GetTicks64() + 120;
            if (!keyboard("Web remote port (1-65535)", text))
                return;
            if (text.empty() || text.size() > 5 ||
                text.find_first_not_of("0123456789") != std::string::npos) {
                status = "Enter a port from 1 to 65535.";
                return;
            }
            value = std::stoi(text);
        }
        applySettings({{key, value}});
    }
    void editStorageFolder(const StorageDevice &device) {
        if(device.id==-2){status="Insert an audio CD, then open Physical Disc.";return;}
        auto value=storageFolder(settings,device);
        if(!keyboard(device.name+" music folder",value))return;
        cooldown=SDL_GetTicks64()+120;
        if(device.id==-1){
            if(!independent&&!validLocalRoot(value)){status="Choose an SD card folder starting with sdmc:/.";return;}
            applySettings({{"music_root",value}});
        }else {
            auto folders=settings.storageFolders;folders[device.mount]=value;applySettings({{"storage_folders",folders}});
        }
    }
    bool loading = false;
    Share edit;
    int editIndex = -1;
    bool editing = false, running = true;
    SDL_Texture *artTexture = nullptr;
    std::vector<StorageDevice> storageDevices_{{-1,"SD card","sdmc:"}};
    Uint64 cooldown = 0;
    double touchY = -1;
    bool gesture = false;
    ButtonRepeat verticalRepeat;
    void changePage(int p) {
        page = p==0||p==1||p==3||p==4||p==5||p==6||p==7||p==8||p==9||p==10?p:0;
        clientEditing = false;
        controlsFocused = false;
        editing = false;
        colourPicking = false;
        settingsNavigation.entries = false;
        selected = scroll = 0;
        status = "";
    }
    int sourceTabPage=0;
    std::map<int,std::pair<int,int>> tabPositions;
    int mainTab()const{return page==5||page==7?5:page==3?3:page==4?4:page==6?6:page==8?8:page==9||page==10?9:0;}
    void selectTab(int tab){
        if(tab==mainTab())return;
        tabPositions[page]={selected,scroll};
        if(mainTab()==0)sourceTabPage=page;
        int target=tab==0?(sourceTabPage==1&&!sourceSelected?0:sourceTabPage):tab==5&&!collectionAlbumID.empty()?7:tab==9&&!playlistID.empty()?10:tab;
        changePage(target);
        auto remembered=tabPositions.find(target);
        if(remembered!=tabPositions.end()){selected=std::clamp(remembered->second.first,0,maxSelection());scroll=std::min(selected,remembered->second.second);}
    }
    void switchTab(int direction){auto tabs=mainTabs();int at=0;for(int i=0;i<int(tabs.size());i++)if(tabs[i].second==mainTab())at=i;selectTab(tabs[(at+direction+int(tabs.size()))%tabs.size()].second);}
    bool persist() {
        if(independent)return true;
        std::string error;
        bool ok = saveSettings(settingsPath, settings, error);
        if (!ok)
            status = error;
        player.settings(settings);
        if (ok) {
            ++revision;
            lastPublish = 0;
        }
        return ok;
    }
    void browse(Source s, const std::string &path) {
        if(independent){hostBrowse(std::move(s),path);return;}
        albumContextID.clear();
        if (sourceSelected && !loading)
            browsePositions[browseKey()] = {selected, scroll, {}};
        sourceSelected = true;
        controlsFocused = false;
        source = std::move(s);
        folder = path;
        loading = true;
        entries.clear();
        selected = scroll = 0;
        status = source.root == DiscRoot ? "Reading audio CD and metadata..." : "Loading folder...";
        browser.request(source, folder);
        page = 1;
    }
    void openSelected() {
        if (loading || selected < 0 || selected >= int(entries.size()))
            return;
        auto &e = entries[selected];
        if (e.directory) {
            browse(source, e.path);
            return;
        }
        std::vector<Track> tracks;
        int index = 0;
        for (auto &entry : entries)
            if (!entry.directory) {
                if (entry.path == e.path)
                    index = tracks.size();
                tracks.push_back({source, entry.path, source.root == DiscRoot ? entry.name : title(entry.name)});
            }
        player.play(std::move(tracks), index);
        if(settings.autoFullscreen&&!fullscreen)toggleFullscreen();
        status = "";
    }
    void saveShare() {
        if(independent){hostCommand("source_save",{{"index",editIndex},{"share",{{"name",edit.name},{"server",edit.server},{"share",edit.share},{"folder",edit.folder},{"username",edit.user},{"password",edit.password},{"domain",edit.domain},{"guest",edit.guest}}},{"sources_revision",sourcesRevision}});editing=false;selected=scroll=0;return;}
        if (editSourcesRevision != sourcesRevision) {
            status = "Files changed; reopen this editor before saving.";
            return;
        }
        std::string error;
        if (!validate(edit, error)) {
            status = error;
            return;
        }
        if (edit.name.empty())
            edit.name = edit.server + " / " + edit.share;
        auto before = settings;
        if (editIndex < 0)
            settings.shares.push_back(edit);
        else
            settings.shares[editIndex] = edit;
        if (!persist()) {
            settings = before;
            player.settings(settings);
            return;
        }
        editing = false;
        selected = 0;
        scroll = 0;
        sourcesChanged();
        status = "SMB share saved.";
    }
    void editField() {
        cooldown = SDL_GetTicks64() + 120;
        switch (selected) {
        case 0:
            keyboard("Share display name", edit.name);
            break;
        case 1:
            keyboard("SMB server name or IP", edit.server);
            break;
        case 2:
            keyboard("SMB share name", edit.share);
            break;
        case 3:
            keyboard("Subfolder (optional)", edit.folder);
            break;
        case 4:
            if (!edit.guest)
                keyboard("SMB username", edit.user);
            break;
        case 5:
            if (!edit.guest)
                keyboard("SMB password", edit.password, true);
            break;
        case 6:
            keyboard("Domain (optional)", edit.domain);
            break;
        case 7:
            edit.guest = !edit.guest;
            break;
        case 8: {
            std::string error;
            if (!validate(edit, error)) {
                status = error;
                break;
            }
            status = "Testing SMB connection...";
            browser.request(Source{true, edit, edit.folder}, "", 1);
            break;
        }
        case 9:
            saveShare();
            break;
        case 10:
            if(independent){hostCommand("source_remove",{{"index",editIndex},{"sources_revision",sourcesRevision}});editing=false;selected=scroll=0;return;}
            if (editSourcesRevision != sourcesRevision) {
                status = "Files changed; reopen this editor.";
                return;
            }
            if (editIndex >= 0) {
                auto before = settings;
                settings.shares.erase(settings.shares.begin() + editIndex);
                if (!persist()) {
                    settings = before;
                    player.settings(settings);
                    break;
                }
                editing = false;
                selected = 0;
                sourcesChanged();
                status = "SMB share removed.";
            }
            break;
        }
    }
    int maxSelection() const {
        if(page==7||page==8||page==10){const auto &data=albumPage(currentAlbumKey());return data.contains("tracks")?std::max(0,int(data["tracks"].size())-1):0;}
        if(page==5)return libraryID.empty()?std::max(0,int(catalog["libraries"].size())-1):std::max(0,int(libraryAlbums().size())-1);
        if(page==6)return std::max(0,int(catalog["stations"].size())-1);
        if(page==9)return std::max(0,int(catalog["playlists"].size())-1);
        if (page == 4 && independent)return 0;
        if (page == 4)
            return clientEditing ? (editClientIndex < 0 ? 4 : 5)
                                 : int(settings.remoteClients.size()) + 1;
        if (editing)
            return editIndex < 0 ? 9 : 10;
        if (page == 1)
            return std::max(0, int(entries.size()) - 1);
        if (page == 0)
            return std::max(0,int(storageDevices_.size()+settings.shares.size())-1);

        return int(settingFields().size()) - 1;
    }
    void move(int delta) {
        if (optionApply) {
            optionIndex = (optionIndex + delta + int(optionValues.size())) % optionValues.size();
            return;
        }
        if (colourPicking) {
            if (colourSelection < 10)
                colourSelection = delta > 0 ? (colourSelection < 5 ? colourSelection + 5 : 10)
                                            : std::max(0, colourSelection - 5);
            else
                colourSelection = delta < 0 && colourSelection == 10
                                      ? 5
                                      : std::clamp(colourSelection + delta, 10, 15);
            return;
        }
        if(playerSettingsOpen){playerSettingsRow=std::clamp(playerSettingsRow+delta,0,playerSettingsTab?5:7);return;}
        if (fullscreen || controlsFocused) {
            playerVertical(delta);
            return;
        }

        if(page==7||page==8||page==10){int count=maxSelection()+1;selected=(selected+delta+count)%count;scroll=std::clamp(scroll,std::max(0,selected-albumVisibleRows()+1),selected);return;}
        if((page==5||page==6||page==9)&&collectionGrid()){collectionVertical(delta);return;}
        if (page == 3) {
            settingsNavigation.move(delta, layout().rows());
            return;
        }
        int count = maxSelection() + 1;
        selected = (selected + delta % count + count) % count;
        if (selected < scroll)
            scroll = selected;
        if (selected >= scroll + layout().rows())
            scroll = selected - layout().rows() + 1;
    }
    void stopPlayback(){player.stop();if(fullscreen&&settings.exitFullscreenOnStop){playerSettingsOpen=false;visualizerFullscreen=false;toggleFullscreen();}}
    void transport(int index) {
        switch (index) {
        case 0:
            previousTrack();
            break;
        case 1:
            player.pause();
            break;
        case 2:
            nextTrack();
            break;
        case 3:
            stopPlayback();
            break;
        case 4:
            settings.shuffle = !settings.shuffle;
            persist();
            break;
        case 5:
            settings.repeat = (settings.repeat + 1) % 3;
            persist();
            break;
        case 6:
            player.seek(-10);
            break;
        case 7:
            player.seek(10);
            break;
        case 8:
            settings.volume = std::max(0, settings.volume - 5);
            persist();
            break;
        case 9:
            settings.volume = std::min(100, settings.volume + 5);
            persist();
            break;
        }
    }
    void action() {
        if(independent&&page==4&&!optionApply){independentDisconnect=true;return;}
        if (optionApply) {
            auto f = std::move(optionApply);
            optionApply = nullptr;
            f(optionValues[optionIndex]);
            return;
        }
        if(colourPicking){colourAction();return;}
        if(playerSettingsOpen){playerSettingsChange();return;}
        if (page == 4 && !controlsFocused && !fullscreen) {
            clientAction();
            return;
        }
        if (colourPicking) {
            colourAction();
            return;
        }
        if (controlsFocused || fullscreen) {
            playerAction();
            return;
        }
        if(page==7||page==8||page==10){albumPageAction();return;}
        if(page==5||page==6||page==9){collectionAction();return;}
        if (editing) {
            editField();
            return;
        }
        if (page == 1) {
            openSelected();
            return;
        }
        if (page == 0) {
            int localCount=storageDevices_.size();
            if(localCount+settings.shares.size()==0)return;
            if(selected<localCount)browse(Source{false,{},storageFolder(settings,storageDevices_[selected])},"");
            else if(selected==localCount+int(settings.shares.size())){
                edit={};editIndex=-1;editing=true;editSourcesRevision=sourcesRevision;selected=scroll=0;
            }else {
                auto &share=settings.shares[selected-localCount];browse(Source{true,share,share.folder},"");
            }
            return;
        }
        if (page == 3 && !settingsNavigation.entries)
            settingsNavigation.entries = true;
        else
            settingAction();
    }

    void adjust(int dir) {
        if (optionApply) {
            move(dir);
            return;
        }
        if(colourPicking){colourAdjust(dir);return;}
        if(playerSettingsOpen){playerSettingsChange(dir);return;}
        if (fullscreen || controlsFocused) {
            playerHorizontal(dir);
            return;
        }
        if((page==7||page==8||page==10)&&!controlsFocused&&!fullscreen){return;}
        if((page==5||page==6||page==9)&&collectionGrid()&&!controlsFocused&&!fullscreen){int n=maxSelection()+1;selected=settings.wrapNavigation?(selected+dir+n)%n:std::clamp(selected+dir,0,n-1);return;}
        if(page==5||page==6||page==9){move(dir);return;}
        if (!editing && (page == 0 || page == 1)) {
            move(dir * 5);
            return;
        }
        if (colourPicking) {
            colourAdjust(dir);
            return;
        }
        if (!editing && !controlsFocused && page == 3) {
            settingAction(dir);
            return;
        }
        if (controlsFocused) {
            control = (control + dir + 10) % 10;
            return;
        }
        move(dir * 5);
    }

    void back() {
        if(visualizerFullscreen){visualizerFullscreen=false;return;}
        if (optionApply) {
            optionApply = nullptr;
            return;
        }
        if(colourPicking){closeColours(false);return;}
        if(playerSettingsOpen){playerSettingsOpen=false;return;}
        if (clientEditing) {
            clientEditing = false;
            selected = scroll = 0;
            return;
        }
        if (colourPicking) {
            closeColours(false);
            return;
        }
        if (fullscreen) {
            toggleFullscreen();
            return;
        }
        if (controlsFocused) {
            controlsFocused = false;seekerFocused=false;
            return;
        }
        if(page==10){playlistID.clear();page=9;selected=std::clamp(playlistRootSelected,0,maxSelection());scroll=0;return;}
        if(page==7){collectionAlbumID.clear();page=5;selected=std::clamp(collectionTileSelected,0,maxSelection());scroll=std::min(selected,collectionTileScroll);return;}
        if(page==5&&!libraryID.empty()){libraryID.clear();selected=std::clamp(collectionRootSelected,0,maxSelection());scroll=0;return;}
        if(page==8||page==6||page==5||page==9||page==0)return;
        if (page == 3 && settingsNavigation.entries) {
            settingsNavigation.entries = false;
            return;
        }
        if (editing) {
            editing = false;
            selected = scroll = 0;
            status = "";
        } else if (page == 1 && !folder.empty()) {
            auto child = folder;
            browse(source, parent(folder));
            browsePositions[browseKey()].child = child;
        } else {
            page = 0;
            selected = scroll = 0;
        }
    }
    void click(int x, int y) {
        if(visualizerFullscreen){visualizerFullscreen=false;return;}
        if(listening){setListening(false);return;}
        if (connectionOpen || scanOpen) {
            dialogClick(x, y);
            return;
        }
        if (optionApply) {
            int first = layout().optionFirst(optionIndex);
        ui.scrollIndicator(1117,170,layout().optionRows()*48-6,int(optionLabels.size()),first,layout().optionRows());
            if (inside(x, y, 170, 170, 940, layout().optionRows() * 48)) {
                int i = first + (y - 170) / 48;
                if (i < int(optionValues.size())) {
                    optionIndex = i;
                    action();
                }
            }
            return;
        }
        if (colourPicking) {
            x-=198;
            for (int i = 0; i < 10; i++)
                if (inside(x, y, 52 + (i % 5) * 156, 200 + (i / 5) * 54, 144, 44)) {
                    colourSelection = i;
                    colourAction();
                    return;
                }
            for (int i = 0; i < 3; i++)
                if (inside(x, y, 210, 326 + i * 47, 600, 36)) {
                    colourSelection = 10 + i;
                    customRGB[i] = std::clamp((x - 210) * 255 / 600, 0, 255);
                    return;
                }
            if (inside(x, y, 52, 476, 760, 38)) {
                colourSelection = 13;
                colourAction();
                return;
            }
            if (inside(x, y, 52, 531, 360, 44)) {
                colourSelection = 14;
                colourAction();
                return;
            }
            if (inside(x, y, 432, 531, 380, 44)) {
                colourSelection = 15;
                colourAction();
                return;
            }
            return;
        }
        if(playerSettingsOpen){
            if(inside(x,y,168,118,944,46))return;
            if(inside(x,y,170,198,940,(playerSettingsTab?6:8)*43)){playerSettingsRow=(y-198)/43;playerSettingsChange();}return;
        }
        if(canExpandVisualizer()&&visualizerRect().contains(x,y)){visualizerFullscreen=true;return;}
        auto volume=volumeRect();
        if(volume.contains(x,y)){
            controlsFocused=true;playerFocus().row=fullscreen?1:3;playerFocus().column=0;syncPlayerFocus();
            settings.volume=std::clamp((x-volume.x-65)*100/(volume.w-127),0,100);persist();return;
        }
        if(scrubHit(x,y)){
            controlsFocused=true;playerFocus().row=0;playerFocus().column=0;syncPlayerFocus();
            auto p=player.snapshot();if(p.duration>0)player.seek(std::clamp((x-seekRect().x)/double(seekRect().w),0.,1.)*p.duration-p.position);return;
        }
        for(int row=fullscreen?2:1;row<=(fullscreen?2:2);row++)
            for(int col=0;col<PlayerNavigation::count(fullscreen,row);col++)
                if(playerButtonRect(fullscreen,layout().extraHeight(),row,col).contains(x,y)){
                    controlsFocused=true;playerFocus()={row,col};syncPlayerFocus();playerAction();return;
                }
        if(!fullscreen&&!controlsFocused&&!optionApply&&(page==7||page==8||page==10)&&inside(x,y,28,155,828,layout().panelHeight())){
            if(y>=albumTrackTop()){int row=scroll+(y-albumTrackTop())/46;if(row<=maxSelection()){selected=row;albumPageAction();}}return;
        }
        if(!fullscreen&&!controlsFocused&&!optionApply&&(page==5||page==6||page==9)&&inside(x,y,28,182,828,layout().panelHeight()-27)){
            if(collectionGrid()){int first=collectionFirst();int col=(x-44)/202,row=(y-190)/albumTileHeight();if(x<44||y<190||col>3)return;selected=first+row*4+col;if(selected<=maxSelection())collectionAction();}
            else {int first=scroll;selected=first+(y-190)/48;if(y>=190&&selected<=maxSelection())collectionAction();}return;
        }
        if(!fullscreen&&artworkVisible()&&inside(x,y,layout().compactArtX(),182,layout().compactArtDesignSize(),layout().artSize(false))){toggleFullscreen();return;}
        if(fullscreen)return;
        if(inside(x,y,28,90,1224,54)){
            int tab=(x-28)/164;auto tabs=mainTabs();if(tab<int(tabs.size()))selectTab(tabs[tab].second);seekerFocused=false;return;
        }
        if (page == 3) {
            const int row = (y - 182) / 48;
            if (y < 182 || row >= layout().rows()) return;
            if (inside(x,y,28,155,220,layout().panelHeight()) && row+settingsCategoryScroll < int(settingsNavigation.categories.size())) {
                settingsNavigation.category = row+settingsCategoryScroll;
                settingsNavigation.entries = true;
                controlsFocused = seekerFocused = false;
            } else if (inside(x,y,264,155,592,layout().panelHeight())) {
                auto &c = settingsNavigation.categories[settingsNavigation.category];
                const int target = c.scroll + row;
                if (target < int(c.fields.size())) {
                    c.selected = target;
                    settingsNavigation.entries = true;
                    controlsFocused = seekerFocused = false;
                    settingAction();
                }
            }
            return;
        }
        if (inside(x, y, 28, 155, 828, layout().panelHeight())) {
            int row = (y - 182) / 48;
            if (y >= 182 && row < layout().rows()) {
                int target = scroll + row;
                if (target <= maxSelection()) {
                    selected = target;
                    controlsFocused = false;
                    action();
                }
            }
        }
    }

    std::string trackTitle(const Playback &p) const {
        if(p.track.source.root==RadioRoot&&p.busy)return "Loading station...";
        return !p.details.title.empty() ? p.details.title
               : p.track.name.empty()   ? "Nothing playing"
                                        : p.track.name;
    }
    std::string trackAlbum(const Playback &p) const {
        if(p.track.source.root==RadioRoot)return p.track.name;
        return !p.details.album.empty() ? p.details.album
               : p.track.path.empty()   ? "Local + USB + SMB + audio CD"
                                        : filename(parent(p.track.path));
    }
    void drawBadges(const Playback &p, int x, int y, int width, int size = 18) {
        std::vector<std::string> badges;
        if (!p.details.format.empty())
            badges.push_back(p.details.format);
        if (p.details.bitDepth)
            badges.push_back(std::to_string(p.details.bitDepth) + " bit");
        if (p.details.sampleRate) {
            TrackDetails rate;
            rate.sampleRate = p.details.sampleRate;
            badges.push_back(audioDetails(rate));
        } else if (!p.track.path.empty())
            badges.push_back(p.busy ? "Loading sample rate" : "Sample rate unavailable");
        if (p.details.bitRate)
            badges.push_back(std::to_string(p.details.bitRate) + " kbps");
        if(independent)badges=hostState.value("playback",nlohmann::json::object()).value("badges",std::vector<std::string>{});
        int bx = x, by = y;
        for (auto &badge : badges) {
            int w = ui.textWidth(badge, size) + 20;
            if (bx != x && bx + w > x + width) {
                bx = x;
                by += size + 16;
            }
            ui.badge(bx, by, w, size + 14, badge, size);
            bx += w + 8;
        }
    }
    void drawCompact(const Playback &p){
        ui.rect(876,155,376,layout().panelHeight(),PANEL);
        if(artworkVisible())ui.image(artTexture,layout().compactArtX(),182,layout().compactArtDesignSize());
        ui.marquee(900,artworkVisible()?layout().compactTitleY():190,trackTitle(p),22,TEXT,328,"compact-title");
        ui.text(900,artworkVisible()?layout().compactTitleY()+34:228,!p.details.artist.empty()?p.details.artist:trackAlbum(p),16,MUTED,328);
        drawBadges(p,900,artworkVisible()?layout().compactTitleY()+65:265,328,12);
        drawVisualizer(p);
    }
    void drawFullscreen(const Playback &p) {
        if(settings.fullPlayerCard)ui.roundedRect(64,126,1136,422+layout().extraHeight(),PANEL,16);
        if(artworkVisible()){int left=settings.fullPlayerCard?84:64;auto volume=volumeRect();ui.imageWithin(artTexture,left,142,playerX()-left-24,volume.y+volume.h-142);}
        int x=playerX(),width=playerWidth();
        ui.text(x,142,"NOW PLAYING"+(p.count>0?"  "+std::to_string(p.index+1)+"/"+std::to_string(p.count):std::string{}),15,MUTED,width);
        ui.marquee(x,180,trackTitle(p),34,TEXT,width,"fullscreen-title");
        ui.text(x,238,p.details.artist,25,ACCENT,width);
        ui.text(x,282,trackAlbum(p),22,MUTED,width);
        if(!(listening&&settings.listeningHideMetadata))drawBadges(p,x,322,width,16);
        drawVisualizer(p);

    }
    void drawVisualizer(const Playback &p){
        Color ACCENT=settings.visualizerColour=="theme"?nxh::ACCENT:readableAccent(rgbColour(settings.visualizerColour),BG);
        if(!settings.visualizer)return;
        int x=fullscreen?playerX():900,w=fullscreen?playerWidth():328;
        int y=fullscreen?visualizerRect().y:392+layout().extraHeight(),h=fullscreen?visualizerRect().h:22;
        UI::Clip clip(ui,x,y,w,h);
        for(int i=0;i<32;i++){
            int bar=std::max(2,int(std::clamp(p.spectrum[i],0.f,1.f)*h));
            if(fullscreen)ui.recordOnlyRect(x+i*w/32,y+h-bar,std::max(3,w/32-4),bar,ACCENT);
            else ui.rect(x+i*w/32,y+h-bar,std::max(3,w/32-4),bar,ACCENT);
        }
    }
    void drawSeek(const Playback &p){
        auto a=seekRect();bool focus=!listening&&(controlsFocused||fullscreen)&&playerFocus().seek();
        if(focus){ui.roundedRect(a.x-5,a.y-5,a.w+10,a.h+10,ACCENT,10);ui.roundedRect(a.x-3,a.y-3,a.w+6,a.h+6,PANEL,8);}
        double position=scrubbing?scrubPosition:p.position;
        int y=a.y+12;ui.rect(a.x,y,a.w,5,LINE);
        if(p.duration>0){int offset=int(a.w*std::clamp(position/p.duration,0.,1.));ui.rect(a.x,y,offset,5,ACCENT);ui.rect(a.x+std::clamp(offset,4,a.w-4)-4,y-4,8,13,ACCENT);}
        ui.text(a.x,a.y+30,timeLabel(position),fullscreen?18:14,MUTED);
        auto duration=p.track.source.root==RadioRoot?std::string("LIVE"):p.duration>0?(p.details.durationEstimated?"~":"")+timeLabel(p.duration):"--:--";
        ui.rightText(a.x+a.w,a.y+30,duration,fullscreen?18:14,MUTED);
    }
    void drawPlayerButtons(const Playback &p){
        const char *labels[]={"Previous","Play/Pause","Next","Stop","Shuffle","Repeat","EQ / Visualizer","Fullscreen"};
        for(int row=fullscreen?2:1;row<=2;row++)for(int col=0;col<PlayerNavigation::count(fullscreen,row);col++){
            PlayerNavigation item{row,col};int id=item.action(fullscreen);
            auto a=playerButtonRect(fullscreen,layout().extraHeight(),row,col);
            bool focus=(fullscreen||controlsFocused)&&playerFocus().row==row&&playerFocus().column==col;
            bool active=(id==4&&settings.shuffle)||(id==5&&settings.repeat);
            ui.roundedRect(a.x,a.y,a.w,a.h,focus?FOCUS:PANEL);
            ui.transportIcon(a.x+a.w/2,a.y+a.h/2,fullscreen?(id==1?52:44):(id==1?42:34),id==6?10:id==7?11:id,p.playing&&!p.paused,active?ACCENT:TEXT,settings.repeat==2);
            
        }
        auto a=volumeRect();bool focus=(controlsFocused||fullscreen)&&playerFocus().volume(fullscreen);
        if(focus)ui.roundedRect(a.x-4,a.y-4,a.w+8,a.h+8,FOCUS,10);
        ui.text(a.x,a.y,"Volume",14,focus?ACCENT:MUTED);
        ui.rect(a.x+65,a.y+10,a.w-127,4,LINE);ui.rect(a.x+65,a.y+10,(a.w-127)*settings.volume/100,4,ACCENT);
        ui.text(a.x+a.w-54,a.y,std::to_string(settings.volume)+"%",14,focus?ACCENT:MUTED,54);
    }
    void drawColours() {
        ui.rect(0,0,1280,layout().height(),{0,0,0,80});
        ui.roundedRect(198+(28), 155, 828, layout().panelHeight(), {PANEL.r,PANEL.g,PANEL.b,250},18);
        ui.text(198+(46), 160, colourTarget=="visualizer_colour"?"CUSTOM VISUALIZER COLOUR":"CUSTOM ACCENT COLOUR", 15, MUTED);
        for (int i = 0; i < 10; i++) {
            int x = 52 + (i % 5) * 156, y = 200 + (i / 5) * 54;
            Color c = rgbColour(accentPresets[i].hex);
            ui.rect(198+(x), y, 144, 44, colourSelection == i ? FOCUS : BG);
            ui.rect(198+(x + 8), y + 9, 25, 25, c);
            ui.text(198+(x + 42), y + 12, accentPresets[i].name, 17, TEXT, 96);

        }
        ui.text(198+(52), 310, "CUSTOM", 14, MUTED);
        const char *channels[] = {"Red", "Green", "Blue"};
        Color bars[] = {{239, 68, 68}, {34, 197, 94}, {59, 130, 246}};
        for (int i = 0; i < 3; i++) {
            int y = 326 + i * 47;
            if (colourSelection == 10 + i)
                ui.rect(198+(44), y - 1, 780, 38, FOCUS);
            ui.text(198+(52), y + 6, std::string(channels[i]) + "  " + std::to_string(customRGB[i]), 18,
                    TEXT, 145);
            ui.rect(198+(210), y + 15, 600, 5, LINE);
            ui.rect(198+(210), y + 15, customRGB[i] * 600 / 255, 5, bars[i]);
            ui.rect(198+210+std::clamp(customRGB[i]*600/255,5,595)-5,y+8,10,20,TEXT);
        }
        ui.rect(198+(52), 476, 760, 38, colourSelection == 13 ? FOCUS : BG);
        ui.rect(198+(64), 482, 24, 24, rgbColour(colourHex(customRGB)));
        ui.text(198+(103), 483, "Custom hex  " + colourHex(customRGB), 18, TEXT, 690);
        ui.button(198+(52), 531, 360, 44, "Save colour", colourSelection == 14);
        ui.button(198+(432), 531, 380, 44, "Cancel", colourSelection == 15);
    }
    void drawControls(const Playback &p,const std::string &message){
        if(!colourPicking){drawSeek(p);if(!listening)drawPlayerButtons(p);}
        // Status/errors stay visible outside the compact controls-only footer.
        if(!message.empty())ui.text(fullscreen?30:600,fullscreen?90:64,message,13,ACCENT,fullscreen?1210:620);
        if(listening&&settings.listeningHideFooter)return;
        ui.rect(0,layout().footerY(),1280,layout().footerHeight(),FOOTER);
        int y=layout().footerY()+8;std::vector<std::pair<std::string,std::string>> hints;auto flush=[&]{ui.centeredHints(y,hints);};
        auto hint=[&](const char *button,const std::string &action){hints.emplace_back(button,action);};
        if(listening){hint("Plus","Play/Pause");hint("L","");hint("R","Track");hint("ZL","");hint("ZR","Seek 5s");hint("L3","Exit");flush();return;}
        if(optionApply){hint("Dpad","Navigate");hint("A","Select");hint("B","Back");flush();return;}
        if(playerSettingsOpen&&!colourPicking){hint("L","");hint("R","Tabs");hint("Dpad","Adjust");hint("A","Choose");hint("B","Close");flush();return;}
        if(colourPicking){hint("Dpad","Colour");hint("A","Choose");hint("B","Cancel");flush();return;}
        if(fullscreen||controlsFocused){
            const char *labels[]={"Previous","Play/Pause","Next","Stop","Shuffle","Repeat","EQ / Visualizer","Fullscreen"};
            auto &focus=playerFocus();int id=focus.action(fullscreen);
            hint("Dpad",focus.seek()?"Seek 10s":focus.volume(fullscreen)?"Volume":"Navigate");
            if(fullscreen&&fullFocus.row==-1)hint("A","Expand visualizer");
            else if(id>=0)hint("A",labels[id]);
            hint("B",fullscreen?"Close":"Browse");
        }else{hint("Dpad","Navigate");hint("A",page==3?(settingsNavigation.entries?"Change":"Select"):editing?"Edit":"Open");hint("B",page==3&&settingsNavigation.entries?"Categories":"Back");if(page==0)hint("X","Manage");if(page==1)hint("X","Track / album actions");if(page==5||page==6||page==9)hint("X","Manage");if(page==7||page==8||page==10)hint("X","Album actions");}
        if(!fullscreen){hint("Minus","Player");hint("L","");hint("R","Tabs");hint("R3","Fullscreen");}
        hint("Plus","Play/Pause");
        if(fullscreen){hint("L","");hint("R","Track");hint("ZL","");hint("ZR","Seek 5s");hint("L3","Listening");}
        flush();
    }

  public:
    App(UI &u, SDL_Renderer *r, Player &p, Settings s, std::string error,
        std::string settingsFile = ConfigPath)
        : ui(u), renderer(r), player(p), settings(std::move(s)),
          settingsPath(std::move(settingsFile)), status(std::move(error)) {
        mkdir(parent(settingsPath).c_str(),0777);
        collection=std::make_unique<CollectionStore>(parent(settingsPath));catalog=collection->snapshot();
        source = {false, {}, settings.musicRoot};
        page = status.empty() ? 0 : 3;
        setAppearance(settings.theme, settings.accent,settings.menuOpacity,settings.look);
    }
    ~App() {
        if(tileWork.valid())tileWork.get();
        probeCancel = true;
        scanner.cancel();
        disconnectTarget();
        for (auto &f : retiredClients)
            f.get();
        remote.reset();
        for (auto &retired : retiringRemotes)
            retired.wait();
        for(auto &entry:albumTextures)if(entry.second.second)ui.releaseCover(entry.second.second);
        if (artTexture)
            ui.releaseCover(artTexture);
    }
    bool alive() const {
        return running;
    }
    void displayChanged() {
        // Cancel only in-progress pointer gestures; preserve music, pages and focus.
        scrubbing = mouseScrub = mouseColourDrag = false;
        colourDrag = -1;
        touchY = clientTouchY = -1;
        gesture = clientGesture = true;
        layout().keepVisible(selected, scroll, maxSelection() + 1);
        if(nativeTarget)nativeTarget->displayChanged();
        if (targetView)
            targetView->displayChanged();
    }
    void input(PadState &pad, u64 injectedDown = 0, u64 injectedHeld = 0, bool injected = false,bool forwarded=false) {
        if (!injected&&!forwarded)
            padUpdate(&pad);
        auto down = (injected||forwarded) ? injectedDown : padGetButtonsDown(&pad),
             held = (injected||forwarded) ? injectedHeld : padGetButtons(&pad);
        if(nativeTarget){nativeTarget->input(pad,down,held,injected,true);return;}
        if (dialogInput(down, held, injected))
            return;
        if (targetView) {
            if (SDL_GetTicks64() < cooldown) {
                return;
            }
            auto mapButtons = [](u64 buttons) {
                unsigned out = 0;
                const u64 masks[] = {HidNpadButton_A,
                                     HidNpadButton_B,
                                     HidNpadButton_X,
                                     HidNpadButton_Y,
                                     HidNpadButton_Minus,
                                     HidNpadButton_Plus,
                                     HidNpadButton_L,
                                     HidNpadButton_R,
                                     HidNpadButton_ZL,
                                     HidNpadButton_ZR,
                                     HidNpadButton_Up | HidNpadButton_StickLUp,
                                     HidNpadButton_Down | HidNpadButton_StickLDown,
                                     HidNpadButton_Left,
                                     HidNpadButton_Right,
                                     HidNpadButton_StickR};
                for (int i = 0; i < 15; ++i)
                    if (buttons & masks[i])
                        out |= 1u << i;
                return out;
            };
            targetView->buttons(mapButtons(down), mapButtons(held));
            SDL_Event e;
            while (!injected && SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT)
                    running = false;
                if (targetView->screenMode()) {
                    if (e.type == SDL_FINGERDOWN)
                        targetView->screenPointer("down", e.tfinger.x * 1280,
                                                  e.tfinger.y * layout().height());
                    if (e.type == SDL_FINGERMOTION)
                        targetView->screenPointer("move", e.tfinger.x * 1280,
                                                  e.tfinger.y * layout().height());
                    if (e.type == SDL_FINGERUP)
                        targetView->screenPointer("up", e.tfinger.x * 1280,
                                                  e.tfinger.y * layout().height());
                    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.which != SDL_TOUCH_MOUSEID)
                        targetView->screenPointer("down", ui.pointerX(e.button.x), e.button.y);
                    if (e.type == SDL_MOUSEMOTION && (e.motion.state & SDL_BUTTON_LMASK))
                        targetView->screenPointer("move", ui.pointerX(e.motion.x), e.motion.y);
                    if (e.type == SDL_MOUSEBUTTONUP && e.button.which != SDL_TOUCH_MOUSEID)
                        targetView->screenPointer("up", ui.pointerX(e.button.x), e.button.y);
                    continue;
                }
                if (e.type == SDL_FINGERDOWN) {
                    clientTouchY = e.tfinger.y * layout().height();
                    clientGesture =
                        targetView->beginScrub(e.tfinger.x * 1280, e.tfinger.y * layout().height());
                    if (clientGesture)
                        clientTouchY = -1;
                }
                if (e.type == SDL_FINGERMOTION && targetView->scrubActive())
                    targetView->previewScrub(e.tfinger.x * 1280);
                if (e.type == SDL_FINGERMOTION && clientTouchY >= 0) {
                    double y = e.tfinger.y * layout().height();
                    if (std::abs(y - clientTouchY) > 32) {
                        targetView->swipe(y < clientTouchY ? 1 : -1);
                        clientTouchY = y;
                        clientGesture = true;
                    }
                }
                if (e.type == SDL_FINGERUP) {
                    if (targetView->scrubActive())
                        targetView->drag(e.tfinger.x * 1280);
                    else if (!clientGesture)
                        targetView->click(e.tfinger.x * 1280, e.tfinger.y * layout().height());
                    clientTouchY = -1;
                }
                if (e.type == SDL_MOUSEBUTTONDOWN && e.button.which != SDL_TOUCH_MOUSEID) {
                    if (!targetView->beginScrub(ui.pointerX(e.button.x), e.button.y))
                        targetView->click(ui.pointerX(e.button.x), e.button.y);
                }
                if (e.type == SDL_MOUSEMOTION && targetView->scrubActive())
                    targetView->previewScrub(ui.pointerX(e.motion.x));
                if (e.type == SDL_MOUSEBUTTONUP && e.button.which != SDL_TOUCH_MOUSEID &&
                    targetView->scrubActive())
                    targetView->drag(ui.pointerX(e.button.x));
                if (e.type == SDL_MOUSEWHEEL)
                    targetView->swipe(-e.wheel.y);
            }
            return;
        }
        if (down || held) {
            bool wasSleeping = sleeping;
            wake();
            if (wasSleeping)
                return;
        }
        // Consume the exit press and every held input until its release.
        listeningBlocked &= held;
        down &= ~listeningBlocked;held &= ~listeningBlocked;
        if(listening){
            constexpr u64 playback=HidNpadButton_Plus|HidNpadButton_L|HidNpadButton_R|HidNpadButton_ZL|HidNpadButton_ZR;
            if(down&~playback){listeningBlocked=held|down;setListening(false);return;}
            if(down&HidNpadButton_Plus)player.pause();
            if(down&HidNpadButton_L)previousTrack();
            if(down&HidNpadButton_R)nextTrack();
            if(down&HidNpadButton_ZL)player.seek(-5);
            if(down&HidNpadButton_ZR)player.seek(5);
            SDL_Event event;while(SDL_PollEvent(&event))if(event.type==SDL_QUIT)running=false;
            return;
        }
        if(visualizerFullscreen){
            int direction=(held&HidNpadButton_Left)?-1:(held&HidNpadButton_Right)?1:0;
            if(horizontalRepeat.fire(direction,down&(HidNpadButton_Left|HidNpadButton_Right),SDL_GetTicks64()))cycleVisualizer(direction);
            if(down&HidNpadButton_B){visualizerFullscreen=false;return;}
            if(down&HidNpadButton_Plus)player.pause();
            if(down&HidNpadButton_L)previousTrack();if(down&HidNpadButton_R)nextTrack();
            if(down&HidNpadButton_ZL)player.seek(-5);if(down&HidNpadButton_ZR)player.seek(5);
            SDL_Event event;while(SDL_PollEvent(&event)){if(event.type==SDL_QUIT)running=false;else if(event.type==SDL_MOUSEBUTTONDOWN||event.type==SDL_FINGERDOWN)visualizerFullscreen=false;}return;
        }
        if(fullscreen&&!playerSettingsOpen&&!colourPicking&&!optionApply&&(down&HidNpadButton_StickL)){
            setListening(true);listeningBlocked=held|down;return;
        }
        if(playerSettingsOpen&&!colourPicking&&(down&(HidNpadButton_L|HidNpadButton_R))){
            optionApply=nullptr;playerSettingsTab=1-playerSettingsTab;playerSettingsRow=0;return;
        }
        if (down & HidNpadButton_Plus)
            player.pause();
        if (SDL_GetTicks64() < cooldown) {
            wake();
            return;
        }
        if (!playerSettingsOpen && !colourPicking && !optionApply && (down & HidNpadButton_StickR))
            toggleFullscreen();

        if (!fullscreen && !colourPicking && !optionApply && (down & HidNpadButton_L)) {
            fullscreen = false;
            switchTab(-1);
        }
        if (!fullscreen && !colourPicking && !optionApply && (down & HidNpadButton_R)) {
            fullscreen = false;
            switchTab(1);
        }
        bool up = held & (HidNpadButton_Up | HidNpadButton_StickLUp),
             dn = held & (HidNpadButton_Down | HidNpadButton_StickLDown);
        int vertical=up?-1:dn?1:0;
        if(verticalRepeat.fire(vertical,down&(HidNpadButton_Up|HidNpadButton_Down|HidNpadButton_StickLUp|HidNpadButton_StickLDown),SDL_GetTicks64()))
            move(vertical);
        if (down & HidNpadButton_A)
            action();
        if (down & HidNpadButton_B)
            back();
        bool left = held & HidNpadButton_Left, right = held & HidNpadButton_Right;
        int horizontal=left?-1:right?1:0;
        if(horizontalRepeat.fire(horizontal,down&(HidNpadButton_Left|HidNpadButton_Right),SDL_GetTicks64()))
            adjust(horizontal);
        if (down & (fullscreen ? HidNpadButton_L : HidNpadButton_ZL))
            previousTrack();
        if (down & (fullscreen ? HidNpadButton_R : HidNpadButton_ZR))
            nextTrack();
        if(fullscreen&&(down&HidNpadButton_ZL))player.seek(-5);
        if(fullscreen&&(down&HidNpadButton_ZR))player.seek(5);
        if (!fullscreen && !colourPicking && !optionApply && (down & HidNpadButton_Minus)) {
            controlsFocused=!controlsFocused;syncPlayerFocus();
        }
        if (!colourPicking && (down & HidNpadButton_X)) {
            if(!fullscreen&&!controlsFocused&&(page==7||page==8||page==10)){albumActions();return;}
            if(!fullscreen&&!controlsFocused&&(page==5||page==6||page==9)){collectionManage();return;}
            if(!fullscreen&&!controlsFocused&&page==1&&!editing){fileActions();return;}
            if (!fullscreen && page == 4 && !clientEditing && !independent) {
                beginClientEdit(selected < int(settings.remoteClients.size()) ? selected : -1);
                return;
            }
            if (!fullscreen && page == 0 && !editing) {
                sourceActions();
            } else
                player.stop();
        }
        if (down & HidNpadButton_Y) {
            settings.shuffle = !settings.shuffle;
            persist();
        }
        SDL_Event e;
        while (!injected && SDL_PollEvent(&e)) {
            bool pointer = e.type == SDL_FINGERDOWN || e.type == SDL_FINGERUP ||
                           e.type == SDL_FINGERMOTION || e.type == SDL_MOUSEBUTTONDOWN ||
                           e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEMOTION;
            if (pointer) {
                if (sleeping) {
                    wake();
                    wakeGesture = e.type == SDL_FINGERDOWN || e.type == SDL_MOUSEBUTTONDOWN;
                    continue;
                }
                wake();
                if (wakeGesture) {
                    if (e.type == SDL_FINGERUP || e.type == SDL_MOUSEBUTTONUP)
                        wakeGesture = false;
                    continue;
                }
            }
            if (e.type == SDL_QUIT)
                running = false;
            if (e.type == SDL_FINGERDOWN) {
                touchY = e.tfinger.y * layout().height();
                gesture = false;
                if (beginColourDrag(e.tfinger.x * 1280, e.tfinger.y * layout().height())) {
                    colourFinger = e.tfinger.fingerId;
                    gesture = true;
                }
                if (!colourPicking &&
                    beginScrub(e.tfinger.x * 1280, e.tfinger.y * layout().height())) {
                    scrubFinger = e.tfinger.fingerId;
                    gesture = true;
                }
            }
            if (e.type == SDL_FINGERMOTION && colourDrag >= 0 && !mouseColourDrag &&
                e.tfinger.fingerId == colourFinger)
                dragColour(e.tfinger.x * 1280);
            if (e.type == SDL_FINGERMOTION && scrubbing && !mouseScrub &&
                e.tfinger.fingerId == scrubFinger)
                previewScrub(e.tfinger.x * 1280);
            if (!fullscreen && !colourPicking && e.type == SDL_FINGERMOTION && touchY >= 182 &&
                touchY < layout().bottom(600) && e.tfinger.x * 1280 < 856) {
                float dy = e.tfinger.y * layout().height() - touchY;
                if (std::abs(dy) > 35) {
                    move(dy < 0 ? 1 : -1);
                    touchY = e.tfinger.y * layout().height();
                    gesture = true;
                }
            }
            if (e.type == SDL_FINGERUP) {
                if (colourDrag >= 0 && !mouseColourDrag && e.tfinger.fingerId == colourFinger) {
                    dragColour(e.tfinger.x * 1280);
                    colourDrag = -1;
                } else if (scrubbing && !mouseScrub && e.tfinger.fingerId == scrubFinger)
                    finishScrub(e.tfinger.x * 1280);
                else if (!gesture)
                    click(e.tfinger.x * 1280, e.tfinger.y * layout().height());
                touchY = -1;
            }
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.which != SDL_TOUCH_MOUSEID) {
                if (beginColourDrag(ui.pointerX(e.button.x), e.button.y))
                    mouseColourDrag = true;
                else if (!colourPicking && beginScrub(ui.pointerX(e.button.x), e.button.y))
                    mouseScrub = true;
                else
                    click(ui.pointerX(e.button.x), e.button.y);
            }
            if (e.type == SDL_MOUSEMOTION && colourDrag >= 0 && mouseColourDrag)
                dragColour(ui.pointerX(e.motion.x));
            if (e.type == SDL_MOUSEBUTTONUP && e.button.which != SDL_TOUCH_MOUSEID &&
                colourDrag >= 0 && mouseColourDrag) {
                dragColour(ui.pointerX(e.button.x));
                colourDrag = -1;
                mouseColourDrag = false;
            }
            if (e.type == SDL_MOUSEMOTION && scrubbing && mouseScrub)
                previewScrub(ui.pointerX(e.motion.x));
            if (e.type == SDL_MOUSEBUTTONUP && e.button.which != SDL_TOUCH_MOUSEID && scrubbing &&
                mouseScrub)
                finishScrub(ui.pointerX(e.button.x));
            if (e.type == SDL_RENDER_DEVICE_RESET || e.type == SDL_RENDER_TARGETS_RESET) {
                ui.clear();
                artKey = "";
            }
        }
    }
    Uint64 storagePollAt=0;std::vector<StorageDevice> cachedDevices;
    void update() {
        if(independent){updateIndependent();return;}
        if(nativeTarget){nativeTarget->update();if(nativeTarget->independentDisconnect){auto message=nativeTarget->status;closeNativeRemote();status=std::move(message);}}
        if(collection&&SDL_GetTicks64()-catalogAt>500){catalogAt=SDL_GetTicks64();refreshCatalog();}
        if(!storagePollAt||SDL_GetTicks64()-storagePollAt>=500){cachedDevices=detectedStorageDevices();storagePollAt=SDL_GetTicks64();}
        auto devices=cachedDevices;
        bool connected=std::any_of(devices.begin(),devices.end(),[](const auto &device){return device.id==-2;});
        if(connected!=discConnected){discConnected=connected;lastPublish=0;discSignature.clear();albumPages.invalidate("disc");discProbeAt=0;if(!connected&&page==8)selectTab(0);}
        devices.erase(std::remove_if(devices.begin(),devices.end(),[](const auto &device){return device.id==-2;}),devices.end());
        updateDiscPage();
        if(devices!=storageDevices_){
            int focused=selected<int(storageDevices_.size())?storageDevices_[selected].id:0;
            int shareOffset=selected-int(storageDevices_.size());
            bool onSources=page==0&&!editing;
            storageDevices_=std::move(devices);++sourcesRevision;lastPublish=0;
            bool removable=source.root==DiscRoot||source.root.rfind("/mnt/",0)==0;
            bool present=std::any_of(storageDevices_.begin(),storageDevices_.end(),[&](const auto &d){
                return d.id==-2?source.root==DiscRoot:source.root==d.mount||source.root.rfind(d.mount+"/",0)==0;
            });
            if(page==1&&!source.smb&&removable&&!present){
                browser.cancel();loading=false;sourceSelected=false;entries.clear();folder.clear();changePage(0);status="Storage device disconnected.";
            }
            if(onSources){
                if(focused){for(size_t i=0;i<storageDevices_.size();i++)if(storageDevices_[i].id==focused)selected=int(i);}
                else selected=int(storageDevices_.size())+std::max(0,shareOffset);
                selected=std::clamp(selected,0,maxSelection());layout().keepVisible(selected,scroll,maxSelection()+1);
            }
        }
        for (auto it = retiredClients.begin(); it != retiredClients.end();) {
            if (it->wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
                it->get();
                it = retiredClients.erase(it);
            } else
                ++it;
        }
        if (targetView) {
            targetView->update();
            if (targetView->disconnect)
                disconnectTarget();
            else if (targetView->changeMode) {
                auto c = targetView->config();
                disconnectTarget();
                beginConnect(c);
            } else if (targetView->switchClient >= 0)
                connectTarget(targetView->switchClient);
        }
        updateConnections();
        ui.recordScreen(false);
        updateRemote();
        sleeping = !listening && !visualizerFullscreen && settings.screensaverSeconds > 0 &&
                   SDL_GetTicks64() - lastInteraction >= Uint64(settings.screensaverSeconds) * 1000;
        player.tick();
        LoadResult result;
        if (browser.poll(result)) {
            if (result.kind == 1) {
                status = result.ok ? "SMB connected: folder is readable." : result.error;
            } else {
                int activeSelected=selected,activeScroll=scroll;
                loading = false;
                entries = std::move(result.entries);
                source = result.source;
                folder = result.path;
                selected = scroll = 0;
                auto remembered = browsePositions.find(browseKey());
                if (remembered != browsePositions.end()) {
                    selected = remembered->second.selected;
                    scroll = remembered->second.scroll;
                    for (int i = 0; i < int(entries.size()); ++i)
                        if (entries[i].path == remembered->second.child)
                            selected = i;
                    selected = std::clamp(selected, 0, std::max(0, int(entries.size()) - 1));
                    scroll =
                        std::clamp(scroll, std::max(0, selected - layout().rows() + 1), selected);
                }
                if(page!=1){tabPositions[1]={selected,scroll};selected=activeSelected;scroll=activeScroll;}
                status = result.ok
                             ? (entries.empty() ? "No supported music files or folders here." : "")
                             : result.error;
            }
        }
        auto p = player.snapshot();
        std::string metadataKey=p.track.source.share.server+"/"+p.track.source.root+"/"+p.track.path;
        if(p.playing&&!p.busy&&!p.track.path.empty()&&metadataKey!=tagKey){tagKey=metadataKey;if(lower(p.track.path).size()>=5&&lower(p.track.path).substr(p.track.path.size()-5)==".flac")tags.request(p.track.source,p.track.path,3);else tags.cancel();}
        LoadResult tagResult;if(tags.poll(tagResult)&&tagResult.ok)player.metadata(tagResult.details,tagResult.source,tagResult.path);
        Source artworkSource=p.track.source;int stationArtRevision=0;if(p.track.source.root==RadioRoot)for(auto &station:catalog["stations"])if(station["url"]==p.track.path){artworkSource.share.folder=station.value("logo",std::string{});artworkSource.share.domain=collection->imagePath("station",station["id"]);stationArtRevision=station.value("image_revision",0);break;}
        auto key = std::to_string(stationArtRevision)+artworkSource.share.folder+"/"+p.track.source.share.server + "/" + p.track.source.share.share + "/" +
                   p.track.source.root + "/" + p.track.path +
                   (settings.preferFolderArt ? "/folder-first" : "/embedded-first");
        if(auto album=playbackAlbum(p))key+="/album-art-"+std::to_string(album->value("cover_revision",0));
        if (!settings.showArtwork) {
            if (artTexture) {
                ui.releaseCover(artTexture);
                artTexture = nullptr;
            }
            artKey = "";
            if (!remoteArt.empty()) {
                remoteArt.clear();
                if (remote)
                    remote->artwork({});
            }
        }
        if (settings.showArtwork && !p.track.path.empty() && key != artKey && (p.track.source.root==RadioRoot||!p.busy)) {
            artKey = key;
            ui.resetMarquees();
            scrubbing = false;
            mouseScrub = false;
            if (artTexture) {
                ui.releaseCover(artTexture);
                artTexture = nullptr;
            }
            remoteArt.clear();
            if (remote)
                remote->artwork({});
            playbackTrace("main.artwork.request");
            art.request(artworkSource, p.track.path, 2, settings.preferFolderArt);
        }
        if (art.poll(result) && settings.showArtwork) {
            if(result.cover.empty())if(auto album=playbackAlbum(p)){std::ifstream f(collection->coverPath(album->at("id")),std::ios::binary);result.cover.assign(std::istreambuf_iterator<char>(f),{});}
            playbackTrace("main.artwork.decode.begin", result.cover.size());
            if (artTexture)
                ui.releaseCover(artTexture);
            remoteArt.clear();
            artTexture = result.surface?ui.coverPrepared(result.surface.get(),result.encoded,settings.remoteEnabled?&remoteArt:nullptr):ui.cover(result.cover, settings.remoteEnabled ? &remoteArt : nullptr);
            playbackTrace("main.artwork.decode.end", remoteArt.size());
            if (remote)
                remote->artwork(remoteArt);
            scrubbing = false;
            mouseScrub = false;
        }
    }
    void draw(){
        if(nativeTarget){
            try{nativeTarget->draw();}
            catch(const std::exception &error){nativeTarget->remoteDiagnostic("render-error");nativeTarget->status=std::string("Remote display error: ")+error.what();nativeTarget->independentDisconnect=true;}
            return;
        }
        draw(player.snapshot());
    }
    void draw(const Playback &incoming) {
        if(nativeTarget){nativeTarget->draw();return;}
        setAppearance(settings.theme,settings.accent,settings.menuOpacity,settings.look);
        auto p=libraryPlayback(incoming);
        frameDiagnostics.view=visualizerFullscreen?"visualizer":fullscreen?"fullscreen":"menu";
        if (connectionOpen) {
            drawConnection();
            return;
        }
        if (scanOpen) {
            drawScan();
            return;
        }
        if (targetView) {
            targetView->draw();
            finishFrame(false);
            return;
        }
        setAppearance(settings.theme, colourPicking&&colourTarget=="accent_colour" ? colourHex(customRGB) : settings.accent,settings.menuOpacity,settings.look);
        ui.setBackgroundAnimation(settings.animatedBackground?settings.backgroundAnimation:0);ui.setBackgroundSpeed(settings.backgroundSpeed);
        ui.begin();
        if (sleeping) {
            ui.rect(0, 0, 1280, layout().height(), {0, 0, 0});
            if (settings.showClock)
                ui.text(1060, 32, clock(), 22, {100, 100, 100});
            drawOptions();
            finishFrame();
            return;
        }
        if(fullscreen&&visualizerFullscreen&&!ui.recordingScreen()){
            drawLocalVisualizer(p);finishFrame();return;
        }
        header();
        if (fullscreen) {
            drawFullscreen(p);
            drawControls(p, p.error);
            if(visualizerFullscreen)ui.paintBackground();
            drawLocalVisualizer(p);
            drawPlayerSettings();
            if(colourPicking)drawColours();
            drawOptions();
            finishFrame();
            return;
        }
        auto tabs=mainTabs();for(int i=0;i<int(tabs.size());i++)ui.button(28+i*164,90,152,48,tabs[i].first,mainTab()==tabs[i].second);
        if(page==7||page==8||page==10){drawAlbumPage(p);drawCompact(p);drawControls(p,p.error.empty()?status:p.error);drawOptions();finishFrame();return;}

        if(page==5||page==6||page==9){drawCollections();drawCompact(p);drawControls(p,p.error.empty()?status:p.error);drawOptions();finishFrame();return;}
        if (page == 3) {
            ui.rect(28, 155, 220, layout().panelHeight(), PANEL);
            ui.rect(264, 155, 592, layout().panelHeight(), PANEL);
            ui.text(46, 160, "CATEGORIES", 15, MUTED, 184);
            int categoryRows=layout().rows(),categoryCount=int(settingsNavigation.categories.size());
            settingsCategoryScroll=std::clamp(settingsCategoryScroll,std::max(0,settingsNavigation.category-categoryRows+1),settingsNavigation.category);
            ui.scrollIndicator(241,182,categoryRows*48-4,categoryCount,settingsCategoryScroll,categoryRows);
            for (int i = settingsCategoryScroll; i < std::min(categoryCount,settingsCategoryScroll+categoryRows); ++i) {
                const int y = 182 + (i-settingsCategoryScroll) * 48;
                const bool current = i == settingsNavigation.category;
                const bool focus = current && !settingsNavigation.entries && !controlsFocused;
                UI::Clip clip(ui, 37, y-3, 202, 50);
                if (current) ui.rect(40, y, 196, 44, FOCUS);

                ui.text(52, y + 9, settingsNavigation.categories[i].name, 20,
                        focus ? ACCENT : TEXT, 172);
            }
        } else ui.rect(28, 155, 828, layout().panelHeight(), PANEL);
        std::vector<std::string> names, values;
        std::string heading;
        if (page == 4 && independent){heading="REMOTE CONNECTION";names={"Disconnect from "+independent->config.name};values={independent->config.host};
        } else if (page == 4) {
            heading = clientEditing ? "EDIT REMOTE CLIENT" : "REMOTE CLIENTS";
            if (clientEditing) {
                names = {"Name", "Client type", "Host / IP", "Save", "Cancel"};
                values = {editClient.name, hostName(editClient.type),
                          editClient.host, "", ""};
                if (editClientIndex >= 0)
                    names.push_back("Remove client");
            } else {
                for (auto &c : settings.remoteClients) {
                    names.push_back(c.name);
                    values.push_back(c.host + ":" + std::to_string(c.port));
                }
                names.push_back("+ Add remote client");
                names.push_back("Scan for remote clients");
            }
        } else if (editing) {
            heading = editIndex < 0 ? "ADD SMB SHARE" : "EDIT SMB SHARE";
            names = {"Display name",    "Server / IP", "Share name", "Subfolder",
                     "Username",        "Password",    "Domain",     "Guest access",
                     "Test connection", "Save share"};
            if (editIndex >= 0)
                names.push_back("Remove share");
            values = {edit.name,
                      edit.server,
                      edit.share,
                      edit.folder,
                      edit.guest ? "Not required" : edit.user,
                      edit.guest              ? "Not required"
                      : edit.password.empty() ? "Not set"
                                              : "********",
                      edit.domain,
                      edit.guest ? "On" : "Off",
                      "",
                      ""};
            if (editIndex >= 0)
                values.push_back("");
        } else if (page == 1) {
            heading = source.root == DiscRoot ? "PHYSICAL DISC" : source.smb
                          ? (source.share.name.empty() ? source.share.server : source.share.name)
                          : "LOCAL / USB";
            heading = sourceSelected ? heading + "  /  " + (folder.empty() ? "Music" : folder)
                                     : "SELECT A FILE SOURCE";
            for(auto &a:catalog["albums"])if(a["id"]==albumContextID)heading=a.value("title",std::string{})+" / "+a.value("artist",std::string{});

        } else if (page == 0) {
            heading = "FILES";
            for(auto &device:storageDevices_){names.push_back(device.name);values.push_back(device.id==-2?"USB audio CD / MusicBrainz":storageFolder(settings,device));}
            for (auto &s : settings.shares) {
                names.push_back(s.name.empty() ? s.server : s.name);
                values.push_back(s.server + " / " + s.share);
            }

        } else {
            heading = settingsNavigation.current().name;
            const auto fields = settingFields();
            for (int index : settingsNavigation.current().fields) {
                const auto &field = fields[index];
                names.push_back(field["label"].get<std::string>());
                values.push_back(settingText(field));
            }
        }
        const bool settingsPage = page == 3;
        const auto rowFields=settingsPage?settingFields():nlohmann::json::array();
        const int listX = settingsPage ? 280 : 40, listW = settingsPage ? 560 : 804;
        const int textX = listX + 17;
        const int rowScroll = settingsPage ? settingsNavigation.current().scroll : scroll;
        const int rowSelected = settingsPage ? settingsNavigation.current().selected : selected;
        const bool listFocused = !controlsFocused && (!settingsPage || settingsNavigation.entries);
        ui.text(settingsPage ? 282 : 46, 160, heading, 15, MUTED, settingsPage ? 550 : 790);
        const int rowCount=page==1?int(entries.size()):int(names.size());
        for (int row = 0; row < layout().rows(); row++) {
            int index = rowScroll + row;
            if (index >= rowCount)
                break;
            int y = 182 + row * 48;
            UI::Clip clip(ui, listX-3, y-3, listW+6, 50);
            if (listFocused && rowSelected == index) {
                ui.rect(listX, y, listW, 44, FOCUS);

            }
            bool active = listFocused && rowSelected == index;
            const auto &rowName=page==1?entries[index].name:names[index];
            std::string rowValue=page==1?(entries[index].directory?"Folder":"Track"):index<int(values.size())?values[index]:std::string{};
            bool hasValue=!rowValue.empty();
            // Every tab uses the entire row; secondary details sit below its title.
            ui.text(textX, y + (hasValue ? 2 : 9), rowName, hasValue ? 20 : 21,
                    (settingsPage&&settingDisabled(rowFields[settingsNavigation.current().fields[index]]))?Color{MUTED.r,MUTED.g,MUTED.b,130}:active ? ACCENT : TEXT, settingsPage ? 526 : hasValue && page == 1 ? 640 : 770);
            if (hasValue) {
                if (page == 1 && !editing)
                    ui.rightText(listX+listW-17, y + 11, rowValue, 17, MUTED);
                else
                    ui.text(textX, y + 25, rowValue, 14, MUTED, settingsPage ? 526 : 770);
            }
        }
        ui.scrollIndicator(settingsPage?846:849,182,layout().rows()*48-4,rowCount,rowScroll,layout().rows());
        if (loading)
            ui.text(57, 240, "Loading...", 25, ACCENT);
        else if (rowCount==0)
            ui.text(57, 240, "Open Files to select your music.", 23, MUTED, 750);
        drawCompact(p);
        if (colourPicking)
            drawColours();
        auto message = !p.error.empty() ? p.error : status;
        drawControls(p, message);
        drawOptions();
        finishFrame();
    }
};
#include "main_switch.inc"
