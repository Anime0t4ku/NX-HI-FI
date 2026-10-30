#include "input_timing.hpp"
#include "player_navigation.hpp"
#include "settings_navigation.hpp"
#include "playback_diagnostics.hpp"
#include "loader.hpp"
#include "player.hpp"
#include "disc.hpp"
#include "ui.hpp"
#include "remote.hpp"
#include "remote_view.hpp"
#include "discovery.hpp"
#include "screen_control.hpp"
#include "remote_keyboard.hpp"
#include <future>
#include <ctime>
#include <arpa/inet.h>
#include <cstring>
#ifndef NXH_APP_TEST
#include <switch.h>
#endif
#include <sys/stat.h>
using namespace nxh;
static const std::string ConfigPath = "sdmc:/switch/nx-hifi/config.json";
static bool keyboard(const std::string &label, std::string &value, bool password = false) {
    if (remoteKeyboard.intercept)
        return remoteKeyboard.request(label, value, password);
    SwkbdConfig cfg{};
    if (R_FAILED(swkbdCreate(&cfg, 0)))
        return false;
    swkbdConfigMakePresetDefault(&cfg);
    swkbdConfigSetHeaderText(&cfg, label.c_str());
    swkbdConfigSetInitialText(&cfg, value.c_str());
    swkbdConfigSetStringLenMax(&cfg, 255);
    swkbdConfigSetPasswordFlag(&cfg, password ? 1 : 0);
    char out[1024]{};
    Result rc = swkbdShow(&cfg, out, sizeof out);
    swkbdClose(&cfg);
    SDL_PumpEvents();
    SDL_FlushEvents(SDL_FINGERDOWN, SDL_FINGERMOTION);
    SDL_FlushEvents(SDL_MOUSEMOTION, SDL_MOUSEWHEEL);
    if (R_FAILED(rc))
        return false;
    value = out;
    return true;
}
class App {
    const Layout &layout() const {
        return ui.layout();
    }
#include "app_remote.inc"
    UI &ui;
    SDL_Renderer *renderer;
    Player &player;
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
        setAppearance(settings.theme, settings.accent);
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
            cooldown = 15;
        } else if (selected == 1) {
            choose("Client type", {"nx", "mister"}, {"NX Hi-Fi", "MiSTer Hi-Fi"}, editClient.type,
                   [this](auto v) {
                       editClient.type = v.template get<std::string>();
                       editClient.port = 8182;
                   });
        } else if (selected == 2) {
            keyboard("Host or IP address", editClient.host);
            cooldown = 15;
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
    Loader browser, art;
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
        optionTitle = std::move(title);
        optionValues = std::move(values);
        optionLabels = std::move(labels);
        optionApply = std::move(apply);
        optionIndex = 0;
        for (int i = 0; i < int(optionValues.size()); ++i)
            if (optionValues[i] == current)
                optionIndex = i;
    }
    void drawOptions() {
        if (!optionApply)
            return;
        ui.rect(140, 100, 1000, 510 + layout().extraHeight(), PANEL);
        ui.text(170, 120, optionTitle, 26, ACCENT);
        int first = layout().optionFirst(optionIndex);
        for (int i = first; i < std::min(first + layout().optionRows(), int(optionLabels.size()));
             ++i)
            ui.button(170, 170 + (i - first) * 48, 940, 42, optionLabels[i], i == optionIndex);
    }
    bool sourceSelected = false, controlsFocused = false;
    SettingsNavigation settingsNavigation;
    int control = 0;
    PlayerNavigation menuFocus,fullFocus{2,1};
    PlayerNavigation &playerFocus(){return fullscreen?fullFocus:menuFocus;}
    const PlayerNavigation &playerFocus()const{return fullscreen?fullFocus:menuFocus;}
    PlayerRect seekRect()const{return playerSeekRect(fullscreen,layout().extraHeight(),playerX(),playerWidth());}
    PlayerRect volumeRect()const{return playerVolumeRect(fullscreen,layout().extraHeight(),playerX(),playerWidth());}
    void syncPlayerFocus(){seekerFocused=(fullscreen||controlsFocused)&&playerFocus().seek();control=std::max(0,playerFocus().action(fullscreen));}
    void playerVertical(int delta){playerFocus().vertical(fullscreen,delta);syncPlayerFocus();}
    void playerHorizontal(int delta){
        if(playerFocus().seek())player.seek(delta*10);
        else if(playerFocus().volume(fullscreen)){settings.volume=std::clamp(settings.volume+delta*5,0,100);persist();}
        else playerFocus().horizontal(fullscreen,delta);
        syncPlayerFocus();
    }
    void playerAction(){
        int action=playerFocus().action(fullscreen);
        if(action==6){
            std::vector<nlohmann::json> values{-1};std::vector<std::string> labels{"Off"};int current=-1;
            for(int i=0;i<int(presets.size());i++){values.push_back(i);labels.push_back(presets[i].name);if(settings.eqEnabled&&settings.eq==presets[i].gains)current=i;}
            choose("Equalizer preset",values,labels,current,[this](auto v){int index=v.template get<int>();settings.eqEnabled=index>=0;if(index>=0)settings.eq=presets[index].gains;persist();});
        }
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
    void openColours() {
        colourPicking = true;
        status = "";
        colourSelection = 0;
        customRGB = colourRGB(settings.accent);
        for (int i = 0; i < 10; i++)
            if (settings.accent == accentPresets[i].hex)
                colourSelection = i;
    }
    bool beginColourDrag(int x, int y) {
        if (!colourPicking)
            return false;
        for (int i = 0; i < 3; i++)
            if (inside(x, y, 210, 326 + i * 47, 600, 36)) {
                colourDrag = i;
                colourSelection = 10 + i;
                dragColour(x);
                return true;
            }
        return false;
    }
    void dragColour(int x) {
        if (colourDrag >= 0)
            customRGB[colourDrag] = std::clamp((x - 210) * 255 / 600, 0, 255);
    }
    void closeColours(bool save) {
        if (save) {
            auto before = settings.accent;
            settings.accent = colourHex(customRGB);
            if (!persist()) {
                settings.accent = before;
                player.settings(settings);
                return;
            }
        }
        colourPicking = false;
        colourDrag = -1;
        mouseColourDrag = false;
        setAppearance(settings.theme, settings.accent);
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
            cooldown = 15;
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
    void toggleFullscreen() {
        if(colourPicking)return;
        fullscreen=!fullscreen;scrubbing=mouseScrub=false;
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
    int horizontalFrames = 0, attemptedPort = -1;
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
        return artworkVisible() ? 560 : 64;
    }
    int playerWidth() const {
        return artworkVisible() ? 656 : 1152;
    }
    bool artworkVisible() const {
        return settings.showArtwork && (artTexture || !settings.hideMissingArt);
    }
    std::string clock() const {
        u64 timestamp;
        TimeCalendarTime cal{};
        TimeCalendarAdditionalInfo info{};
        if (R_SUCCEEDED(timeGetCurrentTime(TimeType_UserSystemClock, &timestamp)) &&
            R_SUCCEEDED(timeToCalendarTimeWithMyRule(timestamp, &cal, &info)))
            return formatClock(cal.hour, cal.minute, settings.clock24h);
        return "--:--";
    }
    void header() {
        ui.text(28, 19, "NX Hi-Fi", 38, ACCENT);
        ui.text(28 + ui.textWidth("NX Hi-Fi", 38) + 14, 35, std::string("v") + Version, 16, MUTED);
        if (settings.showClock) {
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
        if (settings.musicRoot != before.musicRoot)
            sourcesChanged();
        return true;
    }
    nlohmann::json remoteCommand(const nlohmann::json &j) {
        using J = nlohmann::json;
        auto fail = [](std::string error) { return J{{"ok", false}, {"error", error}}; };
        std::string op = j.value("op", std::string{});
        if (op.rfind("screen_", 0) == 0)
            return screenCommand(j);
        remoteKeyboard.cancel();
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
        } else if (op == "settings") {
            if (!j.contains("revision") || !j["revision"].is_number_integer() ||
                j["revision"].get<uint64_t>() != revision)
                return fail("Settings changed; refresh and try again.");
            if (!applySettings(j.at("patch")))
                return fail(status);
        } else if (op == "source_save" || op == "source_remove") {
            if (!sourceCheck())
                return fail("Sources changed; refresh and try again.");
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
                return fail("Sources changed; refresh and try again.");
            int index = j.value("source", -1);
            if (index < -2 || index >= int(settings.shares.size()))
                return fail("Source no longer exists.");
            Source src = index == -2 ? Source{false, {}, DiscRoot} : index < 0
                             ? Source{false, {}, settings.musicRoot}
                             : Source{true, settings.shares[index], settings.shares[index].folder};
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
            player.previous();
        else if (op == "next")
            player.next();
        else if (op == "stop")
            player.stop();
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
    nlohmann::json remoteState() {
        using J = nlohmann::json;
        auto p = player.snapshot();
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
        auto eqPresets = J::array();
        for (auto &preset : presets)
            eqPresets.push_back({{"name", preset.name}, {"gains", preset.gains}});
        auto accents = J::array();
        for (auto &preset : accentPresets)
            accents.push_back({{"name", preset.name}, {"hex", preset.hex}});
        return {{"version", Version},
                {"physical_disc", true},
                {"revision", revision},
                {"sources_revision", sourcesRevision},
                {"settings", settingsDocument(settings)},
                {"fields", settingFields()},
                {"presets", eqPresets},
                {"accents", accents},
                {"clock", clock()},
                {"remote_address", remoteAddress},
                {"playback",
                 {{"title", trackTitle(p)},
                  {"artist", p.details.artist},
                  {"album", trackAlbum(p)},
                  {"badges", badges},
                  {"index", p.index},
                  {"count", p.count},
                  {"position", p.position},
                  {"duration", p.duration},
                  {"duration_estimated", p.details.durationEstimated},
                  {"playing", p.playing},
                  {"paused", p.paused},
                  {"busy", p.busy},
                  {"error", p.error},
                  {"spectrum", p.spectrum},
                  {"track_key", p.track.source.share.server + "/" + p.track.source.share.share +
                                    "/" + p.track.source.root + "/" + p.track.path}}}};
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
                remoteKeyboard.intercept = false;
                remoteKeyboard.cancel();
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
                remote->publish(remoteState(), settings, sourcesRevision);
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
            in_addr address{};
            address.s_addr = gethostid();
            auto ip = std::string(inet_ntoa(address));
            remoteAddress = address.s_addr
                                ? "http://" + ip + ":" + std::to_string(settings.remotePort)
                                : "Not connected to a network";
        }
        if (!lastPublish || now - lastPublish >= 50) {
            lastPublish = now;
            remote->publish(remoteState(), settings, sourcesRevision);
        }
    }
    std::string settingText(const nlohmann::json &field) const {
        std::string key = field["key"], kind = field["kind"];
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
    void settingAction(int = 1) {
        if (!settingsNavigation.entries) return;
        auto fields = settingFields();
        auto field = fields[settingsNavigation.fieldIndex()];
        std::string key = field["key"], kind = field["kind"];
        if (kind == "info")
            return;
        if (kind == "colour") {
            openColours();
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
            cooldown = 15;
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
    void editSDRoot() {
        auto value = settings.musicRoot;
        cooldown = 15;
        if (keyboard("SD music folder (sdmc:/...)", value))
            applySettings({{"music_root", value}});
    }
    bool loading = false;
    Share edit;
    int editIndex = -1;
    bool editing = false, running = true;
    SDL_Texture *artTexture = nullptr;
    int cooldown = 0;
    double touchY = -1;
    bool gesture = false;
    ButtonRepeat verticalRepeat, horizontalRepeat;
    int repeatFrames = 0; // The existing remote connection/scan dialogs own this repeat state.
    void changePage(int p) {
        page = p;
        clientEditing = false;
        controlsFocused = false;
        editing = false;
        colourPicking = false;
        settingsNavigation.entries = false;
        selected = scroll = 0;
        status = "";
    }
    bool persist() {
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
        if (editSourcesRevision != sourcesRevision) {
            status = "Sources changed; reopen this editor before saving.";
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
        cooldown = 15;
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
            if (editSourcesRevision != sourcesRevision) {
                status = "Sources changed; reopen this editor.";
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
        if (page == 4)
            return clientEditing ? (editClientIndex < 0 ? 4 : 5)
                                 : int(settings.remoteClients.size()) + 1;
        if (editing)
            return editIndex < 0 ? 9 : 10;
        if (page == 1)
            return std::max(0, int(entries.size()) - 1);
        if (page == 0)
            return settings.shares.size() + 2;
        if (page == 2)
            return 7;
        return int(settingFields().size()) - 1;
    }
    void move(int delta) {
        if (optionApply) {
            optionIndex = (optionIndex + delta + int(optionValues.size())) % optionValues.size();
            return;
        }
        if (fullscreen || controlsFocused) {
            playerVertical(delta);
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
    void transport(int index) {
        switch (index) {
        case 0:
            player.previous();
            break;
        case 1:
            player.pause();
            break;
        case 2:
            player.next();
            break;
        case 3:
            player.stop();
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
        if (optionApply) {
            auto f = std::move(optionApply);
            optionApply = nullptr;
            f(optionValues[optionIndex]);
            return;
        }
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
        if (editing) {
            editField();
            return;
        }
        if (page == 1) {
            openSelected();
            return;
        }
        if (page == 0) {
            if (selected == 0)
                browse(Source{false, {}, settings.musicRoot}, "");
            else if (selected == int(settings.shares.size()) + 1)
                browse(Source{false, {}, DiscRoot}, "");
            else if (selected == int(settings.shares.size()) + 2) {
                edit = {};
                editIndex = -1;
                editing = true;
                editSourcesRevision = sourcesRevision;
                selected = scroll = 0;
            } else
                browse(Source{true, settings.shares[selected - 1],
                              settings.shares[selected - 1].folder},
                       "");
            return;
        }
        if (page == 2) {
            if (selected == 0) {
                settings.eqEnabled = !settings.eqEnabled;
                persist();
            } else if (selected == 1) {
                std::vector<nlohmann::json> values;
                std::vector<std::string> labels;
                int current = -1;
                for (int i = 0; i < int(presets.size()); ++i) {
                    values.push_back(i);
                    labels.push_back(presets[i].name);
                    if (settings.eq == presets[i].gains)
                        current = i;
                }
                choose("Equalizer preset", values, labels, current, [this](auto v) {
                    settings.eq = presets[v.template get<int>()].gains;
                    settings.eqEnabled = true;
                    persist();
                });
            } else if (selected == 7) {
                settings.eq = {};
                persist();
            }
            return;
        }
        if (page == 3 && !settingsNavigation.entries) settingsNavigation.entries = true;
        else settingAction();
    }

    void adjust(int dir) {
        if (optionApply) {
            move(dir);
            return;
        }
        if (fullscreen || controlsFocused) {
            playerHorizontal(dir);
            return;
        }
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
        if (page == 2 && !editing && selected >= 2 && selected <= 6) {
            settings.eq[selected - 2] =
                std::clamp(settings.eq[selected - 2] + dir * .5f, -6.f, 6.f);
            settings.eqEnabled = true;
            persist();

        } else if (page == 2 && selected == 1) {
            int i = dir > 0 ? -1 : 0;
            for (int j = 0; j < int(presets.size()); j++)
                if (settings.eq == presets[j].gains)
                    i = j;
            i = (i + dir + int(presets.size())) % presets.size();
            settings.eq = presets[i].gains;
            settings.eqEnabled = true;
            persist();
        } else
            move(dir * 5);
    }
    void back() {
        if (optionApply) {
            optionApply = nullptr;
            return;
        }
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
        if (connectionOpen || scanOpen) {
            dialogClick(x, y);
            return;
        }
        if (optionApply) {
            int first = layout().optionFirst(optionIndex);
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
        if(!fullscreen&&artworkVisible()&&inside(x,y,layout().compactArtX(),182,layout().compactArtDesignSize(),layout().artSize(false))){toggleFullscreen();return;}
        if(fullscreen)return;
        if(inside(x,y,28,90,770,54)){
            int tab=(x-28)/154;if(tab<5)changePage(tab);seekerFocused=false;return;
        }
        if (page == 3) {
            const int row = (y - 182) / 48;
            if (y < 182 || row >= layout().rows()) return;
            if (inside(x,y,28,155,220,layout().panelHeight()) && row < int(settingsNavigation.categories.size())) {
                settingsNavigation.category = row;
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
        return !p.details.title.empty() ? p.details.title
               : p.track.name.empty()   ? "Nothing playing"
                                        : p.track.name;
    }
    std::string trackAlbum(const Playback &p) const {
        return !p.details.album.empty() ? p.details.album
               : p.track.path.empty()   ? "SD card + SMB + audio CD"
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
    void drawFullscreen(const Playback &p) {
        if(artworkVisible())ui.image(artTexture,64,140,400);
        int x=playerX(),width=playerWidth();
        ui.text(x,142,"NOW PLAYING",15,MUTED,width);
        ui.marquee(x,180,trackTitle(p),34,TEXT,width,"fullscreen-title");
        ui.text(x,238,p.details.artist,25,ACCENT,width);
        ui.text(x,282,trackAlbum(p),22,MUTED,width);
        drawBadges(p,x,330,width,16);
        drawVisualizer(p);
        if(p.count>0)ui.text(64,artworkVisible()?layout().bottom(490):100,std::to_string(p.index+1)+" / "+std::to_string(p.count),16,MUTED);
    }
    void drawVisualizer(const Playback &p) {
        if (!settings.visualizer) return;
        const auto area=playerVisualizerRect(fullscreen,layout().extraHeight(),playerX(),playerWidth());
        UI::Clip clip(ui,area.x,area.y,area.w,area.h);
        for (int i=0;i<32;++i) {
            const int height=std::max(2,int(std::clamp(p.spectrum[i],0.f,1.f)*area.h));
            ui.rect(area.x+i*area.w/32,area.y+area.h-height,
                    std::max(3,area.w/32-(fullscreen?7:4)),height,ACCENT);
        }
    }
    void drawSeek(const Playback &p){
        auto a=seekRect();bool focus=(controlsFocused||fullscreen)&&playerFocus().seek();
        if(focus){ui.roundedRect(a.x-5,a.y-5,a.w+10,a.h+10,ACCENT,10);ui.roundedRect(a.x-3,a.y-3,a.w+6,a.h+6,PANEL,8);}
        double position=scrubbing?scrubPosition:p.position;
        int y=a.y+12;ui.rect(a.x,y,a.w,5,LINE);
        if(p.duration>0){int offset=int(a.w*std::clamp(position/p.duration,0.,1.));ui.rect(a.x,y,offset,5,ACCENT);ui.rect(a.x+std::clamp(offset,4,a.w-4)-4,y-4,8,13,ACCENT);}
        ui.text(a.x,a.y+30,timeLabel(position),fullscreen?18:14,MUTED);
        auto duration=p.duration>0?(p.details.durationEstimated?"~":"")+timeLabel(p.duration):"--:--";
        ui.rightText(a.x+a.w,a.y+30,duration,fullscreen?18:14,MUTED);
    }
    void drawPlayerButtons(const Playback &p){
        for(int row=fullscreen?2:1;row<=2;row++)for(int col=0;col<PlayerNavigation::count(fullscreen,row);col++){
            PlayerNavigation item{row,col};int id=item.action(fullscreen);
            auto a=playerButtonRect(fullscreen,layout().extraHeight(),row,col);
            bool focus=(fullscreen||controlsFocused)&&playerFocus().row==row&&playerFocus().column==col;
            bool active=(id==4&&settings.shuffle)||(id==5&&settings.repeat);
            if(focus)ui.roundedRect(a.x-3,a.y-3,a.w+6,a.h+6,ACCENT,14);
            ui.roundedRect(a.x,a.y,a.w,a.h,id==1?FOCUS:PANEL);
            ui.transportIcon(a.x+a.w/2,a.y+a.h/2,fullscreen?(id==1?52:44):(id==1?42:34),id==6?10:id==7?11:id,p.playing&&!p.paused,active||id==1?ACCENT:TEXT,settings.repeat==2);
            if(active)ui.rect(a.x+a.w/2-10,a.y+a.h-5,20,3,ACCENT);
        }
        auto a=volumeRect();bool focus=(controlsFocused||fullscreen)&&playerFocus().volume(fullscreen);
        if(focus){ui.roundedRect(a.x-4,a.y-4,a.w+8,a.h+8,ACCENT,10);ui.roundedRect(a.x-2,a.y-2,a.w+4,a.h+4,PANEL,8);}
        ui.text(a.x,a.y,"Volume",14,focus?ACCENT:MUTED);
        ui.rect(a.x+65,a.y+10,a.w-127,4,LINE);ui.rect(a.x+65,a.y+10,(a.w-127)*settings.volume/100,4,ACCENT);
        ui.text(a.x+a.w-54,a.y,std::to_string(settings.volume)+"%",14,focus?ACCENT:MUTED,54);
    }
    void drawColours() {
        ui.rect(28, 155, 828, layout().panelHeight(), PANEL);
        ui.text(46, 160, "ACCENT COLOUR", 15, MUTED);
        for (int i = 0; i < 10; i++) {
            int x = 52 + (i % 5) * 156, y = 200 + (i / 5) * 54;
            Color c = rgbColour(accentPresets[i].hex);
            ui.rect(x, y, 144, 44, colourSelection == i ? FOCUS : BG);
            ui.rect(x + 8, y + 9, 25, 25, c);
            ui.text(x + 42, y + 12, accentPresets[i].name, 17, TEXT, 96);
            if (colourSelection == i)
                ui.rect(x, y + 42, 144, 2, ACCENT);
        }
        ui.text(52, 310, "CUSTOM", 14, MUTED);
        const char *channels[] = {"Red", "Green", "Blue"};
        Color bars[] = {{239, 68, 68}, {34, 197, 94}, {59, 130, 246}};
        for (int i = 0; i < 3; i++) {
            int y = 326 + i * 47;
            if (colourSelection == 10 + i)
                ui.rect(44, y - 1, 780, 38, FOCUS);
            ui.text(52, y + 6, std::string(channels[i]) + "  " + std::to_string(customRGB[i]), 18,
                    TEXT, 145);
            ui.rect(210, y + 15, 600, 5, LINE);
            ui.rect(210, y + 15, customRGB[i] * 600 / 255, 5, bars[i]);
            ui.rect(210 + std::clamp(customRGB[i] * 600 / 255, 5, 595) - 5, y + 8, 10, 20, TEXT);
        }
        ui.rect(52, 476, 760, 38, colourSelection == 13 ? FOCUS : BG);
        ui.rect(64, 482, 24, 24, rgbColour(colourHex(customRGB)));
        ui.text(103, 483, "Custom hex  " + colourHex(customRGB), 18, TEXT, 690);
        ui.button(52, 531, 360, 44, "Save colour", colourSelection == 14);
        ui.button(432, 531, 380, 44, "Cancel", colourSelection == 15);
    }
    void drawControls(const Playback &p,const std::string &message){
        if(!colourPicking){drawSeek(p);drawPlayerButtons(p);}
        // Status/errors stay visible outside the compact controls-only footer.
        if(!message.empty())ui.text(fullscreen?30:600,fullscreen?90:64,message,13,ACCENT,fullscreen?1210:620);
        ui.rect(0,layout().footerY(),1280,layout().footerHeight(),FOOTER);
        int x=28,y=layout().footerY()+8;
        auto hint=[&](const char *button,const std::string &action){x=ui.padHint(x,y,button,action);};
        if(optionApply){hint("Dpad","Navigate");hint("A","Select");hint("B","Back");return;}
        if(colourPicking){hint("Dpad","Colour");hint("A","Choose");hint("B","Cancel");return;}
        if(fullscreen||controlsFocused){
            const char *labels[]={"Previous","Play/Pause","Next","Stop","Shuffle","Repeat","Equalizer","Fullscreen"};
            auto &focus=playerFocus();int id=focus.action(fullscreen);
            hint("Dpad",focus.seek()?"Seek 10s":focus.volume(fullscreen)?"Volume":"Navigate");
            if(id>=0)hint("A",labels[id]);
            hint("B",fullscreen?"Close":"Browse");
        }else{hint("Dpad","Navigate");hint("A",page==3?(settingsNavigation.entries?"Change":"Select"):editing?"Edit":"Open");hint("B",page==3&&settingsNavigation.entries?"Categories":"Back");if(page==0)hint("X","Edit source");if(page==4)hint("X","Edit client");}
        if(!fullscreen){hint("-","Player");hint("L","");hint("R","Tabs");hint("R3","Fullscreen");}
        hint("+","Play/Pause");
        if(fullscreen){hint("L","");hint("R","Track");hint("ZL","");hint("ZR","Track");}
    }

  public:
    App(UI &u, SDL_Renderer *r, Player &p, Settings s, std::string error,
        std::string settingsFile = ConfigPath)
        : ui(u), renderer(r), player(p), settings(std::move(s)),
          settingsPath(std::move(settingsFile)), status(std::move(error)) {
        source = {false, {}, settings.musicRoot};
        page = status.empty() ? 0 : 3;
        setAppearance(settings.theme, settings.accent);
    }
    ~App() {
        probeCancel = true;
        scanner.cancel();
        disconnectTarget();
        for (auto &f : retiredClients)
            f.get();
        remote.reset();
        for (auto &retired : retiringRemotes)
            retired.wait();
        if (artTexture)
            ui.releaseCover(artTexture);
    }
    bool alive() const {
        return running;
    }
    void displayChanged() {
        screenPointerActive = false;
        // Cancel only in-progress pointer gestures; preserve music, pages and focus.
        scrubbing = mouseScrub = mouseColourDrag = false;
        colourDrag = -1;
        touchY = clientTouchY = -1;
        gesture = clientGesture = true;
        layout().keepVisible(selected, scroll, maxSelection() + 1);
        if (targetView)
            targetView->displayChanged();
    }
    void input(PadState &pad, u64 injectedDown = 0, u64 injectedHeld = 0, bool injected = false) {
        if (!injected)
            padUpdate(&pad);
        auto down = injected ? injectedDown : padGetButtonsDown(&pad),
             held = injected ? injectedHeld : padGetButtons(&pad);
        if (!injected && (down || held))
            remoteKeyboard.cancel();
        if (dialogInput(down, held, injected))
            return;
        if (targetView) {
            if (cooldown > 0) {
                --cooldown;
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
        if (down & HidNpadButton_Plus)
            player.pause();
        if (cooldown > 0) {
            wake();
            cooldown--;
            return;
        }
        if (!colourPicking && !optionApply && (down & HidNpadButton_StickR))
            toggleFullscreen();

        if (!fullscreen && !colourPicking && !optionApply && (down & HidNpadButton_L)) {
            fullscreen = false;
            changePage((page + 4) % 5);
        }
        if (!fullscreen && !colourPicking && !optionApply && (down & HidNpadButton_R)) {
            fullscreen = false;
            changePage((page + 1) % 5);
        }
        bool up = held & (HidNpadButton_Up | HidNpadButton_StickLUp),
             dn = held & (HidNpadButton_Down | HidNpadButton_StickLDown);
        int vertical=up?-1:dn?1:0;
        if(verticalRepeat.fire(vertical,down&(HidNpadButton_Up|HidNpadButton_Down|HidNpadButton_StickLUp|HidNpadButton_StickLDown),SDL_GetTicks64()))move(vertical);
        if (down & HidNpadButton_A)
            action();
        if (down & HidNpadButton_B)
            back();
        bool left = held & HidNpadButton_Left, right = held & HidNpadButton_Right;
        int horizontal=left?-1:right?1:0;
        if(horizontalRepeat.fire(horizontal,down&(HidNpadButton_Left|HidNpadButton_Right),SDL_GetTicks64()))adjust(horizontal);
        if (down & (HidNpadButton_ZL | (fullscreen ? HidNpadButton_L : 0))) player.previous();
        if (down & (HidNpadButton_ZR | (fullscreen ? HidNpadButton_R : 0))) player.next();
        if (!colourPicking && !optionApply && (down & HidNpadButton_Minus)) {
            if(fullscreen)toggleFullscreen();
            else {controlsFocused=!controlsFocused;syncPlayerFocus();}
        }
        if (!colourPicking && (down & HidNpadButton_X)) {
            if (!fullscreen && page == 4 && !clientEditing) {
                beginClientEdit(selected < int(settings.remoteClients.size()) ? selected : -1);
                return;
            }
            if (!fullscreen && page == 0 && !editing) {
                if (selected == 0) {
                    editSDRoot();
                    return;
                }
                if (selected > 0 && selected <= int(settings.shares.size())) {
                    editIndex = selected - 1;
                    edit = settings.shares[editIndex];
                } else {
                    editIndex = -1;
                    edit = {};
                }
                editing = true;
                editSourcesRevision = sourcesRevision;
                selected = scroll = 0;
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
                if (!injected)
                    remoteKeyboard.cancel();
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
                if (!fullscreen && !colourPicking && !optionApply &&
                    touchY >= 182 && touchY < 155 + layout().panelHeight() && e.tfinger.x * 1280 < 856) {
                    controlsFocused = seekerFocused = false;
                    if (page == 3) settingsNavigation.entries = e.tfinger.x * 1280 >= 264;
                }
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
    void update() {
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
        ui.recordScreen(settings.remoteEnabled && screenSessions.active(SDL_GetTicks64()) &&
                        SDL_GetTicks64() - lastScreenPublish >= 50);
        updateRemote();
        sleeping = settings.screensaverSeconds > 0 &&
                   SDL_GetTicks64() - lastInteraction >= Uint64(settings.screensaverSeconds) * 1000;
        player.tick();
        LoadResult result;
        if (browser.poll(result)) {
            if (result.kind == 1) {
                status = result.ok ? "SMB connected: folder is readable." : result.error;
            } else {
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
                status = result.ok
                             ? (entries.empty() ? "No supported music files or folders here." : "")
                             : result.error;
            }
        }
        auto p = player.snapshot();
        auto key = p.track.source.share.server + "/" + p.track.source.share.share + "/" +
                   p.track.source.root + "/" + p.track.path +
                   (settings.preferFolderArt ? "/folder-first" : "/embedded-first");
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
        if (settings.showArtwork && !p.track.path.empty() && key != artKey) {
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
            art.request(p.track.source, p.track.path, 2, settings.preferFolderArt);
        }
        if (art.poll(result) && settings.showArtwork) {
            playbackTrace("main.artwork.decode.begin", result.cover.size());
            if (artTexture)
                ui.releaseCover(artTexture);
            remoteArt.clear();
            artTexture = ui.cover(result.cover, settings.remoteEnabled ? &remoteArt : nullptr);
            playbackTrace("main.artwork.decode.end", remoteArt.size());
            if (remote)
                remote->artwork(remoteArt);
            scrubbing = false;
            mouseScrub = false;
        }
    }
    void draw() { draw(player.snapshot()); }
    void draw(const Playback &p) {
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
        setAppearance(settings.theme, colourPicking ? colourHex(customRGB) : settings.accent);
        ui.begin();
        if (sleeping) {
            ui.rect(0, 0, 1280, layout().height(), {0, 0, 0});
            if (settings.showClock)
                ui.text(1060, 32, clock(), 22, {100, 100, 100});
            drawOptions();
            finishFrame();
            return;
        }
        header();
        if (fullscreen) {
            drawFullscreen(p);
            drawControls(p, p.error);
            drawOptions();
            finishFrame();
            return;
        }
        const char *tabs[] = {"Sources", "Library", "Equalizer", "Settings", "Remote"};
        for (int i = 0; i < 5; i++)
            ui.button(28 + i * 154, 90, 142, 48, tabs[i], page == i);

        if (page == 3) {
            ui.rect(28, 155, 220, layout().panelHeight(), PANEL);
            ui.rect(264, 155, 592, layout().panelHeight(), PANEL);
            ui.text(46, 160, "CATEGORIES", 15, MUTED, 184);
            for (int i = 0; i < int(settingsNavigation.categories.size()); ++i) {
                const int y = 182 + i * 48;
                const bool current = i == settingsNavigation.category;
                const bool focus = current && !settingsNavigation.entries && !controlsFocused;
                UI::Clip clip(ui, 40, y, 196, 44);
                if (current) ui.rect(40, y, 196, 44, FOCUS);
                if (focus) ui.rect(40, y, 4, 44, ACCENT);
                ui.text(52, y + 9, settingsNavigation.categories[i].name, 20,
                        focus ? ACCENT : TEXT, 172);
            }
        } else ui.rect(28, 155, 828, layout().panelHeight(), PANEL);
        ui.rect(876, 155, 376, layout().panelHeight(), PANEL);

        std::vector<std::string> names, values;
        std::string heading;
        if (page == 4) {
            heading = clientEditing ? "EDIT REMOTE CLIENT" : "REMOTE CLIENTS";
            if (clientEditing) {
                names = {"Name", "Client type", "Host / IP", "Save", "Cancel"};
                values = {editClient.name, editClient.type == "nx" ? "NX Hi-Fi" : "MiSTer Hi-Fi",
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
                          : "SD CARD";
            heading = sourceSelected ? heading + "  /  " + (folder.empty() ? "Music" : folder)
                                     : "SELECT A MUSIC SOURCE";
            for (auto &e : entries) {
                names.push_back(e.name);
                values.push_back(e.directory ? "Folder" : "Track");
            }
        } else if (page == 0) {
            heading = "MUSIC SOURCES";
            names.push_back("SD card");
            values.push_back(settings.musicRoot);
            for (auto &s : settings.shares) {
                names.push_back(s.name.empty() ? s.server : s.name);
                values.push_back(s.server + " / " + s.share);
            }
            names.push_back("Physical Disc");
            values.push_back("USB audio CD / MusicBrainz");
            names.push_back("+ Add SMB share");
            values.push_back("");
        } else if (page == 2) {
            heading = "FIVE-BAND EQUALIZER";
            std::string preset = "Custom";
            for (auto &v : presets)
                if (v.gains == settings.eq)
                    preset = v.name;
            names = {"Equalizer", "Preset",         "Bass 60 Hz",    "Low-mid 250 Hz",
                     "Mid 1 kHz", "High-mid 4 kHz", "Treble 12 kHz", "Reset flat"};
            values = {settings.eqEnabled ? "On" : "Off", preset};
            for (auto gain : settings.eq) {
                char b[20];
                snprintf(b, sizeof b, "%+.1f dB", gain);
                values.push_back(b);
            }
            values.push_back("");
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
        const int listX = settingsPage ? 280 : 40, listW = settingsPage ? 560 : 804;
        const int textX = listX + 17;
        const int rowScroll = settingsPage ? settingsNavigation.current().scroll : scroll;
        const int rowSelected = settingsPage ? settingsNavigation.current().selected : selected;
        const bool listFocused = !controlsFocused && (!settingsPage || settingsNavigation.entries);
        ui.text(settingsPage ? 282 : 46, 160, heading, 15, MUTED, settingsPage ? 550 : 790);
        for (int row = 0; row < layout().rows(); row++) {
            int index = rowScroll + row;
            if (index >= int(names.size()))
                break;
            int y = 182 + row * 48;
            UI::Clip clip(ui, listX, y, listW, 44);
            if (listFocused && rowSelected == index) {
                ui.rect(listX, y, listW, 44, FOCUS);
                ui.rect(listX, y, 4, 44, ACCENT);
            }
            bool active = listFocused && rowSelected == index;
            bool hasValue = index < int(values.size()) && !values[index].empty();
            // Every tab uses the entire row; secondary details sit below its title.
            ui.text(textX, y + (hasValue ? 2 : 9), names[index], hasValue ? 20 : 21,
                    active ? ACCENT : TEXT, settingsPage ? 526 : hasValue && page == 1 ? 640 : 770);
            if (hasValue) {
                if (page == 1 && !editing)
                    ui.text(713, y + 11, values[index], 17, MUTED, 110);
                else
                    ui.text(textX, y + 25, values[index], 14, MUTED, settingsPage ? 526 : 770);
            }
        }
        if (loading)
            ui.text(57, 240, "Loading...", 25, ACCENT);
        else if (names.empty())
            ui.text(57, 240, "Open Sources to select your music.", 23, MUTED, 750);
        if(artworkVisible())ui.image(artTexture,layout().compactArtX(),182,layout().compactArtDesignSize());
        ui.marquee(900,artworkVisible()?layout().compactTitleY():190,trackTitle(p),22,TEXT,328,"compact-title");
        ui.text(900,artworkVisible()?layout().compactTitleY()+34:228,!p.details.artist.empty()?p.details.artist:trackAlbum(p),16,MUTED,328);
        drawBadges(p,900,artworkVisible()?layout().compactTitleY()+65:265,328,12);
        drawVisualizer(p);
        if (colourPicking)
            drawColours();
        auto message = !p.error.empty() ? p.error : status;
        drawControls(p, message);
        drawOptions();
        finishFrame();
    }
};
#ifndef NXH_APP_TEST
int main(int, char **) {
    Result net = socketInitializeDefault();
    Result fonts = plInitialize(PlServiceType_User);
    mkdir("sdmc:/switch/nx-hifi", 0777);
    Settings settings;
    settings.musicRoot = "sdmc:/music";
    std::string error;
    playbackTrace("boot.gui-1.2.0", 0, true);
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
    SDL_Window *window = SDL_CreateWindow("NX Hi-Fi", 0, 0, initialLayout.outputWidth(),
                                          initialLayout.outputHeight(), SDL_WINDOW_SHOWN);
    SDL_Renderer *renderer =
        window
            ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC)
            : nullptr;
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
        App app(ui, renderer, player, settings, error);
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
            app.input(pad);
            app.update();
            app.draw();
        }
    }
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
    return 0;
}
#endif
