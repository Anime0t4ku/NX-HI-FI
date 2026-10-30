#pragma once
#include <algorithm>
namespace nxh {
// Fullscreen: seek > volume > transport. Menu: seek > transport > extras > volume.
struct PlayerNavigation {
    int row=1,column=1;
    static int count(bool full,int row){return full?(row==2?7:1):(row==1?3:row==2?4:1);}
    bool seek()const{return row==0;}
    bool volume(bool full)const{return row==(full?1:3);}
    void vertical(bool full,int direction){
        int before=count(full,row);row=std::clamp(row+direction,0,full?2:3);
        int after=count(full,row);column=before>1?int(double(column)*(after-1)/(before-1)+.5):after/2;
    }
    void horizontal(bool full,int direction){column=std::clamp(column+direction,0,count(full,row)-1);}
    int action(bool full)const{
        constexpr int transport[]={0,1,3,2,4,5,6};
        constexpr int primary[]={0,1,2},secondary[]={3,4,5,7};
        return seek()||volume(full)?-1:full?transport[std::clamp(column,0,6)]:row==1?primary[std::clamp(column,0,2)]:secondary[std::clamp(column,0,3)];
    }
};
struct PlayerRect{int x,y,w,h;bool contains(int px,int py)const{return px>=x&&py>=y&&px<x+w&&py<y+h;}};
inline PlayerRect playerSeekRect(bool full,int extra,int rightX,int rightW){return full?PlayerRect{rightX,370+extra,rightW,32}:PlayerRect{900,399+extra,328,32};}
inline PlayerRect playerVolumeRect(bool full,int extra,int rightX,int rightW){return full?PlayerRect{rightX,455+extra,rightW,42}:PlayerRect{900,615+extra,328,30};}
// Separate from metadata/seek: between volume and transport in fullscreen,
// and between the secondary transport row and volume in the mini player.
inline PlayerRect playerVisualizerRect(bool full,int extra,int rightX,int rightW){
    return full?PlayerRect{rightX,509+extra,rightW,47}:PlayerRect{900,583+extra,328,20};
}
inline PlayerRect playerButtonRect(bool full,int extra,int row,int column){
    if(full)return {64+column*164,568+extra,152,76};
    if(row==1){constexpr int x[]={916,1006,1120},w[]={70,98,70};return {x[column],448+extra+(column==1?0:8),w[column],column==1?66:50};}
    return {900+column*84,527+extra,76,44};
}
}
