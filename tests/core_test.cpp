#include "core.hpp"
#include "artwork.hpp"
#include "metadata.hpp"
#include "decoder.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
using namespace nxh;
class MemoryInput : public Input {
  public:
    std::vector<unsigned char> data;
    uint64_t pos = 0;
    int reads = 0;
    size_t read(void *out, size_t n) override {
        reads++;
        n = std::min<uint64_t>(n, data.size() - pos);
        std::memcpy(out, data.data() + pos, n);
        pos += n;
        return n;
    }
    bool seek(int64_t n, int origin) override {
        int64_t at = origin == SEEK_SET   ? n
                     : origin == SEEK_CUR ? int64_t(pos) + n
                                          : int64_t(data.size()) + n;
        if (at < 0 || uint64_t(at) > data.size())
            return false;
        pos = at;
        return true;
    }
    uint64_t position() const override {
        return pos;
    }
    uint64_t size() const override {
        return data.size();
    }
};
static ma_result readAudio(ma_decoder *d, void *out, size_t n, size_t *got) {
    *got = static_cast<Input *>(d->pUserData)->read(out, n);
    return *got ? MA_SUCCESS : MA_AT_END;
}
static ma_result seekAudio(ma_decoder *d, ma_int64 n, ma_seek_origin origin) {
    return static_cast<Input *>(d->pUserData)
                   ->seek(n, origin == ma_seek_origin_start     ? SEEK_SET
                             : origin == ma_seek_origin_current ? SEEK_CUR
                                                                : SEEK_END)
               ? MA_SUCCESS
               : MA_BAD_SEEK;
}
static std::vector<unsigned char> wav() {
    std::vector<unsigned char> b(44 + 44100 * 4);
    auto put = [&](size_t at, uint32_t x, int n) {
        for (int i = 0; i < n; i++)
            b[at + i] = (x >> (8 * i)) & 255;
    };
    std::memcpy(b.data(), "RIFF", 4);
    put(4, b.size() - 8, 4);
    std::memcpy(b.data() + 8, "WAVEfmt ", 8);
    put(16, 16, 4);
    put(20, 1, 2);
    put(22, 2, 2);
    put(24, 44100, 4);
    put(28, 44100 * 4, 4);
    put(32, 4, 2);
    put(34, 16, 2);
    std::memcpy(b.data() + 36, "data", 4);
    put(40, b.size() - 44, 4);
    for (int i = 0; i < 44100; i++) {
        int16_t sample = std::sin(i * 2 * 3.141592653589793 * 440 / 44100) * 12000;
        put(44 + i * 4, uint16_t(sample), 2);
        put(46 + i * 4, uint16_t(sample), 2);
    }
    return b;
}
static void testArtwork() {
    const std::vector<unsigned char> image = {137, 'P', 'N', 'G', 13, 10, 26, 10, 1, 2, 3};
    auto valid = [&](const std::vector<unsigned char> &v) { return v == image; };
    auto be = [](std::vector<unsigned char> &b, uint32_t n, int bytes = 4, bool sync = false) {
        for (int i = bytes - 1; i >= 0; i--)
            b.push_back((n >> ((sync ? 7 : 8) * i)) & (sync ? 127 : 255));
    };
    for (int version : {2, 3, 4}) {
        std::vector<unsigned char> pic = {0, 'i', 'm', 'a', 'g', 'e', '/', 'p', 'n', 'g', 0, 3, 0};
        pic.insert(pic.end(), 200, 0); // exercise synchsafe frame lengths above 127
        pic.insert(pic.end(), image.begin(), image.end());
        std::vector<unsigned char> frame;
        std::string id = version == 2 ? "PIC" : "APIC";
        frame.insert(frame.end(), id.begin(), id.end());
        be(frame, pic.size(), version == 2 ? 3 : 4, version == 4);
        if (version != 2) {
            frame.push_back(0);
            frame.push_back(0);
        }
        frame.insert(frame.end(), pic.begin(), pic.end());
        MemoryInput in;
        in.data = {'I', 'D', '3', static_cast<unsigned char>(version), 0, 0};
        be(in.data, frame.size(), 4, true);
        in.data.insert(in.data.end(), frame.begin(), frame.end());
        assert(embeddedCover(in, valid) == image);
        in.pos = 0;
        assert(embeddedCover(in, [](const auto &) { return false; }).empty());
        in.data.resize(in.data.size() - 1);
        in.pos = 0;
        assert(embeddedCover(in, valid).empty());
    }
    std::vector<unsigned char> pic;
    be(pic, 3);
    be(pic, 9);
    for (char c : std::string("image/png"))
        pic.push_back(c);
    be(pic, 0);
    be(pic, 100);
    be(pic, 100);
    be(pic, 24);
    be(pic, 0);
    be(pic, image.size());
    pic.insert(pic.end(), image.begin(), image.end());
    MemoryInput flac;
    flac.data = {'f', 'L', 'a', 'C', 0x86};
    be(flac.data, pic.size(), 3);
    flac.data.insert(flac.data.end(), pic.begin(), pic.end());
    assert(embeddedCover(flac, valid) == image);
    flac.pos = 0;
    flac.data[12] = 255; // malformed MIME length must never escape bounds
    assert(embeddedCover(flac, valid).empty());
    MemoryInput oversized;
    oversized.data = {'I', 'D', '3', 4, 0, 0, 127, 127, 127, 127};
    assert(embeddedCover(oversized, valid).empty());
    assert(!Share{}.guest);
}

