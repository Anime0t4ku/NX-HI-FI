#pragma once
#include "storage.hpp"
#include "metadata.hpp"
namespace nxh {
inline constexpr const char *RadioRoot="radio://";
bool httpURL(const std::string &);
std::unique_ptr<Input> openRadio(const std::string &,std::string &,const std::atomic<uint64_t> *,uint64_t);
class ExtendedAudio {
    struct Impl;std::unique_ptr<Impl> impl;
public:
    ExtendedAudio();~ExtendedAudio();
    bool open(Input &,bool live,TrackDetails &,std::string &);
    size_t read(float *,size_t,std::string &);
    bool seek(double);
    std::vector<unsigned char> artwork()const;
};
}
