#pragma once
#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace u2k {
// Integer scheduling arithmetic shared by the DeckLink backend and deterministic
// model. Source origin is the sample selected for output stream position zero;
// its clock-domain mapping is supplied by the caller, not inferred here.
class AudioSchedule {
    int64_t sourceOrigin_=0,cursor_=0;
    uint32_t target_=0;
public:
    AudioSchedule()=default;
    AudioSchedule(int64_t sourceOrigin,uint32_t target):sourceOrigin_(sourceOrigin),target_(target){
        if(!target)throw std::invalid_argument("Audio schedule target must be positive");
    }
    int64_t cursor()const{return cursor_;}
    int64_t readStart()const{return sourceOrigin_+cursor_;}
    uint32_t target()const{return target_;}
    uint32_t blockSize(uint32_t buffered)const{return buffered>=target_?0:std::min<uint32_t>(1024,target_-buffered);}
    bool catchUp(int64_t played){if(played<=cursor_)return false;cursor_=played;return true;}
    void advance(uint32_t written){cursor_+=written;}
};
}