static void testMetadata() {
    auto be = [](std::vector<unsigned char> &b, uint32_t n, int bytes = 4, bool sync = false) {
        for (int i = bytes - 1; i >= 0; i--)
            b.push_back((n >> ((sync ? 7 : 8) * i)) & (sync ? 127 : 255));
    };
    auto le = [](std::vector<unsigned char> &b, uint32_t n, int bytes = 4) {
        for (int i = 0; i < bytes; i++)
            b.push_back((n >> (8 * i)) & 255);
    };
    std::string jp = "日本の音楽";
    for (int version : {2, 3, 4}) {
        std::vector<unsigned char> frames;
        auto frame = [&](std::string id, std::vector<unsigned char> value) {
            frames.insert(frames.end(), id.begin(), id.end());
            be(frames, value.size(), version == 2 ? 3 : 4, version == 4);
            if (version != 2) {
                frames.push_back(0);
                frames.push_back(0);
            }
            frames.insert(frames.end(), value.begin(), value.end());
        };
        std::vector<unsigned char> title = {3};
        title.insert(title.end(), jp.begin(), jp.end());
        frame(version == 2 ? "TT2" : "TIT2", title);
        // UTF-16 LE artist: A + U+1F3B5, including surrogate pair.
        frame(version == 2 ? "TP1" : "TPE1", {1, 255, 254, 'A', 0, 0x3c, 0xd8, 0xb5, 0xdf, 0, 0});
        frame(version == 2 ? "TAL" : "TALB", {0, 'C', 'a', 'f', 0xe9, 0});
        MemoryInput input;
        input.data = {'I', 'D', '3', static_cast<unsigned char>(version), 0, 0};
        be(input.data, frames.size(), 4, true);
        input.data.insert(input.data.end(), frames.begin(), frames.end());
        auto m = readTrackDetails(input, "test.mp3");
        assert(m.title == jp && m.artist == "A🎵" && m.album == "Café" && m.format == "MP3");
        assert(input.position() == 0); // decoder must always start at the file beginning
        input.data.resize(14);
        assert(readTrackDetails(input, "test.mp3").title.empty());
        assert(input.position() == 0);
    }
    std::vector<unsigned char> comments;
    le(comments, 4);
    for (char c : std::string("test"))
        comments.push_back(c);
    le(comments, 3);
    for (auto text :
         {std::string("TITLE=") + jp, std::string("ARTIST=Artist"), std::string("ALBUM=Album")}) {
        le(comments, text.size());
        comments.insert(comments.end(), text.begin(), text.end());
    }
    MemoryInput flac;
    flac.data = {'f', 'L', 'a', 'C', 0, 0, 0, 34};
    std::vector<unsigned char> info(34);
    uint32_t rate = 96000, channels = 2, bits = 24;
    info[10] = rate >> 12;
    info[11] = rate >> 4;
    info[12] = ((rate & 15) << 4) | ((channels - 1) << 1) | ((bits - 1) >> 4);
    info[13] = (bits - 1) << 4;
    flac.data.insert(flac.data.end(), info.begin(), info.end());
    // A picture block is skipped rather than decoded by the text reader.
    flac.data.push_back(6);
    be(flac.data, 128, 3);
    flac.data.insert(flac.data.end(), 128, 0);
    flac.data.push_back(0x84);
    be(flac.data, comments.size(), 3);
    flac.data.insert(flac.data.end(), comments.begin(), comments.end());
    auto m = readTrackDetails(flac, "test.flac");
    assert(m.title == jp && m.artist == "Artist" && m.album == "Album");
    assert(m.sampleRate == 96000 && m.channels == 2 && m.bitDepth == 24 && flac.position() == 0);
    assert(audioDetails(m) == "FLAC / 24 bit / 96 kHz");
    m.sampleRate = 44100;
    assert(audioDetails(m) == "FLAC / 24 bit / 44.1 kHz");
    MemoryInput wave;
    wave.data = wav();
    m = readTrackDetails(wave, "test.wav");
    assert(m.sampleRate == 44100 && m.channels == 2 && m.bitDepth == 16 && wave.position() == 0);
    std::vector<unsigned char> infoList = {'I', 'N', 'F', 'O', 'I', 'N', 'A', 'M'};
    le(infoList, 6);
    for (char c : std::string("Title"))
        infoList.push_back(c);
    infoList.push_back(0);
    for (char c : std::string("LIST"))
        wave.data.push_back(c);
    le(wave.data, infoList.size());
    wave.data.insert(wave.data.end(), infoList.begin(), infoList.end());
    assert(readTrackDetails(wave, "test.wav").title == "Title");
    MemoryInput ogg;
    auto page = [&](const std::vector<unsigned char> &packet, bool first) {
        std::vector<unsigned char> h(27);
        std::memcpy(h.data(), "OggS", 4);
        h[5] = first ? 2 : 0;
        h[14] = 1;
        size_t segments = packet.size() / 255 + 1;
        h[26] = segments;
        ogg.data.insert(ogg.data.end(), h.begin(), h.end());
        for (size_t i = 0; i < segments; i++)
            ogg.data.push_back(
                std::min<size_t>(255, packet.size() - std::min(packet.size(), i * 255)));
        ogg.data.insert(ogg.data.end(), packet.begin(), packet.end());
    };
    std::vector<unsigned char> ident(30);
    ident[0] = 1;
    std::memcpy(ident.data() + 1, "vorbis", 6);
    ident[11] = 2;
    ident[12] = 0x80;
    ident[13] = 0xbb;
    page(ident, true);
    std::vector<unsigned char> comment = {3, 'v', 'o', 'r', 'b', 'i', 's'};
    comment.insert(comment.end(), comments.begin(), comments.end());
    page(comment, false);
    m = readTrackDetails(ogg, "test.ogg");
    assert(m.title == jp && m.artist == "Artist" && m.album == "Album" && m.sampleRate == 48000 &&
           m.channels == 2 && ogg.position() == 0);
    std::vector<unsigned char> malformed = {255, 255, 255, 255};
    metaComments(m, malformed);
    assert(m.title == jp);
    MemoryInput oversized;
    oversized.data = {'I', 'D', '3', 4, 0, 0, 127, 127, 127, 127, 0, 0};
    assert(readTrackDetails(oversized, "test.mp3").title.empty() && oversized.position() == 0);
    unsigned char broken[] = {0xf0, 0x28, 0x8c, 0x28};
    assert(!metaText(broken, sizeof broken).empty());
}

