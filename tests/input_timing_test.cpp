#include "input_timing.hpp"
#include <cassert>
#include <iostream>
using namespace nxh;
int main(){
    ButtonRepeat fast,slow;
    assert(fast.fire(1,true,1000)&&slow.fire(1,true,1000));
    for(uint64_t t=1016;t<1300;t+=16)assert(!fast.fire(1,false,t));
    assert(!slow.fire(1,false,1200));
    assert(fast.fire(1,false,1300)&&slow.fire(1,false,1300));
    assert(!fast.fire(1,false,1384));assert(fast.fire(1,false,1385));
    // Direction reversals and a new press respond on that poll, even during repeat delay.
    assert(fast.fire(-1,true,1386));assert(fast.fire(-1,true,1387));
    assert(!fast.fire(0,false,1388));assert(fast.fire(-1,false,1389));
    // A long stall causes one step rather than a burst of accumulated repeats.
    assert(fast.fire(-1,false,4000));assert(!fast.fire(-1,false,4000));
    std::cout<<"Immediate presses, frame-independent hold repeat, reversals and stall recovery passed.\n";
}
