#pragma once
#include <algorithm>
#include <cmath>

namespace pi {
inline int playbackFrame(double seconds,int count,int fps,bool loop) {
    if(count<=0 || !std::isfinite(seconds)) return 0;
    auto frame=std::floor(std::max(0.0,seconds)*std::max(1,fps));
    return loop?(int)std::fmod(frame,(double)count):(int)std::min(frame,(double)count-1);
}
}
