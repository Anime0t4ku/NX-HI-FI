#pragma once
#include "ui.hpp"
#include "player.hpp"
#include "player_navigation.hpp"
#include "visualizer_styles.hpp"
#include <deque>
namespace nxh {
class Visualizer {
    std::array<float,32> bands{},peaks{};
    struct History {std::array<float,32> spectrum;std::array<float,256> left;};
    std::deque<History> history;
    Uint64 last=0,historyAt=0;
    uint64_t epoch=0,lastFrame=~uint64_t(0);
    std::string track;int previousStyle=-1;
    static Color blend(Color a,Color b,float t){t=std::clamp(t,0.f,1.f);return {Uint8(a.r+(b.r-a.r)*t),Uint8(a.g+(b.g-a.g)*t),Uint8(a.b+(b.b-a.b)*t)};}
  public:
    void draw(UI &ui,const Playback &p,PlayerRect area,const Settings &s){
        const auto now=SDL_GetTicks64();int style=validVisualizerStyle(s.visualizerStyle)?s.visualizerStyle:0;
        if(track!=p.track.path||style!=previousStyle||epoch!=p.analysisEpoch){bands={};peaks={};history.clear();track=p.track.path;previousStyle=style;epoch=p.analysisEpoch;last=0;historyAt=0;lastFrame=~uint64_t(0);}
        float dt=last?std::min(.1f,float(now-last)/1000):1.f/60;last=now;
        bool valid=p.playing&&!p.busy&&!p.ended,advancing=valid&&!p.paused;
        float smoothing=std::clamp(s.visualizerSmoothing,0,95)/100.f;
        float attack=.008f+smoothing*.012f,release=.06f+smoothing*.24f,energy=0;
        for(int i=0;i<32;i++){
            float target=valid?std::clamp(p.spectrum[i]*s.visualizerSensitivity/100.f,0.f,1.f):0;
            float alpha=p.paused?0:1-std::exp(-dt/(target>bands[i]?attack:release));
            bands[i]+=(target-bands[i])*alpha;energy+=bands[i]/32;
            if(!p.paused)peaks[i]=std::max(bands[i],peaks[i]-dt*.65f);
        }
        // Real past audible windows, not scaled copies or clock-driven geometry.
        if(advancing&&p.analysisFrame!=lastFrame&&(history.empty()||now-historyAt>=16)){
            history.push_front({p.spectrum,p.waveLeft});if(history.size()>96)history.pop_back();historyAt=now;lastFrame=p.analysisFrame;
        }
        if(!valid&&energy<.005f)history.clear();
        int x=area.x,y=area.y,w=area.w,h=area.h;if(w<4||h<4)return;
        UI::Clip clip(ui,x,y,w,h);
        Color ACCENT=s.visualizerColour=="theme"?nxh::ACCENT:readableAccent(rgbColour(s.visualizerColour),BG);
        auto ink=[&](float strength){return blend(BG,ACCENT,strength);};
        auto line=[&](float a,float b,float c,float d,Color color){ui.visualizerLine(int(a),int(b),int(c),int(d),color);};
        auto interpolate=[&](const std::array<float,32>& values,float fraction){float f=std::clamp(fraction,0.f,1.f)*31;int i=int(f);return values[i]+(values[std::min(31,i+1)]-values[i])*(f-i);};
        auto sample=[&](float f){return interpolate(bands,f);};
        auto wave=[&](float v){return valid?std::clamp(v*s.visualizerSensitivity/100.f,-1.f,1.f):0.f;};
        if(style==0||style==2||style==14||style==18){
            for(int i=0;i<32;i++){
                int bw=std::max(2,w/32-3),bh=std::max(2,int(bands[i]*h)),bx=x+i*w/32;
                if(style==2){bh/=2;ui.rect(bx,y+h/2-bh,bw,std::max(2,bh*2),ACCENT);}
                else if(style==14){for(int by=0;by<bh;by+=8)ui.rect(bx,y+h-by-6,bw,5,ink(.35f+.65f*by/h));ui.rect(bx,y+h-int(peaks[i]*h),bw,2,TEXT);}
                else if(style==18){for(int by=0;by<bh;by+=4){float t=float(by)/std::max(1,bh);ui.rect(bx,y+h-by-4,bw,4,blend({220,40,15},{255,226,85},t));}}
                else ui.rect(bx,y+h-bh,bw,bh,ACCENT);
            }return;
        }
        if(style==1||style==9){
            int layers=style==1?std::min(5,int(history.size())):std::min(24,int(history.size()));
            if(!layers)layers=1;
            for(int layer=layers-1;layer>=0;layer--){
                float depth=float(layer)/std::max(1,layers),base=y+h-(style==9?depth*h*.65f:layer*h*.09f);
                float inset=style==9?depth*w*.22f:0;Color color=ink(1-depth*.8f);
                const auto &values=history.empty()?bands:history[std::min(int(history.size())-1,layer*(style==1?3:2))].spectrum;
                for(int i=0;i<127;i++){
                    float a=layer?std::clamp(interpolate(values,i/127.f)*s.visualizerSensitivity/100.f,0.f,1.f):sample(i/127.f);
                    float b=layer?std::clamp(interpolate(values,(i+1)/127.f)*s.visualizerSensitivity/100.f,0.f,1.f):sample((i+1)/127.f);
                    line(x+inset+i*(w-2*inset)/127,base-a*h*(style==1?.52f:.3f),x+inset+(i+1)*(w-2*inset)/127,base-b*h*(style==1?.52f:.3f),color);
                }
            }return;
        }
        if(style==3||style==7||style==15){
            int layers=style==3?std::max(1,std::min(6,int(history.size()))):style==15?2:1;
            for(int l=layers-1;l>=0;l--){
                const auto &values=style==3&&!history.empty()?history[std::min(int(history.size())-1,l)].left:style==15&&l==1?p.waveRight:p.waveLeft;
                for(int i=0;i<255;i++){
                    float center=style==15?y+h*(l?.75f:.25f):y+h*.5f,amp=h*(style==15?.22f:.44f);
                    line(x+i*w/255.f,center+wave(values[i])*amp,x+(i+1)*w/255.f,center+wave(values[i+1])*amp,ink(1-l*.14f));
                }
            }return;
        }
        if(style==5){
            for(int col=0;col<int(history.size());col++)for(int b=0;b<32;b++){
                float v=std::clamp(history[col].spectrum[b]*s.visualizerSensitivity/100.f,0.f,1.f);
                Color c=v<.5f?blend({10,12,40},{30,80,220},v*2):blend({30,80,220},{255,214,64},(v-.5f)*2);
                int cw=std::max(1,(w+95)/96),ch=std::max(1,(h+31)/32);ui.rect(x+w-(col+1)*cw,y+h-(b+1)*ch,cw,ch,c);
            }return;
        }
        if(style==10){for(int i=0;i<32;i++)for(int j=0;j<12;j++){
            float v=std::clamp(bands[i]*12-j,0.f,1.f);int size=std::max(2,int(std::min(w/32.f,h/12.f)*.55f));ui.rect(x+i*w/32+(w/32-size)/2,y+h-(j+1)*h/12,size,size,ink(.08f+.92f*v));
        }return;}
    }
};
}
