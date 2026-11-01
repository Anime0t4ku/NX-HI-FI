#pragma once
// The NX wire button ordering is retained for incoming Screen Control.
#include "platform_paths.hpp"
#include <SDL.h>
#include <cstdint>
#ifdef __SWITCH__
#include <switch.h>
#else
using u64 = uint64_t;
inline constexpr u64 HidNpadButton_A=1ull<<0,HidNpadButton_B=1ull<<1,HidNpadButton_X=1ull<<2,HidNpadButton_Y=1ull<<3,
HidNpadButton_Minus=1ull<<4,HidNpadButton_Plus=1ull<<5,HidNpadButton_L=1ull<<6,HidNpadButton_R=1ull<<7,
HidNpadButton_ZL=1ull<<8,HidNpadButton_ZR=1ull<<9,HidNpadButton_Up=1ull<<10,HidNpadButton_Down=1ull<<11,
HidNpadButton_Left=1ull<<12,HidNpadButton_Right=1ull<<13,HidNpadButton_StickR=1ull<<14,
HidNpadButton_StickLUp=1ull<<15,HidNpadButton_StickLDown=1ull<<16,HidNpadButton_StickL=1ull<<17;
struct PadState { u64 down=0,held=0; };
void padUpdate(PadState *);
inline u64 padGetButtonsDown(PadState *p){return p->down;}
inline u64 padGetButtons(PadState *p){return p->held;}
#endif
#include <string>
namespace nxh {
void initPlatform();
std::string localNetworkAddress();
void closePlatform();
bool storageAccessReady();
bool refreshDisplay(SDL_Window *,SDL_Renderer *,bool force=false);
bool platformQuitRequested();
const char *assetRoot();
}
