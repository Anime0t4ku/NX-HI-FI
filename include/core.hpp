#pragma once
#include "json.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace nxh {
constexpr const char *Version = "1.1.0";
struct Share {
    std::string name, server, share, folder, user, password, domain;
    bool guest = false;
};
struct RemoteClientConfig {
    std::string name, host, type = "nx";
    int port = 8182;
};
bool validateRemoteClient(const RemoteClientConfig &, std::string &);
struct Settings {
    std::string musicRoot;
    std::string theme = "dark", accent = "#EF4444";
    bool showClock = true, clock24h = true, showArtwork = true, hideMissingArt = false;
    bool preferFolderArt = false, visualizer = true, rememberShuffleRepeat = true;
    bool wrapNavigation = true, holdRepeat = true, remoteEnabled = false;
    int screensaverSeconds = 0, remotePort = 8182;
    int volume = 80;
    bool shuffle = false, gaplessPlayback = true;
    int repeat = 0;
    bool eqEnabled = false;
    std::array<float, 5> eq{};
    std::vector<Share> shares;
    std::vector<RemoteClientConfig> remoteClients;
};
struct AccentPreset {
    const char *name, *hex;
};
inline constexpr std::array<AccentPreset, 10> accentPresets{{{"Red", "#EF4444"},
                                                             {"Orange", "#F97316"},
                                                             {"Yellow", "#EAB308"},
                                                             {"Green", "#22C55E"},
                                                             {"Teal", "#14B8A6"},
                                                             {"Cyan", "#06B6D4"},
                                                             {"Blue", "#3B82F6"},
                                                             {"Purple", "#A855F7"},
                                                             {"Pink", "#EC4899"},
                                                             {"Grey", "#808080"}}};
bool normalizeColour(std::string &hex);
std::array<int, 3> colourRGB(const std::string &hex);
std::string colourHex(const std::array<int, 3> &rgb);
// Back-and-forth title movement with a pause at either end.
inline double marqueeOffset(int textWidth, int viewport, double elapsed) {
    double distance = std::max(0, textWidth - viewport);
    if (distance == 0)
        return 0;
    double travel = distance / 38., pause = 1.4;
    double phase = std::fmod(std::max(0., elapsed), 2 * (pause + travel));
    if (phase < pause)
        return 0;
    if (phase < pause + travel)
        return (phase - pause) * 38.;
    if (phase < 2 * pause + travel)
        return distance;
    return std::max(0., distance - (phase - 2 * pause - travel) * 38.);
}
struct Preset {
    const char *name;
    std::array<float, 5> gains;
};
extern const std::array<Preset, 11> presets;
std::string lower(std::string s);
std::string filename(const std::string &p);
std::string title(const std::string &p);
bool audioFile(const std::string &p);
bool safeRelative(const std::string &p);
std::string join(const std::string &a, const std::string &b);
std::string parent(const std::string &p);
bool validate(const Share &s, std::string &error);
bool loadSettings(const std::string &path, Settings &settings, std::string &error);
bool saveSettings(const std::string &path, const Settings &settings, std::string &error);
nlohmann::json settingsDocument(const Settings &, bool secrets = false);
nlohmann::json settingFields();
bool patchSettings(Settings &, const nlohmann::json &, std::string &error);
bool readShare(const nlohmann::json &, const Share *previous, Share &, std::string &error);
std::string formatClock(int hour, int minute, bool clock24h);
inline constexpr std::array<int, 7> saverOptions{{0, 30, 60, 120, 300, 600, 1800}};
struct Entry {
    std::string name, path;
    bool directory = false;
};
void sortEntries(std::vector<Entry> &entries);
struct Source {
    bool smb = false;
    Share share;
    std::string root;
};
struct Track {
    Source source;
    std::string path, name;
};
class Input {
  public:
    virtual ~Input() = default;
    virtual std::string error() const { return {}; }
    virtual size_t read(void *dst, size_t size) = 0;
    virtual bool seek(int64_t offset, int origin) = 0;
    virtual uint64_t position() const = 0;
    virtual uint64_t size() const = 0;
};
class LocalInput final : public Input {
    FILE *file_ = nullptr;
    uint64_t size_ = 0, pos_ = 0;

  public:
    explicit LocalInput(const std::string &path);
    ~LocalInput();
    bool valid() const { return file_ != nullptr; }
    size_t read(void *, size_t) override;
    bool seek(int64_t, int) override;
    uint64_t size() const override { return size_; }
    uint64_t position() const override { return pos_; }
};
// One-page read-ahead cache. Seeks inside the page never issue a remote read.
class BufferedInput final : public Input {
    std::unique_ptr<Input> inner_;
    std::vector<unsigned char> buffer_;
    uint64_t pos_ = 0, start_ = 0;
    size_t count_ = 0;

  public:
    explicit BufferedInput(std::unique_ptr<Input> inner, size_t capacity = 512 * 1024);
    std::string error() const override { return inner_->error(); }
    size_t read(void *, size_t) override;
    bool seek(int64_t, int) override;
    uint64_t size() const override { return inner_->size(); }
    uint64_t position() const override { return pos_; }
};
struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void peak(float hz, float gain);
    float process(float x);
    void reset() { z1 = z2 = 0; }
};
class Equalizer {
    std::array<std::array<Biquad, 2>, 5> bands_;
    std::array<float, 5> current_{};
    bool enabled_ = false;
    float headroom_ = 1;

  public:
    void configure(bool enabled, const std::array<float, 5> &gains);
    void reset();
    void process(float *samples, size_t frames);
};
} // namespace nxh
