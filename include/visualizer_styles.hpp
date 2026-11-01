#pragma once
#include <array>
#include <algorithm>
namespace nxh {
// Keep persisted IDs stable when removing styles.
inline constexpr std::array<int,11> visualizerIds{{0,1,2,3,5,7,9,10,14,15,18}};
inline bool validVisualizerStyle(int id){return std::find(visualizerIds.begin(),visualizerIds.end(),id)!=visualizerIds.end();}
inline const char *visualizerName(int id){switch(id){
case 1:return "Spectrum mountains";case 2:return "Mirrored spectrum";case 3:return "Waveform ribbon";
case 5:return "Spectrogram";case 7:return "Oscilloscope";case 9:return "Spectrum waterfall";
case 10:return "Dot matrix";case 14:return "Pixel skyline";case 15:return "Twin waves";
case 18:return "Flame spectrum";default:return "Stock spectrum";
}}
}
