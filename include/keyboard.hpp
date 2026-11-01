#pragma once
#include <functional>
#include <string>
#include <SDL.h>
namespace nxh {
class UI;
void bindKeyboard(UI *,SDL_Renderer *,std::function<void()> tick);
bool switchKeyboard(const std::string &,std::string &,bool);
}
