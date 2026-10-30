#pragma once
// Desktop test stand-ins for controller injection and the system text/clock APIs.
// The NRO always includes libnx's real switch.h instead.
#include <cstdint>
#include <ctime>
#include <unistd.h>
using u64=uint64_t;
using Result=uint32_t;
#define R_SUCCEEDED(rc) ((rc)==0)
#define R_FAILED(rc) ((rc)!=0)
struct PadState{};
enum : u64 {
    HidNpadButton_A=1ULL<<0,HidNpadButton_B=1ULL<<1,HidNpadButton_X=1ULL<<2,HidNpadButton_Y=1ULL<<3,
    HidNpadButton_StickL=1ULL<<4,HidNpadButton_StickR=1ULL<<5,HidNpadButton_L=1ULL<<6,HidNpadButton_R=1ULL<<7,
    HidNpadButton_ZL=1ULL<<8,HidNpadButton_ZR=1ULL<<9,HidNpadButton_Plus=1ULL<<10,HidNpadButton_Minus=1ULL<<11,
    HidNpadButton_Left=1ULL<<12,HidNpadButton_Up=1ULL<<13,HidNpadButton_Right=1ULL<<14,HidNpadButton_Down=1ULL<<15,
    HidNpadButton_StickLUp=1ULL<<17,HidNpadButton_StickLDown=1ULL<<19
};
inline void padUpdate(PadState*){}
inline u64 padGetButtonsDown(PadState*){return 0;}
inline u64 padGetButtons(PadState*){return 0;}
struct SwkbdConfig{};
inline Result swkbdCreate(SwkbdConfig*,int){return 1;}
inline void swkbdConfigMakePresetDefault(SwkbdConfig*){}
inline void swkbdConfigSetHeaderText(SwkbdConfig*,const char*){}
inline void swkbdConfigSetInitialText(SwkbdConfig*,const char*){}
inline void swkbdConfigSetStringLenMax(SwkbdConfig*,int){}
inline void swkbdConfigSetPasswordFlag(SwkbdConfig*,int){}
inline Result swkbdShow(SwkbdConfig*,char*,size_t){return 1;}
inline void swkbdClose(SwkbdConfig*){}
struct TimeCalendarTime{int hour,minute;};
struct TimeCalendarAdditionalInfo{};
constexpr int TimeType_UserSystemClock=0;
inline Result timeGetCurrentTime(int,u64 *value){*value=std::time(nullptr);return 0;}
inline Result timeToCalendarTimeWithMyRule(u64 stamp,TimeCalendarTime *out,TimeCalendarAdditionalInfo*){
    std::time_t t=stamp;auto *now=std::localtime(&t);out->hour=now->tm_hour;out->minute=now->tm_min;return 0;
}
