#include "core.hpp"
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
    return ext == ".flac" || ext == ".mp3" || ext == ".wav" || ext == ".ogg" || ext == ".oga";
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
        value = "#EF4444";
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
        c.port != 8182 || (c.type != "nx" && c.type != "mister")) {
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
    return {{"music_root", s.musicRoot},
            {"volume", s.volume},
            {"shuffle", s.shuffle},
            {"repeat", s.repeat},
            {"eq_enabled", s.eqEnabled},
            {"eq", s.eq},
            {"shares", shares},
            {"remote_clients", clients},
            {"theme", s.theme},
            {"accent_colour", s.accent},
            {"show_clock", s.showClock},
            {"clock_24h", s.clock24h},
            {"show_artwork", s.showArtwork},
            {"hide_missing_art", s.hideMissingArt},
            {"prefer_folder_art", s.preferFolderArt},
            {"visualizer", s.visualizer},
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
    add("accent_colour", "Accent colour", "Appearance", "colour");
    add("show_clock", "Show clock", "Appearance", "bool");
    add("clock_24h", "24-hour clock", "Appearance", "bool");
    add("show_artwork", "Show artwork", "Artwork", "bool");
    add("hide_missing_art", "Hide missing artwork placeholder", "Artwork", "bool");
    add("prefer_folder_art", "Prefer folder artwork", "Artwork", "bool");
    add("visualizer", "Show visualizer", "Visualizer", "bool");
    add("gapless_playback", "Gapless audio CD playback", "Playback", "bool");
    add("remember_shuffle_repeat", "Remember shuffle / repeat", "Playback", "bool");
    add("screensaver_seconds", "Screensaver", "Display", "choice",
        {0, 30, 60, 120, 300, 600, 1800});
    add("remote_enabled", "Enable web remote", "Remote", "bool");

    add("remote_address", "Remote address", "Remote", "info");
    return rows;
}
bool patchSettings(Settings &s, const nlohmann::json &patch, std::string &error) {
    if (!patch.is_object()) {
        error = "Settings must be an object.";
        return false;
    }
    auto next = s;
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        auto key = it.key();
        auto &v = it.value();
        bool *flag = nullptr;
        if (key == "show_clock")
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
            continue;
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
                if (text.rfind("sdmc:/", 0) != 0 || text.size() > 1024 ||
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
    auto theme = text(j, "theme", "dark");
    s.theme = theme == "light" || theme == "oled" ? theme : "dark";
    auto accent = text(j, "accent_colour", "#EF4444");
    s.accent = normalizeColour(accent) ? accent : "#EF4444";
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
    auto fields = settingFields();
    for (auto &field : fields) {
        std::string key = field["key"];
        if (key == "theme" || key == "accent_colour" || key == "remote_address")
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
        error = "Cannot create config file. Check the SD card.";
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
void sortEntries(std::vector<Entry> &e) {
    std::sort(e.begin(), e.end(), [](auto &a, auto &b) {
        return a.directory != b.directory ? a.directory : lower(a.name) < lower(b.name);
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
                inner_->read(buffer_.data(), std::min<uint64_t>(buffer_.size(), size() - pos_));
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
