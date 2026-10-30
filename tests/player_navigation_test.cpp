#include "player_navigation.hpp"
#include "core.hpp"
#include <cassert>
#include <cstdio>
#include <unistd.h>
#include <fstream>
#include <iostream>
using namespace nxh;
int main(){
    PlayerNavigation menu;assert(menu.action(false)==1);
    menu.vertical(false,-1);assert(menu.seek());
    menu.vertical(false,1);assert(menu.action(false)==1);
    menu.vertical(false,1);assert(menu.row==2);menu.column=3;assert(menu.action(false)==7);
    menu.vertical(false,1);assert(menu.volume(false));menu.vertical(false,1);assert(menu.volume(false));
    PlayerNavigation full{0,0};assert(full.seek());full.vertical(true,1);assert(full.volume(true));full.vertical(true,1);assert(full.action(true)==2);
    full.column=0;assert(full.action(true)==0);full.horizontal(true,-1);assert(full.column==0);
    full.column=6;assert(full.action(true)==6);full.horizontal(true,1);assert(full.column==6);
    for(bool f:{false,true})for(int extra:{0,144}){
        const auto band=playerVisualizerRect(f,extra,560,656);
        const auto volume=playerVolumeRect(f,extra,560,656);
        if(f) assert(band.y>=volume.y+volume.h+8);
        else assert(band.y+band.h<=volume.y-8);
        for(int row=f?2:1;row<=2;row++)for(int col=0;col<PlayerNavigation::count(f,row);col++){
            auto box=playerButtonRect(f,extra,row,col);assert(box.contains(box.x+box.w/2,box.y+box.h/2));assert(box.x>=0&&box.x+box.w<=1280);assert(box.y+box.h<664+extra);
            if(f) assert(band.y+band.h<=box.y-8);
            else if(row==2) assert(band.y>=box.y+box.h+8);
        }
    }
    Settings settings;assert(settings.volume==100&&!settings.autoFullscreen);
    std::string error;assert(patchSettings(settings,{{"auto_fullscreen",true}},error));assert(settings.autoFullscreen);
    assert(!patchSettings(settings,{{"auto_fullscreen","yes"}},error));assert(settings.autoFullscreen);
    auto config="/tmp/nx-hifi-player-settings-"+std::to_string(getpid())+".json";
    settings.volume=37;assert(saveSettings(config,settings,error));Settings loaded;assert(loadSettings(config,loaded,error));assert(loaded.volume==37&&loaded.autoFullscreen);
    {std::ofstream old(config);old<<"{\"volume\":80}";}
    Settings legacy;assert(loadSettings(config,legacy,error));assert(legacy.volume==80&&!legacy.autoFullscreen);
    std::remove(config.c_str());std::remove((config+".bak").c_str());
    std::cout<<"Player focus rows/actions, pointer geometry, Auto Fullscreen persistence and new/existing volume defaults passed.\n";
}
