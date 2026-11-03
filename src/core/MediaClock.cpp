#include "MediaClock.hpp"
#ifdef __APPLE__
#include <mach/mach_time.h>
#else
#include <chrono>
#include <thread>
#endif
namespace u2k {
#ifdef __APPLE__
static int64_t nanoseconds(uint64_t ticks){
    static const auto timebase=[] {mach_timebase_info_data_t t;mach_timebase_info(&t);return t;}();
    return int64_t((__uint128_t(ticks)*timebase.numer)/timebase.denom);
}
int64_t mediaSampleTime(){return samplesFromNanoseconds(nanoseconds(mach_absolute_time()));}
void waitUntilSampleTime(int64_t samples){
    static const auto timebase=[] {mach_timebase_info_data_t t;mach_timebase_info(&t);return t;}();
    if(samples<=0)return;const auto ns=__uint128_t(samples/48000)*1000000000+__uint128_t(samples%48000)*1000000000/48000;
    mach_wait_until(uint64_t(ns*timebase.denom/timebase.numer));
}
HostClockSnapshot hostClockSnapshot(){
    const auto before=nanoseconds(mach_absolute_time());
    const auto continuous=nanoseconds(mach_continuous_time());
    const auto after=nanoseconds(mach_absolute_time());
    return {before+(after-before)/2,continuous,(after-before+1)/2};
}
#else
int64_t mediaSampleTime(){return samplesFromNanoseconds(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
void waitUntilSampleTime(int64_t samples){const auto now=mediaSampleTime();if(samples>now)std::this_thread::sleep_for(std::chrono::nanoseconds((samples-now)*1000000000/48000));}
HostClockSnapshot hostClockSnapshot(){const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();return {ns,ns,0};}
#endif
}
