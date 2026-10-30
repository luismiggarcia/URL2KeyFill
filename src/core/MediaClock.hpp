#pragma once
#include <cstdint>

namespace u2k {
// Keep quotient/remainder separate: multiplying a nanosecond uptime by 48000
// would overflow int64 long before converting the result to sample time.
constexpr int64_t samplesFromNanoseconds(int64_t ns){return (ns/1'000'000'000)*48000+(ns%1'000'000'000)*48000/1'000'000'000;}
struct HostClockSnapshot {int64_t uptimeNs=0,continuousNs=0,uncertaintyNs=0;};
HostClockSnapshot hostClockSnapshot();
int64_t mediaSampleTime();

// Detect changes in sleep-inclusive versus sleep-exclusive clock offset. A slow
// observation is discarded rather than mistaken for a suspend event.
class ClockGapDetector {
    bool ready_=false;int64_t offset_=0,uncertainty_=0;
public:
    bool observe(HostClockSnapshot s){
        constexpr int64_t threshold=50'000'000;
        if(s.uncertaintyNs>threshold/4)return false;
        const auto offset=s.continuousNs-s.uptimeNs;
        const auto delta=offset-offset_;
        const bool changed=ready_&&(delta>threshold+uncertainty_+s.uncertaintyNs||delta<-(threshold+uncertainty_+s.uncertaintyNs));
        ready_=true;offset_=offset;uncertainty_=s.uncertaintyNs;return changed;
    }
};
}
