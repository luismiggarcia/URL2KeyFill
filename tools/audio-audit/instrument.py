#!/usr/bin/env python3
"""Patch a *copy* of src/browser/Browser.hpp so every CEF audio packet is logged.

Usage: instrument.py <copied Browser.hpp>

The log (path from the U2K_TRACE environment variable) has one line per packet:
  browser raw_pts_ms adjusted_pts_ms frames channels system_us steady_us mach_absolute_us mach_continuous_us
All clocks are read on the CEF audio thread at packet arrival. Never apply this to the
tracked source tree.
"""
import sys

path = sys.argv[1]
source = open(path).read()

helper = r'''#include <cstdio>
#include <mach/mach_time.h>
namespace u2k {
inline void traceAudioPacket(const void* browser,int64_t raw,int64_t adjusted,int frames,int channels){
    static FILE* trace=std::getenv("U2K_TRACE")?std::fopen(std::getenv("U2K_TRACE"),"a"):nullptr;if(!trace)return;
    static const double tick=[]{mach_timebase_info_data_t t;mach_timebase_info(&t);return double(t.numer)/t.denom/1000.;}();
    using namespace std::chrono;
    std::fprintf(trace,"%p %lld %lld %d %d %lld %lld %.3f %.3f\n",browser,(long long)raw,(long long)adjusted,frames,channels,
        (long long)duration_cast<microseconds>(system_clock::now().time_since_epoch()).count(),
        (long long)duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count(),
        mach_absolute_time()*tick,mach_continuous_time()*tick);
    std::fflush(trace);
}
}
'''
anchor = '#include <cstdlib>\n'
call_old = 'audio_->push(data,frames,channels_.load(),pts+audioOffset_);}'
call_new = 'traceAudioPacket(this,pts,pts+audioOffset_,frames,channels_.load());' + call_old
if source.count(anchor) != 1 or source.count(call_old) != 1:
    sys.exit("Browser.hpp no longer matches the instrumentation anchors; update instrument.py.")
source = source.replace(anchor, anchor + helper, 1).replace(call_old, call_new, 1)
open(path, 'w').write(source)
