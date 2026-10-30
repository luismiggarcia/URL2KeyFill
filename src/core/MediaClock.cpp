#include "MediaClock.hpp"
#ifdef __APPLE__
#include <mach/mach_time.h>
#else
#include <chrono>
#endif
namespace u2k {
#ifdef __APPLE__
static int64_t nanoseconds(uint64_t ticks){
    static const auto timebase=[] {mach_timebase_info_data_t t;mach_timebase_info(&t);return t;}();
    return int64_t((__uint128_t(ticks)*timebase.numer)/timebase.denom);
}
int64_t mediaSampleTime(){return samplesFromNanoseconds(nanoseconds(mach_absolute_time()));}
HostClockSnapshot hostClockSnapshot(){
    const auto before=nanoseconds(mach_absolute_time());
    const auto continuous=nanoseconds(mach_continuous_time());
    const auto after=nanoseconds(mach_absolute_time());
    return {before+(after-before)/2,continuous,(after-before+1)/2};
}
#else
int64_t mediaSampleTime(){return samplesFromNanoseconds(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
HostClockSnapshot hostClockSnapshot(){const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();return {ns,ns,0};}
#endif
}
