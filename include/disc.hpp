#pragma once
#include "metadata.hpp"
#include <functional>
#include <atomic>
#include <map>
#include <mutex>
namespace nxh {
inline constexpr const char *DiscRoot = "cdda:";
struct DiscTrack {
    int number = 0, start = 0, end = 0; // LBA, exclusive end
};
struct DiscTOC {
    int first = 0, last = 0;
    std::array<int, 100> offsets{}; // MusicBrainz: MSF frames, including +150
    std::vector<DiscTrack> audio;
};
std::string discID(const DiscTOC &);
bool parseDiscTOC(const unsigned char *, size_t, DiscTOC &, std::string &);
struct DiscMetadata {
    bool found = false;
    int64_t at = 0;
    std::string album, artist, release, group;
    std::map<int, std::string> titles, artists;
    std::vector<unsigned char> cover;
};
// HTTP injected for deterministic metadata tests. Returns the HTTP status, or 0 on failure.
using DiscHTTP = std::function<int(const std::string &, std::vector<unsigned char> &, size_t)>;
DiscMetadata parseDiscMetadata(const nlohmann::json &, const std::string &, int64_t);
using DiscArtCheck = std::function<bool(const std::vector<unsigned char> &)>;
DiscMetadata lookupDiscMetadata(const std::string &, const std::string &, const DiscHTTP &,
                                const DiscArtCheck & = {});
bool listDisc(std::vector<Entry> &, std::string &);
std::unique_ptr<Input> openDisc(const std::string &, std::string &,
                              const std::atomic<uint64_t> * = nullptr, uint64_t = 0);
TrackDetails discDetails(const std::string &);
std::vector<unsigned char> discCover(const std::string &);
void shutdownDisc();
} // namespace nxh
