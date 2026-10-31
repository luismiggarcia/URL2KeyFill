#pragma once
#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace u2k {
class ClockPhaseAcquisition {
    int64_t minimumPosition_=0,lastAnchor_=0;uint32_t stable_=0;
public:
    void reset(int64_t minimumPosition){minimumPosition_=minimumPosition;stable_=0;}
    bool observe(int64_t host,int64_t position,int64_t querySpan){
        if(querySpan>48){stable_=0;return false;}
        const auto anchor=host-position,delta=anchor-lastAnchor_;
        stable_=stable_&&delta>=-24&&delta<=24?stable_+1:1;lastAnchor_=anchor;
        return position>=minimumPosition_&&stable_>=4;
    }
};
// Integer scheduling arithmetic shared by the DeckLink backend and deterministic
// model. Source origin is the sample selected for output stream position zero;
// its clock-domain mapping is supplied by the caller, not inferred here.
class AudioSchedule {
    int64_t sourceOrigin_=0,cursor_=0;
    uint32_t target_=0;
public:
    // Provisional preroll origin. Recalibrate from stable queue/host observations
    // after acquisition; clock-rate correction is a separate operation.
    static AudioSchedule fromHostAnchor(int64_t host,uint32_t target,uint32_t sourceAge){
        return AudioSchedule(host-int64_t(target)-sourceAge,target);
    }
    AudioSchedule()=default;
    AudioSchedule(int64_t sourceOrigin,uint32_t target):sourceOrigin_(sourceOrigin),target_(target){
        if(!target)throw std::invalid_argument("Audio schedule target must be positive");
    }
    int64_t cursor()const{return cursor_;}
    int64_t readStart()const{return sourceOrigin_+cursor_;}
    int64_t sourceOrigin()const{return sourceOrigin_;}
    int64_t calibrate(int64_t host,int64_t played,uint32_t sourceAge){const auto old=sourceOrigin_;sourceOrigin_=host-played-int64_t(target_)-sourceAge;return sourceOrigin_-old;}
    int64_t calibrateQueue(int64_t host,uint32_t buffered,uint32_t sourceAge){return calibrate(host,cursor_-int64_t(buffered),sourceAge);}
    uint32_t target()const{return target_;}
    uint32_t paddingRemaining(uint32_t initialPadding)const{return cursor_>=initialPadding?0:uint32_t(initialPadding-cursor_);}
    uint32_t blockSize(uint32_t buffered,uint32_t initialPadding=0)const{const auto count=buffered>=target_?0:std::min<uint32_t>(1024,target_-buffered);const auto padding=paddingRemaining(initialPadding);return padding?std::min(count,padding):count;}
    uint32_t bufferedSpan(uint32_t deviceBuffered,int64_t played)const{const auto ahead=cursor_>played?uint64_t(cursor_-played):0;return std::max(deviceBuffered,uint32_t(std::min<uint64_t>(ahead,UINT32_MAX)));}
    bool recover(int64_t played,uint32_t lead){if(played<=cursor_)return false;cursor_=played+lead;return true;}
    bool catchUp(int64_t played){return recover(played,0);}
    void advance(uint32_t written){cursor_+=written;}
};
inline double audioTransitionWeight(uint32_t sample,uint32_t length){return length<2?1.:std::min(1.,double(sample)/(length-1));}
}