static void testAppearanceAndTiming() {
    Settings defaults;
    assert(defaults.theme == "dark" && defaults.accent == "#EF4444");
    for (auto &preset : accentPresets) {
        std::string hex = preset.hex;
        assert(normalizeColour(hex));
        assert(colourHex(colourRGB(hex)) == hex);
    }
    std::string hex = "1a2B3c";
    assert(normalizeColour(hex) && hex == "#1A2B3C");
    hex = "#GG0000";
    assert(!normalizeColour(hex));
    hex = "#123";
    assert(!normalizeColour(hex));
    assert(colourHex({-1, 256, 128}) == "#00FF80");
    assert(marqueeOffset(100, 200, 20) == 0);
    assert(marqueeOffset(1000, 500, 1) == 0);
    assert(marqueeOffset(1000, 500, 2) > 0);
    double travel = 500 / 38.;
    assert(std::abs(marqueeOffset(1000, 500, 1.4 + travel) - 500) < .01);
    assert(marqueeOffset(1000, 500, 2.8 + travel + 1) < 500);
    for (int i = 0; i < 1000; i++) {
        double offset = marqueeOffset(1000, 500, i * .1);
        assert(offset >= 0 && offset <= 500);
    }
    char path[] = "/tmp/nx-theme-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);
    unlink(path);
    std::string error;
    Settings saved;
    saved.theme = "oled";
    saved.accent = "#06B6D4";
    assert(saveSettings(path, saved, error));
    Settings loaded;
    assert(loadSettings(path, loaded, error));
    assert(loaded.theme == "oled" && loaded.accent == "#06B6D4");
    auto write = [&](const std::string &text) {
        FILE *f = std::fopen(path, "wb");
        assert(f);
        std::fwrite(text.data(), 1, text.size(), f);
        std::fclose(f);
    };
    write("{\"theme\":\"invalid\",\"accent_colour\":\"bad\"}");
    assert(loadSettings(path, loaded, error) && loaded.theme == "dark" &&
           loaded.accent == "#EF4444");
    write("{\"volume\":42}");
    assert(loadSettings(path, loaded, error));
    assert(loaded.volume == 42 && loaded.theme == "dark" && loaded.accent == "#EF4444");
    unlink(path);
    unlink((std::string(path) + ".bak").c_str());
    // MPEG-1 Layer III, 128 kbps at 44.1 kHz. Exercise Xing duration and bounded CBR fallback.
    MemoryInput mp3;
    mp3.data.resize(417 * 20);
    for (int i = 0; i < 20; i++) {
        size_t p = i * 417;
        mp3.data[p] = 255;
        mp3.data[p + 1] = 251;
        mp3.data[p + 2] = 144;
        mp3.data[p + 3] = 0;
    }
    auto cbr = readTrackDetails(mp3, "sample.mp3");
    assert(cbr.sampleRate == 44100 && cbr.bitRate == 128 && cbr.duration > 0 &&
           cbr.durationEstimated && mp3.position() == 0);
    std::memcpy(mp3.data.data() + 36, "Xing", 4);
    mp3.data[43] = 1;
    mp3.data[46] = 3;
    mp3.data[47] = 232; // 1000 frames
    auto vbr = readTrackDetails(mp3, "sample.mp3");
    assert(!vbr.durationEstimated && std::abs(vbr.duration - 1000 * 1152 / 44100.) < .001);
    assert(vbr.sampleRate == 44100 && mp3.position() == 0);
    mp3.data.assign(100, 0);
    assert(readTrackDetails(mp3, "sample.mp3").duration == 0);
}

