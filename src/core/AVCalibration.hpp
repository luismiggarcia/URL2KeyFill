#pragma once
#include "Core.hpp"
#include <algorithm>
#include <cstdint>
#include <optional>
namespace u2k {
// CEF paints at the largest integer multiple of the SDI cadence up to 60 fps (25p -> 50).
// A finer paint grid halves the start-dependent A/V phase of browser content (audit §11).
inline int64_t cefRateMultiplier(const Rate& rate){int64_t k=1;while(rate.scale*(k+1)<=60*rate.duration+rate.duration/2)++k;return k;}
// Default A/V adjustment per SDI cadence (ms, positive delays audio). Chosen to maximise the
// smallest margin between the target window and the p1/p99 A/V of 9-11 SDI loopback starts
// per cadence (bundled A/V page and <video> known-PTS reference, net of input path): EBU R37
// (-40/+60) at 25p, where start-to-start browser phase (~73 ms) exceeds the ATSC window, and
// ATSC IS-191 (-15/+45) at 50p and 59.94p (margins 13.7, 7.0 and 0.6 ms; audit §11.4). Later
// 59.94p starts left that 0.6 ms behind (neighbour-frame oscillation), so 59.94p guarantees
// EBU R37 only (audit §11.5). Valid with the CEF rate rule above. Unmeasured cadences have no
// default; operators add a manual adjustment on top.
inline std::optional<int32_t> calibratedSyncMs(const Rate& rate){
 struct Entry {int64_t duration,scale;int32_t ms;};
 static constexpr Entry table[]={{1000,25000,17},{1000,50000,14},{1001,60000,-2}};
 for(const auto& e:table)if(e.duration*rate.scale==e.scale*rate.duration)return e.ms;
 return {};
}
// Total applied to the output; the A/V adjustment range is +/-200 ms.
inline int32_t effectiveSyncMs(std::optional<int32_t> calibrated,int32_t manual){return std::clamp(calibrated.value_or(0)+manual,-200,200);}
}
