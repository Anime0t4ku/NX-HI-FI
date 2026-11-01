#include "platform_paths.hpp"
#include "core.hpp"
#include "visualizer_styles.hpp"
#include <cctype>
#include <cstring>
#include <fstream>
#include <sys/stat.h>
namespace nxh {
const std::array<Preset, 11> presets = {{{"Flat", {0, 0, 0, 0, 0}},
                                         {"Rock", {4, 2, -1.5, 2.5, 4}},
                                         {"Dance", {5, 2.5, 0, 1.5, 3}},
                                         {"Pop", {-1, 2, 3, 1.5, -1}},
                                         {"Jazz", {3, 1.5, -1.5, 1, 2.5}},
                                         {"Classical", {3, 1.5, -1, 1.5, 3}},
                                         {"Hip-Hop", {5, 3, -.5, 1, 2}},
                                         {"Vocal", {-2, .5, 3, 2.5, .5}},
                                         {"Acoustic", {2, 1.5, .5, 2, 2.5}},
                                         {"Bass Boost", {6, 4, 0, 0, 0}},
                                         {"Treble Boost", {0, 0, 0, 3, 6}}}};
std::string lower(std::string s) {
    for (char &c : s)
        c = std::tolower(static_cast<unsigned char>(c));
    return s;
}
std::string filename(const std::string &p) {
    auto i = p.find_last_of('/');
    return i == std::string::npos ? p : p.substr(i + 1);
}
std::string title(const std::string &p) {
    auto s = filename(p);
    auto i = s.find_last_of('.');
    return i == std::string::npos ? s : s.substr(0, i);
}
bool audioFile(const std::string &p) {
    auto i = p.find_last_of('.');
    if (i == std::string::npos)
        return false;
    auto ext = lower(p.substr(i));
    return ext == ".flac" || ext == ".m4a" || ext == ".mp3" || ext == ".wav" || ext == ".ogg" || ext == ".oga";
}
bool safeRelative(const std::string &p) {
    if (p.find('\0') != std::string::npos)
        return false;
    if (!p.empty() &&
        (p[0] == '/' || p.find('\\') != std::string::npos || p.find(':') != std::string::npos))
        return false;
    size_t start = 0;
    while (start <= p.size()) {
        auto end = p.find('/', start);
        if (p.substr(start, end - start) == "..")
            return false;
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return true;
}
std::string join(const std::string &a, const std::string &b) {
    return a.empty() ? b : b.empty() ? a : a.back() == '/' ? a + b : a + "/" + b;
}
std::string parent(const std::string &p) {
    auto i = p.find_last_of('/');
    return i == std::string::npos ? "" : p.substr(0, i);
}
bool validate(const Share &s, std::string &error) {
    if (s.server.empty() || s.share.empty()) {
        error = "Server and share name are required.";
        return false;
    }
    if (s.server.find_first_of("/\\ \r\n") != std::string::npos ||
        s.share.find_first_of("/\\\r\n") != std::string::npos) {
        error = "Enter a host/IP and a share name, without smb://.";
        return false;
    }
    if (!safeRelative(s.folder)) {
        error = "Folder must be relative to the share.";
        return false;
    }
    return true;
}
bool normalizeColour(std::string &hex) {
    if (hex.size() == 6)
        hex = "#" + hex;
    if (hex.size() != 7 || hex[0] != '#')
        return false;
    for (size_t i = 1; i < hex.size(); ++i) {
        char c = hex[i];
        if (c >= 'a' && c <= 'f')
            hex[i] = c - 'a' + 'A';
        else if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F')))
            return false;
    }
    return true;
}
std::array<int, 3> colourRGB(const std::string &hex) {
    std::string value = hex;
    if (!normalizeColour(value))
        value = "#3B82F6";
    auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'A' + 10; };
    std::array<int, 3> rgb{};
    for (int i = 0; i < 3; ++i)
        rgb[i] = digit(value[1 + i * 2]) * 16 + digit(value[2 + i * 2]);
    return rgb;
}
std::string colourHex(const std::array<int, 3> &rgb) {
    char hex[8];
    std::snprintf(hex, sizeof hex, "#%02X%02X%02X", std::clamp(rgb[0], 0, 255),
                  std::clamp(rgb[1], 0, 255), std::clamp(rgb[2], 0, 255));
    return hex;
}
bool validateRemoteClient(const RemoteClientConfig &c, std::string &error) {
    if (c.name.empty() || c.name.size() > 100 || c.host.empty() || c.host.size() > 253 ||
        c.host.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-") !=
            std::string::npos ||
        c.port != 8182 || (c.type != "nx" && c.type != "prospero" && c.type != "mister")) {
        error = "Enter a name, host/IP and client type. Port is fixed at 8182.";
        return false;
    }
    return true;
}
nlohmann::json settingsDocument(const Settings &s, bool secrets) {
    auto clients = nlohmann::json::array();
    for (auto &c : s.remoteClients)
        clients.push_back({{"name", c.name}, {"host", c.host}, {"type", c.type}, {"port", c.port}});
    auto shares = nlohmann::json::array();
    for (auto &x : s.shares)
        shares.push_back({{"name", x.name},
                          {"server", x.server},
                          {"share", x.share},
                          {"folder", x.folder},
                          {"username", x.user},
                          {"password_set", !x.password.empty()},
                          {"domain", x.domain},
                          {"guest", x.guest}});
    if (secrets)
        for (size_t i = 0; i < s.shares.size(); i++)
            shares[i]["password"] = s.shares[i].password;
    int eqPreset=-1;for(int i=0;i<int(presets.size());i++)if(s.eq==presets[i].gains)eqPreset=i;
    return {{"eq_preset",eqPreset},{"eq_0",s.eq[0]},{"eq_1",s.eq[1]},{"eq_2",s.eq[2]},{"eq_3",s.eq[3]},{"eq_4",s.eq[4]},{"eq_reset",false},
            {"music_root", s.musicRoot},
            {"storage_folders",s.storageFolders},
            {"volume", s.volume},
            {"auto_fullscreen",s.autoFullscreen},
            {"listening_hide_footer",s.listeningHideFooter},
            {"listening_hide_metadata",s.listeningHideMetadata},
            {"listening_hide_clock",s.listeningHideClock},
            {"listening_hide_title",s.listeningHideTitle},
            {"shuffle", s.shuffle},
            {"repeat", s.repeat},
            {"eq_enabled", s.eqEnabled},
            {"eq", s.eq},
            {"shares", shares},
            {"remote_clients", clients},
            {"theme", s.theme},
            {"animated_background",s.animatedBackground&&s.backgroundAnimation!=0},
            {"background_animation",s.animatedBackground?s.backgroundAnimation:0},
            {"menu_opacity",s.menuOpacity},{"background_speed",s.backgroundSpeed},{"look",s.look},{"visualizer_colour",s.visualizerColour},{"collections_view",s.collectionsView},{"radio_view",s.radioView},{"playlists_view",s.playlistsView},{"full_player_card",s.fullPlayerCard},{"exit_fullscreen_on_stop",s.exitFullscreenOnStop},
            {"accent_colour", s.accent},
            {"show_clock", s.showClock},
            {"clock_24h", s.clock24h},
            {"show_artwork", s.showArtwork},
            {"hide_missing_art", s.hideMissingArt},
            {"prefer_folder_art", s.preferFolderArt},
            {"visualizer", s.visualizer},
            {"visualizer_style",s.visualizerStyle},{"visualizer_sensitivity",s.visualizerSensitivity},
            {"visualizer_smoothing",s.visualizerSmoothing},{"visualizer_expand",s.visualizerExpand},
            {"remember_shuffle_repeat", s.rememberShuffleRepeat},
            {"gapless_playback", s.gaplessPlayback},
            {"wrap_navigation", s.wrapNavigation},
            {"hold_repeat", s.holdRepeat},
            {"remote_enabled", s.remoteEnabled},
            {"screensaver_seconds", s.screensaverSeconds},
            {"remote_port", s.remotePort}};
}
std::string formatClock(int hour, int minute, bool clock24h) {
    char text[32];
    if (clock24h)
        std::snprintf(text, sizeof text, "%02d:%02d", hour, minute);
    else
        std::snprintf(text, sizeof text, "%d:%02d %s", hour % 12 ? hour % 12 : 12, minute,
                      hour >= 12 ? "PM" : "AM");
    return text;
}
nlohmann::json settingFields() {
    using J = nlohmann::json;
    auto rows = J::array();
    auto add = [&](const char *key, const char *label, const char *group, const char *kind,
                   J choices = J::array()) {
        rows.push_back({{"key", key},
                        {"label", label},
                        {"group", group},
                        {"kind", kind},
                        {"choices", choices}});
    };
    add("theme", "Theme", "Appearance", "choice", {"dark", "light", "oled"});
    add("look","Look","Appearance","choice",{"modern","classic"});rows.back()["choice_labels"]={"Modern","Classic"};
    add("accent_colour", "Accent colour", "Appearance", "colour");
    add("background_animation", "Background animation", "Appearance", "choice",{0,1,2,3});
    rows.back()["choice_labels"]={"Off","Ambient drift","Aurora","Gradient waves"};
    rows.back()["disabled_with_oled"]=true;
    add("background_speed","Animation speed","Appearance","choice",{25,50,75,100,125,150,200});rows.back()["choice_labels"]={"25%","50%","75%","100%","125%","150%","200%"};rows.back()["disabled_with_oled"]=true;rows.back()["requires_enabled"]="background_animation";
    add("menu_opacity","Menu opacity","Appearance","choice",{50,55,60,65,70,75,80,85,90,95,100});rows.back()["choice_labels"]={"50%","55%","60%","65%","70%","75%","80%","85%","90%","95%","100%"};
    add("full_player_card","Full player card","Appearance","bool");
    add("show_clock", "Show clock", "Appearance", "bool");
    add("clock_24h", "24-hour clock", "Appearance", "bool");rows.back()["requires_enabled"]="show_clock";
    add("manage_shares","SMB shares","Library","manager");
    add("manage_collections","Collections","Library","manager");
    add("manage_playlists","Playlists","Library","manager");
    add("manage_radio","Online Radio","Library","manager");
    add("show_artwork", "Show artwork", "Artwork", "bool");
    add("hide_missing_art", "Hide missing artwork placeholder", "Artwork", "bool");
    add("prefer_folder_art", "Prefer folder artwork", "Artwork", "bool");
    add("visualizer_colour","Visualizer colour","Visualizer","colour");
    add("visualizer", "Show visualizer", "Visualizer", "bool");
    J styleIds=J::array(),styleLabels=J::array();for(int id:visualizerIds){styleIds.push_back(id);styleLabels.push_back(visualizerName(id));}
    add("visualizer_style", "Local fullscreen style", "Visualizer", "choice", styleIds);rows.back()["choice_labels"]=styleLabels;
    add("visualizer_sensitivity", "Sensitivity (%)", "Visualizer", "choice", {50,75,100,125,150,200});
    add("visualizer_smoothing", "Smoothing (%)", "Visualizer", "choice", {0,35,65,85});
    add("visualizer_expand", "Select to expand (local fullscreen)", "Visualizer", "bool");
    add("eq_enabled","Enable equalizer","Equalizer","bool");
    J eqIds={-1},eqLabels={"Custom"};for(int i=0;i<int(presets.size());i++){eqIds.push_back(i);eqLabels.push_back(presets[i].name);}
    add("eq_preset","Preset","Equalizer","choice",eqIds);rows.back()["choice_labels"]=eqLabels;
    const char *gainNames[]={"Bass 60 Hz","Low-mid 250 Hz","Mid 1 kHz","High-mid 4 kHz","Treble 12 kHz"};
    for(int i=0;i<5;i++){auto key="eq_"+std::to_string(i);add(key.c_str(),gainNames[i],"Equalizer","gain");}
    add("eq_reset","Reset flat","Equalizer","action");
    add("listening_hide_title", "Hide app title and version", "Listening Mode", "bool");
    add("listening_hide_clock", "Hide clock", "Listening Mode", "bool");
    add("listening_hide_metadata", "Hide technical metadata", "Listening Mode", "bool");
    add("listening_hide_footer", "Hide footer", "Listening Mode", "bool");
    add("exit_fullscreen_on_stop","Exit fullscreen on Stop","Playback","bool");
    add("auto_fullscreen", "Auto Fullscreen", "Playback", "bool");
    add("gapless_playback", "Gapless playback", "Playback", "bool");
    add("remember_shuffle_repeat", "Remember shuffle / repeat", "Playback", "bool");
    add("screensaver_seconds", "Screensaver", "Appearance", "choice",
        {0, 30, 60, 120, 300, 600, 1800});
    add("remote_enabled", "Enable web remote", "Remote", "bool");

    add("remote_address", "Remote address", "Remote", "info");
    return rows;
}
bool patchSettings(Settings &s, const nlohmann::json &patch, std::string &error) {
    if (patch.contains("remote_clients")) { error = "Outgoing remote control is unavailable."; return false; }

    if (!patch.is_object()) {
        error = "Settings must be an object.";
        return false;
    }
    auto next = s;
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        auto key = it.key();
        auto &v = it.value();
        bool *flag = nullptr;
        if(key=="full_player_card")flag=&next.fullPlayerCard;
        else if(key=="exit_fullscreen_on_stop")flag=&next.exitFullscreenOnStop;
        else if (key == "auto_fullscreen")
            flag = &next.autoFullscreen;
        else if (key == "listening_hide_title")
            flag = &next.listeningHideTitle;
        else if (key == "listening_hide_clock")
            flag = &next.listeningHideClock;
        else if (key == "listening_hide_metadata")
            flag = &next.listeningHideMetadata;
        else if (key == "listening_hide_footer")
            flag = &next.listeningHideFooter;
        else if(key=="animated_background")
            flag=&next.animatedBackground;
        else if (key == "show_clock")
            flag = &next.showClock;
        else if (key == "clock_24h")
            flag = &next.clock24h;
        else if (key == "show_artwork")
            flag = &next.showArtwork;
        else if (key == "hide_missing_art")
            flag = &next.hideMissingArt;
        else if (key == "prefer_folder_art")
            flag = &next.preferFolderArt;
        else if (key == "visualizer")
            flag = &next.visualizer;
        else if (key == "gapless_playback")
            flag = &next.gaplessPlayback;
        else if (key == "remember_shuffle_repeat")
            flag = &next.rememberShuffleRepeat;
        else if (key == "wrap_navigation")
            flag = &next.wrapNavigation;
        else if (key == "hold_repeat")
            flag = &next.holdRepeat;
        else if (key == "remote_enabled")
            flag = &next.remoteEnabled;
        else if (key == "shuffle")
            flag = &next.shuffle;
        else if (key == "eq_enabled")
            flag = &next.eqEnabled;
        if (flag) {
            if (!v.is_boolean()) {
                error = "Expected a boolean: " + key;
                return false;
            }
            *flag = v.get<bool>();
            if(key=="animated_background")next.backgroundAnimation=next.animatedBackground?std::max(1,next.backgroundAnimation):0;
            continue;
        }
        if(key=="collections_view"||key=="radio_view"||key=="playlists_view"){if(!v.is_string()||(v!="list"&&v!="grid")){error="Choose List or Grid.";return false;}if(key=="collections_view")next.collectionsView=v.get<std::string>();else if(key=="radio_view")next.radioView=v.get<std::string>();else next.playlistsView=v.get<std::string>();continue;}
        if(key=="visualizer_colour"){if(!v.is_string()){error="Invalid visualizer colour.";return false;}auto colour=v.get<std::string>();if(colour!="theme"&&(colour.size()!=7||colour[0]!='#'||colour.find_first_not_of("0123456789abcdefABCDEF",1)!=colour.npos)){error="Invalid visualizer colour.";return false;}next.visualizerColour=colour;continue;}
        if(key=="look"){if(!v.is_string()||(v!="modern"&&v!="classic")){error="Invalid look.";return false;}next.look=v.get<std::string>();continue;}
        if(key=="menu_opacity"||key=="background_speed"){if(!v.is_number_integer()){error="Expected an integer: "+key;return false;}auto n=v.get<int64_t>();if((key=="menu_opacity"&&(n<50||n>100||n%5))||(key=="background_speed"&&n!=25&&n!=50&&n!=75&&n!=100&&n!=125&&n!=150&&n!=200)){error="Appearance value out of range.";return false;}if(key=="menu_opacity")next.menuOpacity=int(n);else next.backgroundSpeed=int(n);continue;}
        if(key=="background_animation"){if(!v.is_number_integer()||v.get<int64_t>()<0||v.get<int64_t>()>3){error="Invalid background animation.";return false;}next.backgroundAnimation=v.get<int>();next.animatedBackground=next.backgroundAnimation!=0;continue;}
        if(key=="visualizer_expand"){
            if(!v.is_boolean()){error="Expected a boolean: "+key;return false;}
            next.visualizerExpand=v.get<bool>();continue;
        }
        if(key=="visualizer_style"||key=="visualizer_sensitivity"||key=="visualizer_smoothing"){
            if(!v.is_number_integer()){error="Expected an integer: "+key;return false;}
            auto n=v.get<int64_t>();int lo=key=="visualizer_sensitivity"?50:0;
            int hi=key=="visualizer_style"?18:key=="visualizer_sensitivity"?200:95;
            if(n<lo||n>hi||(key=="visualizer_style"&&!validVisualizerStyle(int(n)))){error="Visualizer value out of range.";return false;}
            if(key=="visualizer_style")next.visualizerStyle=int(n);
            else if(key=="visualizer_sensitivity")next.visualizerSensitivity=int(n);
            else next.visualizerSmoothing=int(n);
            continue;
        }
        if(key=="eq_reset"){
            if(!v.is_boolean()){error="Expected a boolean: "+key;return false;}
            if(v.get<bool>())next.eq={};continue;
        }
        if(key=="eq_preset"){
            if(!v.is_number_integer()){error="Invalid EQ preset.";return false;}
            auto index=v.get<int64_t>();if(index< -1||index>=int(presets.size())){error="Invalid EQ preset.";return false;}
            if(index>=0){next.eq=presets[size_t(index)].gains;next.eqEnabled=true;}continue;
        }
        if(key.size()==4&&key.rfind("eq_",0)==0&&key[3]>='0'&&key[3]<='4'){
            if(!v.is_number()){error="Expected EQ gain.";return false;}
            double gain=v.get<double>();if(!std::isfinite(gain)||gain< -6||gain>6){error="EQ gain must be between -6 and 6 dB.";return false;}
            next.eq[key[3]-'0']=float(gain);next.eqEnabled=true;continue;
        }
        if(key=="storage_folders"){
            if(!v.is_object()||v.size()>10){error="Invalid storage folders.";return false;}
            std::map<std::string,std::string> folders;
            for(auto it=v.begin();it!=v.end();++it){
                if(!it.value().is_string()||!validStorageFolder(it.key(),it.value().get<std::string>())){error="Choose a music folder on that drive.";return false;}
                folders[it.key()]=it.value().get<std::string>();
            }
            next.storageFolders=std::move(folders);continue;
        }
        if (key == "theme" || key == "accent_colour" || key == "music_root") {
            if (!v.is_string()) {
                error = "Expected text: " + key;
                return false;
            }
            std::string text = v.get<std::string>();
            if (key == "theme") {
                if (text != "dark" && text != "light" && text != "oled") {
                    error = "Invalid theme.";
                    return false;
                }
                next.theme = text;
            }
            if (key == "accent_colour") {
                if (!normalizeColour(text)) {
                    error = "Use a six-digit hex colour.";
                    return false;
                }
                next.accent = text;
            }
            if (key == "music_root") {
                if (!validLocalRoot(text) || text.size() > 1024 ||
                    text.find('\0') != std::string::npos) {
                    error = "SD folder must start with sdmc:/.";
                    return false;
                }
                next.musicRoot = text;
            }
        } else if (key == "screensaver_seconds" || key == "remote_port" || key == "volume" ||
                   key == "repeat") {
            if (!v.is_number_integer()) {
                error = "Expected an integer: " + key;
                return false;
            }
            auto n = v.get<int64_t>();
            if (key == "screensaver_seconds") {
                if (std::find(saverOptions.begin(), saverOptions.end(), n) == saverOptions.end()) {
                    error = "Invalid screensaver timeout.";
                    return false;
                }
                next.screensaverSeconds = n;
            }
            if (key == "remote_port") {
                if (n != 8182) {
                    error = "The remote port is fixed at 8182.";
                    return false;
                }
                next.remotePort = n;
            }
            if (key == "volume") {
                if (n < 0 || n > 100) {
                    error = "Volume must be between 0 and 100.";
                    return false;
                }
                next.volume = n;
            }
            if (key == "repeat") {
                if (n < 0 || n > 2) {
                    error = "Invalid repeat mode.";
                    return false;
                }
                next.repeat = n;
            }
        } else if (key == "eq") {
            if (!v.is_array() || v.size() != 5) {
                error = "Five EQ bands are required.";
                return false;
            }
            for (size_t i = 0; i < 5; i++) {
                if (!v[i].is_number()) {
                    error = "Invalid EQ gain.";
                    return false;
                }
                auto gain = v[i].get<float>();
                if (!std::isfinite(gain) || gain < -6 || gain > 6) {
                    error = "EQ range is -6 to +6 dB.";
                    return false;
                }
                next.eq[i] = gain;
            }
        } else {
            error = "Unknown setting: " + key;
            return false;
        }
    }
    next.wrapNavigation = next.holdRepeat = true;
    s = std::move(next);
    return true;
}
bool readShare(const nlohmann::json &j, const Share *previous, Share &out, std::string &error) {
    if (!j.is_object()) {
        error = "Invalid share.";
        return false;
    }
    out = previous ? *previous : Share{};
    auto field = [&](const char *key, std::string &dst) {
        if (!j.contains(key))
            return true;
        if (!j[key].is_string())
            return false;
        auto value = j[key].get<std::string>();
        if (value.size() > 1024 || value.find('\0') != std::string::npos)
            return false;
        dst = value;
        return true;
    };
    if (!field("name", out.name) || !field("server", out.server) || !field("share", out.share) ||
        !field("folder", out.folder) || !field("username", out.user) ||
        !field("domain", out.domain)) {
        error = "Invalid share field.";
        return false;
    }
    if (j.contains("guest")) {
        if (!j["guest"].is_boolean()) {
            error = "Invalid guest setting.";
            return false;
        }
        out.guest = j["guest"].get<bool>();
    }
    if (j.contains("clear_password")) {
        if (!j["clear_password"].is_boolean()) {
            error = "Invalid password setting.";
            return false;
        }
        if (j["clear_password"].get<bool>())
            out.password.clear();
    }
    if (j.contains("password")) {
        if (!j["password"].is_string()) {
            error = "Invalid password.";
            return false;
        }
        auto value = j["password"].get<std::string>();
        if (value.size() > 1024 || value.find('\0') != std::string::npos) {
            error = "Invalid password.";
            return false;
        }
        if (!value.empty())
            out.password = value;
    }
    if (!validate(out, error))
        return false;
    if (out.name.empty())
        out.name = out.server + " / " + out.share;
    return true;
}
bool loadSettings(const std::string &path, Settings &s, std::string &error) {
    std::ifstream f(path);
    if (!f) {
        f.open(path + ".bak");
        if (!f)
            return true;
    }
    auto j = nlohmann::json::parse(f, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        error = "Invalid config.json. Existing settings have not been overwritten.";
        return false;
    }
    auto text = [&](const nlohmann::json &o, const char *k, const std::string &fallback = "") {
        auto i = o.find(k);
        return i != o.end() && i->is_string() ? i->get<std::string>() : fallback;
    };
    s.musicRoot = text(j, "music_root", s.musicRoot);
    if(j.contains("storage_folders")&&j["storage_folders"].is_object()){
        for(auto it=j["storage_folders"].begin();it!=j["storage_folders"].end();++it)
            if(it.value().is_string()&&validStorageFolder(it.key(),it.value().get<std::string>()))s.storageFolders[it.key()]=it.value().get<std::string>();
    }
    // Migrate a previous combined Local/USB selection into its own source.
    if(s.musicRoot.rfind("/mnt/",0)==0&&s.musicRoot.size()>=9){
        auto mount=s.musicRoot.substr(0,9);
        if(validStorageFolder(mount,s.musicRoot))s.storageFolders.emplace(mount,s.musicRoot);
        s.musicRoot="/data/music";
    }
    auto theme = text(j, "theme", "dark");
    s.theme = theme == "light" || theme == "oled" ? theme : "dark";
    if(!j.contains("background_animation")&&j.contains("animated_background")&&j["animated_background"].is_boolean()){s.animatedBackground=j["animated_background"].get<bool>();s.backgroundAnimation=s.animatedBackground?1:0;}
    auto accent = text(j, "accent_colour", "#3B82F6");
    s.accent = normalizeColour(accent) ? accent : "#3B82F6";
    if (j.contains("volume") && j["volume"].is_number_integer())
        s.volume = std::clamp(j["volume"].get<int>(), 0, 100);
    if (j.contains("repeat") && j["repeat"].is_number_integer())
        s.repeat = std::clamp(j["repeat"].get<int>(), 0, 2);
    if (j.contains("shuffle") && j["shuffle"].is_boolean())
        s.shuffle = j["shuffle"];
    if (j.contains("eq_enabled") && j["eq_enabled"].is_boolean())
        s.eqEnabled = j["eq_enabled"];
    if (j.contains("eq") && j["eq"].is_array() && j["eq"].size() == 5)
        for (int i = 0; i < 5; i++)
            if (j["eq"][i].is_number())
                s.eq[i] = std::clamp(j["eq"][i].get<float>(), -6.f, 6.f);
    for(auto key:{"collections_view","playlists_view","radio_view"})if(j.contains(key)){std::string ignored;patchSettings(s,nlohmann::json{{key,j[key]}},ignored);}
    auto fields = settingFields();
    for (auto &field : fields) {
        std::string key = field["key"];
        if (key == "theme" || key == "accent_colour" || key == "remote_address" || key.rfind("eq_",0)==0)
            continue;
        if (j.contains(key)) {
            auto copy = s;
            std::string ignored;
            if (patchSettings(copy, nlohmann::json{{key, j[key]}}, ignored))
                s = std::move(copy);
        }
    }
    if (!s.rememberShuffleRepeat) {
        s.shuffle = false;
        s.repeat = 0;
    }
    s.remotePort = 8182;
    s.wrapNavigation = s.holdRepeat = true;
    s.remoteClients.clear();
    if (j.contains("remote_clients") && j["remote_clients"].is_array())
        for (auto &v : j["remote_clients"]) {
            if (!v.is_object() || !v.value("port", nlohmann::json(8182)).is_number_integer())
                continue;
            RemoteClientConfig c;
            if (!v.value("name", nlohmann::json("")).is_string() ||
                !v.value("host", nlohmann::json("")).is_string() ||
                !v.value("type", nlohmann::json("nx")).is_string())
                continue;
            c.name = v.value("name", std::string{});
            c.host = v.value("host", std::string{});
            c.type = v.value("type", std::string("nx"));
            c.port = 8182;
            std::string e;
            if (validateRemoteClient(c, e) && s.remoteClients.size() < 32)
                s.remoteClients.push_back(c);
        }
    s.shares.clear();
    if (j.contains("shares") && j["shares"].is_array())
        for (auto &x : j["shares"]) {
            if (!x.is_object())
                continue;
            Share v;
            v.name = text(x, "name");
            v.server = text(x, "server");
            v.share = text(x, "share");
            v.folder = text(x, "folder");
            v.user = text(x, "username");
            v.password = text(x, "password");
            v.domain = text(x, "domain");
            v.guest =
                x.contains("guest") && x["guest"].is_boolean() ? x["guest"].get<bool>() : false;
            std::string ignored;
            if (validate(v, ignored))
                s.shares.push_back(v);
        }
    return true;
}
bool saveSettings(const std::string &path, const Settings &s, std::string &error) {
    auto document = settingsDocument(s, true);
    if (!s.rememberShuffleRepeat) {
        document["shuffle"] = false;
        document["repeat"] = 0;
    }
    auto data = document.dump(2);
    auto temp = path + ".tmp";
    FILE *f = std::fopen(temp.c_str(), "wb");
    if (!f) {
        error = "Cannot create config file. Check access to sdmc:/switch/nx-hifi.";
        return false;
    }
    bool okay = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    if (std::fclose(f) != 0)
        okay = false;
    if (!okay) {
        std::remove(temp.c_str());
        error = "Could not write settings.";
        return false;
    }
    // FAT-safe backup: also allows recovery if power is lost between renames.
    auto backup = path + ".bak";
    struct stat st{};
    bool exists = stat(path.c_str(), &st) == 0;
    if (exists) {
        std::remove(backup.c_str());
        if (std::rename(path.c_str(), backup.c_str()) != 0) {
            std::remove(temp.c_str());
            error = "Could not back up settings.";
            return false;
        }
    }
    if (std::rename(temp.c_str(), path.c_str()) != 0) {
        if (exists)
            std::rename(backup.c_str(), path.c_str());
        error = "Could not replace settings.";
        return false;
    }
    return true;
}
namespace {
bool naturalNameLess(const std::string &left,const std::string &right){
    auto a=lower(left),b=lower(right);size_t i=0,j=0;
    auto digit=[](unsigned char c){return c>='0'&&c<='9';};
    while(i<a.size()&&j<b.size()){
        if(digit(a[i])&&digit(b[j])){
            size_t endA=i,endB=j;while(endA<a.size()&&digit(a[endA]))endA++;while(endB<b.size()&&digit(b[endB]))endB++;
            size_t startA=i,startB=j;while(startA<endA&&a[startA]=='0')startA++;while(startB<endB&&b[startB]=='0')startB++;
            size_t lengthA=endA-startA,lengthB=endB-startB;
            // Compare digit strings rather than converting: arbitrarily large numbers are safe.
            if(lengthA!=lengthB)return lengthA<lengthB;
            int numeric=a.compare(startA,lengthA,b,startB,lengthB);if(numeric)return numeric<0;
            if(endA-i!=endB-j)return endA-i<endB-j; // Equal values: fewer leading zeroes first.
            i=endA;j=endB;continue;
        }
        auto ca=static_cast<unsigned char>(a[i]),cb=static_cast<unsigned char>(b[j]);
        if(ca!=cb)return ca<cb;i++;j++;
    }
    if(i!=a.size()||j!=b.size())return i==a.size();
    return left<right; // Deterministic order for case-only differences.
}
}
void sortEntries(std::vector<Entry> &entries){
    std::sort(entries.begin(),entries.end(),[](const auto &a,const auto &b){
        return a.directory!=b.directory?a.directory:naturalNameLess(a.name,b.name);
    });
}
LocalInput::LocalInput(const std::string &p) {
    file_ = std::fopen(p.c_str(), "rb");
    if (file_) {
        fseeko(file_, 0, SEEK_END);
        auto n = ftello(file_);
        if (n >= 0)
            size_ = n;
        fseeko(file_, 0, SEEK_SET);
    }
}
LocalInput::~LocalInput() {
    if (file_)
        std::fclose(file_);
}
size_t LocalInput::read(void *d, size_t n) {
    if (!file_)
        return 0;
    auto got = std::fread(d, 1, n, file_);
    pos_ += got;
    return got;
}
bool LocalInput::seek(int64_t n, int origin) {
    int64_t target = origin == SEEK_SET   ? n
                     : origin == SEEK_CUR ? int64_t(pos_) + n
                                          : int64_t(size_) + n;
    if (!file_ || target < 0 || uint64_t(target) > size_ || fseeko(file_, target, SEEK_SET))
        return false;
    pos_ = target;
    return true;
}
BufferedInput::BufferedInput(std::unique_ptr<Input> i, size_t cap)
    : inner_(std::move(i)), buffer_(std::max(size_t(4096), cap)) {}
