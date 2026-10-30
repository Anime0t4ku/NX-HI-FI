#pragma once
#include <cstdint>
namespace nxh {
// Repeat uses elapsed time, never the number of rendered frames.
struct ButtonRepeat {
    int direction=0;uint64_t next=0;
    bool fire(int value,bool pressed,uint64_t now){
        if(!value){direction=0;next=0;return false;}
        if(pressed||value!=direction){direction=value;next=now+300;return true;}
        if(now<next)return false;
        next=now+85;return true; // One action per poll, no catch-up burst after a slow frame.
    }
};
}