static void testRemoteSettings() {
    Settings s;
    std::string error;
    assert(!s.remoteEnabled && s.remotePort == 8182 && s.showClock && s.clock24h);
    assert(patchSettings(s,
                         {{"show_clock", false},
                          {"clock_24h", false},
                          {"show_artwork", false},
                          {"hide_missing_art", true},
                          {"prefer_folder_art", true},
                          {"visualizer", false},
                          {"remember_shuffle_repeat", false},
                          {"wrap_navigation", true},
                          {"hold_repeat", false},
                          {"screensaver_seconds", 30},
                          {"remote_enabled", true},
                          {"remote_port", 8182}},
                         error));
    assert(!s.showClock && !s.clock24h && !s.showArtwork && s.hideMissingArt && s.preferFolderArt &&
           !s.visualizer && s.remoteEnabled && s.remotePort == 8182 && s.screensaverSeconds == 30);
    assert(s.wrapNavigation && s.holdRepeat);
    auto before = settingsDocument(s);
    assert(!patchSettings(s, {{"theme", "light"}, {"unknown", true}}, error));
    assert(settingsDocument(s) == before);
    assert(!patchSettings(s, {{"remote_port", 0}}, error));
    assert(!patchSettings(s, {{"remote_port", 65536}}, error));
    assert(!patchSettings(s, {{"screensaver_seconds", 7}}, error));
    assert(!patchSettings(s, {{"clock_24h", 1}}, error));
    assert(!patchSettings(s, {{"music_root", "/etc"}}, error));
    assert(!safeRelative(std::string("song\0.mp3", 9)));
    Share share;
    share.server = "NAS";
    share.share = "Music";
    share.user = "user";
    share.password = "example-private-password";
    s.shares = {share};
    auto publicSettings = settingsDocument(s);
    assert(!publicSettings["shares"][0].contains("password"));
    assert(publicSettings["shares"][0]["password_set"] == true);
    assert(publicSettings.dump().find(share.password) == std::string::npos);
    auto privateSettings = settingsDocument(s, true);
    assert(privateSettings["shares"][0]["password"] == share.password);
    Share edited;
    assert(readShare({{"name", "Renamed"}, {"password", ""}}, &share, edited, error));
    assert(edited.password == share.password && edited.name == "Renamed");
    assert(readShare({{"clear_password", true}}, &share, edited, error) && edited.password.empty());
    assert(!readShare({{"server", "smb://NAS"}}, &share, edited, error));
    assert(!readShare({{"guest", "true"}}, &share, edited, error));
    auto fields = settingFields();
    for (auto &f : fields) {
        auto key = f["key"].get<std::string>();
        assert(key != "wrap_navigation" && key != "hold_repeat");
        assert(key != "volume" && key != "shuffle" && key != "repeat" && key != "stop");
    }
    char path[] = "/tmp/nx-remote-config-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);
    unlink(path);
    s.shuffle = true;
    s.repeat = 2;
    s.remoteClients = {{"Living room", "192.168.1.20", "nx", 8182},
                       {"MiSTer", "mister.local", "mister", 8182}};
    assert(validateRemoteClient(s.remoteClients[0], error));
    assert(!validateRemoteClient({"bad", "http://host", "nx", 8182}, error));
    assert(!validateRemoteClient({"bad", "host", "nx", 0}, error));
    assert(!validateRemoteClient({"bad", "host", "other", 80}, error));
    assert(saveSettings(path, s, error));
    Settings loaded;
    assert(loadSettings(path, loaded, error));
    assert(!loaded.shuffle && loaded.repeat == 0 && !loaded.rememberShuffleRepeat);
    assert(loaded.wrapNavigation && loaded.holdRepeat);
    assert(loaded.remoteClients.size() == 2 && loaded.remoteClients[1].port == 8182);
    assert(loaded.remoteClients[0].port == 8182);
    auto legacy = settingsDocument(s, true);
    legacy["remote_port"] = 8080;
    legacy["remote_clients"][0]["port"] = 8080;
    {
        std::ofstream old(path);
        old << legacy.dump();
    }
    assert(loadSettings(path, loaded, error) && loaded.remotePort == 8182 &&
           loaded.remoteClients[0].port == 8182);
    assert(loaded.remoteEnabled && loaded.remotePort == 8182 && !loaded.showClock &&
           loaded.preferFolderArt);
    assert(loaded.shares.size() == 1 && loaded.shares[0].password == share.password);
    s.rememberShuffleRepeat = true;
    assert(saveSettings(path, s, error));
    assert(loadSettings(path, loaded, error) && loaded.shuffle && loaded.repeat == 2);
    unlink(path);
    unlink((std::string(path) + ".bak").c_str());
    assert(formatClock(0, 5, false) == "12:05 AM");
    assert(formatClock(12, 30, false) == "12:30 PM");
    assert(formatClock(23, 9, true) == "23:09");
}