size_t BufferedInput::read(void *dest, size_t n) {
    size_t done = 0;
    while (done < n && pos_ < size()) {
        if (pos_ < start_ || pos_ >= start_ + count_) {
            if (!inner_->seek(pos_, SEEK_SET))
                break;
            start_ = pos_;
            count_ =
                inner_->read(buffer_.data(), std::min<uint64_t>(std::min(buffer_.size(),refill_), size() - pos_));
            refill_=std::min(buffer_.size(),refill_*2);
            if (!count_)
                break;
        }
        auto amount = std::min(n - done, count_ - size_t(pos_ - start_));
        std::memcpy(static_cast<unsigned char *>(dest) + done, buffer_.data() + pos_ - start_,
                    amount);
        pos_ += amount;
        done += amount;
    }
    return done;
}
bool BufferedInput::seek(int64_t n, int origin) {
    int64_t target = origin == SEEK_SET   ? n
                     : origin == SEEK_CUR ? int64_t(pos_) + n
                                          : int64_t(size()) + n;
    if (target < 0 || uint64_t(target) > size())
        return false;
    pos_ = target;
    return true;
}
void Biquad::peak(float hz, float gain) {
    constexpr double pi = 3.141592653589793;
    double A = std::pow(10., gain / 40.), w = 2 * pi * hz / 44100.,
           alpha = std::sin(w) / (2 * .707), a0 = 1 + alpha / A;
    b0 = (1 + alpha * A) / a0;
    b1 = -2 * std::cos(w) / a0;
    b2 = (1 - alpha * A) / a0;
    a1 = -2 * std::cos(w) / a0;
    a2 = (1 - alpha / A) / a0;
}
float Biquad::process(float x) {
    float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
}
void Equalizer::configure(bool enabled, const std::array<float, 5> &gains) {
    if (enabled == enabled_ && gains == current_)
        return;
    enabled_ = enabled;
    current_ = gains;
    float total = 0;
    constexpr float hz[] = {60, 250, 1000, 4000, 12000};
    for (int i = 0; i < 5; i++) {
        float g = std::clamp(gains[i], -6.f, 6.f);
        total += std::max(0.f, g);
        for (auto &b : bands_[i])
            b.peak(hz[i], g);
    }
    headroom_ = std::pow(10.f, -total / 20.f);
    reset();
}
void Equalizer::reset() {
    for (auto &band : bands_)
        for (auto &b : band)
            b.reset();
}
void Equalizer::process(float *s, size_t frames) {
    if (!enabled_)
        return;
    for (size_t i = 0; i < frames; i++)
        for (int c = 0; c < 2; c++) {
            float v = s[2 * i + c] * headroom_;
            for (auto &b : bands_)
                v = b[c].process(v);
            s[2 * i + c] = std::clamp(v, -1.f, 1.f);
        }
}
} // namespace nxh
