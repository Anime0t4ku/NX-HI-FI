#include "keyboard.hpp"
#ifdef __SWITCH__
#include <switch.h>
#endif
namespace nxh {
void bindKeyboard(UI *,SDL_Renderer *,std::function<void()>){ }
bool switchKeyboard(const std::string &label,std::string &value,bool password){
#ifdef __SWITCH__
 SwkbdConfig cfg{};if(R_FAILED(swkbdCreate(&cfg,0)))return false;swkbdConfigMakePresetDefault(&cfg);swkbdConfigSetHeaderText(&cfg,label.c_str());swkbdConfigSetSubText(&cfg,label.c_str());swkbdConfigSetGuideText(&cfg,label.c_str());swkbdConfigSetInitialText(&cfg,value.c_str());swkbdConfigSetStringLenMax(&cfg,1023);swkbdConfigSetPasswordFlag(&cfg,password?1:0);char out[4096]{};Result result=swkbdShow(&cfg,out,sizeof out);swkbdClose(&cfg);SDL_PumpEvents();SDL_FlushEvents(SDL_FINGERDOWN,SDL_FINGERMOTION);SDL_FlushEvents(SDL_MOUSEMOTION,SDL_MOUSEWHEEL);if(R_FAILED(result))return false;value=out;return true;
#else
 return false;
#endif
}
}