int main() {
    testRemoteSettings();
    testAppearanceAndTiming();
    testMetadata();
    testArtwork();
    assert(safeRelative("Albums/Artist"));
    assert(!safeRelative("../secret"));
    assert(!safeRelative("Albums/../../secret"));
    assert(!safeRelative("/absolute"));
    assert(!safeRelative("Albums\\secret"));
    assert(audioFile("A.FLAC"));
    assert(!audioFile("A.m4a"));
    assert(!audioFile("README.txt"));
    auto mem = std::make_unique<MemoryInput>();
    mem->data.resize(65536);
    for (size_t i = 0; i < mem->data.size(); i++)
        mem->data[i] = i % 251;
    auto *raw = mem.get();
    BufferedInput buffered(std::move(mem), 4096);
    unsigned char bytes[150];
    assert(buffered.read(bytes, 150) == 150);
    int initial = raw->reads;
    assert(buffered.seek(20, SEEK_SET));
    assert(buffered.read(bytes, 100) == 100);
    assert(raw->reads == initial);
    assert(bytes[0] == 20);
    assert(buffered.seek(60000, SEEK_SET));
    assert(buffered.read(bytes, 100) == 100);
    assert(bytes[0] == 60000 % 251);
    assert(!buffered.seek(-1, SEEK_SET));
    assert(!buffered.seek(1, SEEK_END));
    char path[] = "/tmp/nxh-test-XXXXXX";
    assert(mkdtemp(path));
    std::string config = join(path, "config.json"), error;
    Settings original;
    original.musicRoot = "sdmc:/music";
    original.volume = 65;
    original.eqEnabled = true;
    original.eq = presets[1].gains;
    Share s;
    s.name = "NAS";
    s.server = "nas";
    s.share = "Music";
    s.user = "user";
    s.password = "spaces, quotes \" and UTF-8 日本語";
    s.guest = false;
    original.shares.push_back(s);
    assert(saveSettings(config, original, error));
    Settings restored;
    assert(loadSettings(config, restored, error));
    assert(restored.shares[0].password == s.password && restored.eq == original.eq &&
           restored.volume == 65);
    original.volume = 30;
    assert(saveSettings(config, original, error));
    assert(unlink(config.c_str()) == 0);
    Settings backup;
    assert(loadSettings(config, backup, error));
    assert(backup.volume == 65);
    unlink((config + ".bak").c_str());
    rmdir(path);
    Equalizer eq;
    std::vector<float> silence(4096, 0);
    for (auto &p : presets) {
        for (float g : p.gains)
            assert(g >= -6 && g <= 6);
        eq.configure(true, p.gains);
        eq.process(silence.data(), 2048);
        for (float value : silence)
            assert(std::isfinite(value) && value == 0);
    }
    auto wave = std::make_unique<MemoryInput>();
    wave->data = wav();
    BufferedInput input(std::move(wave), 8192);
    ma_decoder decoder{};
    auto cfg = ma_decoder_config_init(ma_format_f32, 2, 44100);
    assert(ma_decoder_init(readAudio, seekAudio, &input, &cfg, &decoder) == MA_SUCCESS);
    ma_uint64 length = 0;
    assert(ma_decoder_get_length_in_pcm_frames(&decoder, &length) == MA_SUCCESS && length == 44100);
    float pcm[2048];
    ma_uint64 frames = 0;
    assert(ma_decoder_read_pcm_frames(&decoder, pcm, 1024, &frames) == MA_SUCCESS &&
           frames == 1024);
    float peak = 0;
    for (float sample : pcm)
        peak = std::max(peak, std::abs(sample));
    assert(peak > .3f && peak < .4f);
    assert(ma_decoder_seek_to_pcm_frame(&decoder, 33075) == MA_SUCCESS);
    assert(ma_decoder_read_pcm_frames(&decoder, pcm, 1024, &frames) == MA_SUCCESS &&
           frames == 1024);
    ma_decoder_uninit(&decoder);
    auto nativeWave = std::make_unique<MemoryInput>();
    nativeWave->data = wav();
    nativeWave->data[24] = 0x80;
    nativeWave->data[25] = 0xbb; // 48 kHz source
    nativeWave->data[28] = 0x00;
    nativeWave->data[29] = 0xee;
    nativeWave->data[30] = 0x02;
    assert(ma_decoder_init(readAudio, seekAudio, nativeWave.get(), &cfg, &decoder) == MA_SUCCESS);
    ma_uint32 sourceRate = 0, outputRate = 0;
    assert(ma_data_source_get_data_format(decoder.pBackend, nullptr, nullptr, &sourceRate, nullptr,
                                          0) == MA_SUCCESS);
    assert(ma_decoder_get_data_format(&decoder, nullptr, nullptr, &outputRate, nullptr, 0) ==
           MA_SUCCESS);
    assert(sourceRate == 48000 && outputRate == 44100);
    ma_decoder_uninit(&decoder);

    std::cout << "Embedded artwork, malformed metadata, guest default, path validation, buffered "
                 "seeking, credential/config persistence, backup "
                 "recovery, EQ presets and PCM decode/seek passed.\n";
}
