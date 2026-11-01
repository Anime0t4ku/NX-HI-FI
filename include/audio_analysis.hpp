#pragma once
#include <array>
#include <complex>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <memory>
namespace nxh {
struct AudioVisualFrame {
    uint64_t frame=0; // Centre of this analysis window in the queued 44.1 kHz PCM timeline.
    std::array<float,32> spectrum{};
    std::array<float,256> left{},right{};
};
// Decoder-thread STFT: every sample is retained, overlapping windows span 20 Hz–20 kHz.
// The player publishes these packets by audible position, never by decode/read-ahead time.
class AudioAnalysis {
  public:
    static constexpr unsigned Rate=44100,Size=4096,Hop=512;
  private:
    // Decoder threads can have small stacks on PS5. Keep all large DSP buffers
    // in one owned heap allocation, including when AudioAnalysis itself is local.
    struct Buffers {
    std::array<float,Size> ringLeft{},ringRight{},window{},rawLeft{},rawRight{};
    std::array<std::complex<float>,Size> fftLeft{},fftRight{};
    std::array<std::complex<float>,Size/2> twiddles{};
    std::array<unsigned,Size> reversed{};
    };
    std::unique_ptr<Buffers> buffers_;
    unsigned write=0;uint64_t count=0,origin=0;float windowSum=0;
    void fft(std::array<std::complex<float>,Size> &v){
        auto &data=*buffers_;
        for(unsigned i=0;i<Size;i++)if(i<data.reversed[i])std::swap(v[i],v[data.reversed[i]]);
        for(unsigned length=2;length<=Size;length*=2){unsigned half=length/2,step=Size/length;
            for(unsigned base=0;base<Size;base+=length)for(unsigned j=0;j<half;j++){
                auto a=v[base+j],b=v[base+j+half]*data.twiddles[j*step];v[base+j]=a+b;v[base+j+half]=a-b;
            }
        }
    }
    AudioVisualFrame analyse(){
        auto &data=*buffers_;
        AudioVisualFrame out;out.frame=origin+count-Size/2;
        for(unsigned i=0;i<Size;i++){
            unsigned at=(write+i)%Size;data.rawLeft[i]=data.ringLeft[at];data.rawRight[i]=data.ringRight[at];
        }
        float meanLeft=0,meanRight=0;for(unsigned i=0;i<Size;i++){meanLeft+=data.rawLeft[i]/Size;meanRight+=data.rawRight[i]/Size;}
        for(unsigned i=0;i<Size;i++){data.fftLeft[i]={(data.rawLeft[i]-meanLeft)*data.window[i],0};data.fftRight[i]={(data.rawRight[i]-meanRight)*data.window[i],0};}
        fft(data.fftLeft);fft(data.fftRight);
        for(unsigned b=0;b<32;b++){
            double lo=20*std::pow(1000.,b/32.),hi=20*std::pow(1000.,(b+1)/32.);
            unsigned first=std::max(1,int(std::ceil(lo*Size/Rate))),last=std::min(int(Size/2-1),int(std::floor(hi*Size/Rate)));
            if(last<first)first=last=std::clamp(int(std::lround(std::sqrt(lo*hi)*Size/Rate)),1,int(Size/2-1));
            float power=0;for(unsigned k=first;k<=last;k++)power=std::max(power,(std::norm(data.fftLeft[k])+std::norm(data.fftRight[k]))*.5f);
            float magnitude=2*std::sqrt(power)/windowSum;
            out.spectrum[b]=std::clamp((20*std::log10(std::max(1e-8f,magnitude))+72)/72.f,0.f,1.f);
        }
        // Shared rising-zero trigger keeps a periodic waveform steady and preserves stereo timing.
        unsigned start=Size/4;float best=1e9f;
        for(unsigned i=Size/4;i<Size/4+512;i++){
            float before=(data.rawLeft[i-1]+data.rawRight[i-1])*.5f,after=(data.rawLeft[i]+data.rawRight[i])*.5f;
            // Anti-phase stereo must not erase the trigger channel.
            if(std::abs(before)+std::abs(after)<1e-5f){before=data.rawLeft[i-1];after=data.rawLeft[i];}
            if(before<=0&&after>0){float distance=float(i-Size/4);if(distance<best){best=distance;start=i;}}
        }
        // Peak-preserving reduction of a contiguous 2048-sample stereo window.
        for(unsigned i=0;i<256;i++)for(unsigned j=0;j<8;j++){
            float l=data.rawLeft[start+i*8+j],r=data.rawRight[start+i*8+j];
            if(std::abs(l)>std::abs(out.left[i]))out.left[i]=l;
            if(std::abs(r)>std::abs(out.right[i]))out.right[i]=r;
        }
        return out;
    }
  public:
    AudioAnalysis():buffers_(std::make_unique<Buffers>()){
        auto &data=*buffers_;
        for(unsigned i=0;i<Size;i++){
            data.window[i]=.5f-.5f*std::cos(float(2*3.141592653589793*i/(Size-1)));windowSum+=data.window[i];
            unsigned v=i,r=0;for(unsigned bit=0;bit<12;bit++){r=(r<<1)|(v&1);v>>=1;}data.reversed[i]=r;
        }
        for(unsigned i=0;i<Size/2;i++){float a=float(-2*3.141592653589793*i/Size);data.twiddles[i]={std::cos(a),std::sin(a)};}
    }
    void reset(uint64_t frame=0){auto &data=*buffers_;data.ringLeft={};data.ringRight={};write=0;count=0;origin=frame;}
    std::vector<AudioVisualFrame> push(const float *interleaved,size_t frames){
        auto &data=*buffers_;
        std::vector<AudioVisualFrame> out;out.reserve(frames/Hop+1);
        for(size_t i=0;i<frames;i++){
            float l=interleaved[i*2],r=interleaved[i*2+1];data.ringLeft[write]=std::isfinite(l)?l:0;data.ringRight[write]=std::isfinite(r)?r:0;
            write=(write+1)%Size;++count;
            if(count>=Size/2&&count%Hop==0)out.push_back(analyse());
        }return out;
    }
};
static_assert(sizeof(AudioAnalysis)<512,"AudioAnalysis must not place DSP buffers on the decoder stack");

// This queue bridges decoder read-ahead and the audible PCM cursor.
class AudioVisualQueue {
    std::deque<AudioVisualFrame> frames;
    uint64_t submitted=0,origin=0;
  public:
    void reset(uint64_t at=0){frames.clear();submitted=origin=at;}
    void publish(std::vector<AudioVisualFrame> input,uint64_t end){
        submitted=end;for(auto &f:input)frames.push_back(std::move(f));while(frames.size()>256)frames.pop_front();
    }
    AudioVisualFrame sample(uint64_t queued,uint64_t deviceFragment=1024){
        uint64_t remaining=std::min(submitted-origin,queued+deviceFragment),audible=submitted-remaining;
        while(frames.size()>1&&frames[1].frame<=audible)frames.pop_front();
        if(frames.empty()||frames.front().frame>audible){AudioVisualFrame empty;empty.frame=origin;return empty;}
        auto result=frames.front();
        if(frames.size()>1){const auto &next=frames[1];float t=std::clamp(float(audible-result.frame)/float(next.frame-result.frame),0.f,1.f);
            for(unsigned i=0;i<32;i++)result.spectrum[i]+=(next.spectrum[i]-result.spectrum[i])*t;
        }return result;
    }
};

}
