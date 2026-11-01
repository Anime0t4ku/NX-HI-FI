#include "platform.hpp"
#include <sys/stat.h>
#include <cstdio>
#ifdef __SWITCH__
#include <arpa/inet.h>
namespace nxh {
const char *assetRoot(){return "romfs:/";}
void initPlatform(){}
void closePlatform(){}
bool storageAccessReady(){return true;}
bool platformQuitRequested(){return false;}
std::string localNetworkAddress(){std::string result;if(R_SUCCEEDED(nifmInitialize(NifmServiceType_User))){u32 ip=0,mask=0,gw=0,dns1=0,dns2=0;if(R_SUCCEEDED(nifmGetCurrentIpConfigInfo(&ip,&mask,&gw,&dns1,&dns2))){in_addr addr{ip};result=inet_ntoa(addr);}nifmExit();}return result;}
bool refreshDisplay(SDL_Window *,SDL_Renderer *,bool){return false;}
}
#else
#include <SDL.h>
void padUpdate(PadState *p){p->down=p->held=0;}
namespace nxh {
const char *assetRoot(){return "assets/";}
void initPlatform(){}void closePlatform(){}bool storageAccessReady(){return true;}bool platformQuitRequested(){return false;}
std::string localNetworkAddress(){return "127.0.0.1";}bool refreshDisplay(SDL_Window *,SDL_Renderer *,bool){return false;}
}
#endif
