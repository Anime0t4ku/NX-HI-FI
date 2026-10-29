#pragma once
#include "disc.hpp"
#include <cstring>
namespace nxh {
class CDDAInput final:public Input {
    std::function<bool(int,int,unsigned char*)> readSectors_;
    DiscTrack track_;
    uint64_t pos_=0, bytes_=0;
    std::array<unsigned char,44> header_{};
    std::vector<unsigned char> cache_;
    int cacheLBA_=-1,cacheCount_=0;
    std::string error_;
    const std::atomic<uint64_t> *generation_;
    uint64_t expected_;
    void put(int p,uint32_t v,int n){for(int i=0;i<n;++i)header_[p+i]=v>>(8*i);}
  public:
    CDDAInput(std::function<bool(int,int,unsigned char*)> readSectors,DiscTrack t,const std::atomic<uint64_t>*g,uint64_t e)
        :readSectors_(std::move(readSectors)),track_(t),cache_(2352*16),generation_(g),expected_(e) {
        bytes_=uint64_t(t.end-t.start)*2352;
        std::memcpy(header_.data(),"RIFF",4);put(4,bytes_+36,4);
        std::memcpy(header_.data()+8,"WAVEfmt ",8);put(16,16,4);put(20,1,2);put(22,2,2);
        put(24,44100,4);put(28,176400,4);put(32,4,2);put(34,16,2);
        std::memcpy(header_.data()+36,"data",4);put(40,bytes_,4);
    }
    std::string error() const override{return error_;}
    uint64_t position()const override{return pos_;}
    uint64_t size()const override{return bytes_+44;}
    bool seek(int64_t offset,int origin)override {
        int64_t base=origin==SEEK_SET?0:origin==SEEK_CUR?int64_t(pos_):origin==SEEK_END?int64_t(size()):-1;
        if(base<0||offset< -base||offset>int64_t(size())-base)return false;
        pos_=base+offset;return true;
    }
    size_t read(void *out,size_t size)override {
        if(!error_.empty())return 0;
        size_t left=std::min<uint64_t>(size,this->size()-pos_),got=0;
        auto *dst=static_cast<unsigned char*>(out);
        while(left) {
            if(generation_&&generation_->load()!=expected_)break;
            if(pos_<44){size_t n=std::min<uint64_t>(left,44-pos_);std::memcpy(dst+got,header_.data()+pos_,n);pos_+=n;left-=n;got+=n;continue;}
            uint64_t raw=pos_-44;int lba=track_.start+raw/2352;
            if(lba<cacheLBA_||lba>=cacheLBA_+cacheCount_) {
                cacheLBA_=lba;cacheCount_=std::min(16,track_.end-lba);
                if(!readSectors_(lba,cacheCount_,cache_.data())) {
                    error_="Audio CD read failed. The disc may have been removed, changed or damaged.";
                    cacheLBA_=-1;cacheCount_=0;break;
                }
            }
            size_t offset=size_t(lba-cacheLBA_)*2352+raw%2352;
            size_t n=std::min(left,size_t(cacheCount_)*2352-offset);
            std::memcpy(dst+got,cache_.data()+offset,n);pos_+=n;got+=n;left-=n;
        }
        return got;
    }
};
} // namespace nxh
