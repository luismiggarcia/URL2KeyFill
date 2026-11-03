#pragma once
#include "Core.hpp"
#include <algorithm>
#include <cstdint>
#include <optional>
namespace u2k {
// CEF paints at the largest integer multiple of the SDI cadence up to 60 fps (25p -> 50).
// A finer paint grid halves the start-dependent A/V phase of browser content (audit §11).
inline int64_t cefRateMultiplier(const Rate& rate){int64_t k=1;while(rate.scale*(k+1)<=60*rate.duration+rate.duration/2)++k;return k;}
// Default A/V adjustment per SDI cadence (ms, positive delays audio), measured on the SDI
// loopback with the bundled A/V page and <video> reference over several starts, net of the
// input path (+0.7 ms, scheduled SDK reference). It places the median at +15 ms of audio
// lag, the centre of ATSC IS-191 (-15/+45) and inside EBU R37 (-40/+60): start-to-start
// browser phase spreads about +/-15 ms around it (audit §11). Valid with the CEF rate rule
// above. Unmeasured cadences have no default; operators add a manual adjustment on top.
inline std::optional<int32_t> calibratedSyncMs(const Rate& rate){
 struct Entry {int64_t duration,scale;int32_t ms;};
 static constexpr Entry table[]={{1000,25000,13},{1000,50000,12},{1001,60000,-4}};
 for(const auto& e:table)if(e.duration*rate.scale==e.scale*rate.duration)return e.ms;
 return {};
}
// Total applied to the output; the A/V adjustment range is +/-200 ms.
inline int32_t effectiveSyncMs(std::optional<int32_t> calibrated,int32_t manual){return std::clamp(calibrated.value_or(0)+manual,-200,200);}
}
