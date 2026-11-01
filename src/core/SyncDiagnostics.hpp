#pragma once
#include "AudioClock.hpp"
#include <vector>
#include <cstdint>
#include <cstdlib>
namespace u2k {
// Created only for explicit SDI diagnostics. Reserve before starting output;
// single writer, read only after it stops. Never allocate or log on append.
template<class T> struct SyncEventBuffer {
 static constexpr size_t limit=4096;
 std::vector<T> events;uint64_t omitted=0;
 SyncEventBuffer(){events.reserve(limit);}
 void append(const T& event){if(events.size()<limit)events.push_back(event);else ++omitted;}
};
struct SyncAudioEvent {
 int64_t stream=0,host=0;double source=0;AudioMapping mapping;
 int32_t delay=0;bool transition=false;
};
struct SyncVideoEvent {
 int64_t stream=0,host=0,paint=0,content=INT64_MIN,historyTime=INT64_MIN,maxAhead=0,identity=-1;
 uint64_t sequence=0;double target=0;AudioMapping mapping;int32_t adjustment=0;
};
struct SyncClockEvent {
 int64_t queueBefore=0,queueAfter=0,consumed=0,stream=0;
 int64_t hardwareBefore=0,hardwareAfter=0,hardware=0;
 bool hardwareValid=false;double ratio=1,phaseError=0;
};
struct SyncAudioTrace:SyncEventBuffer<SyncAudioEvent> {
 uint32_t quiet=4800;
 // Only samples actually accepted by ScheduleAudioSamples, including partial
 // writes. The unwritten suffix must be inspected on its later submission.
 void accepted(const int32_t* stereo,uint32_t written,int64_t stream,int64_t host,
               const AudioMapping& mapping,int32_t delay,bool transition){
  for(uint32_t j=0;j<written;++j){
   if(std::abs(int64_t(stereo[size_t(j)*2]))>6442450){
    if(quiet>=4800)append({stream+j,host,mapping.at(stream+j)-delay,mapping,delay,transition});
    quiet=0;
   }else quiet=std::min<uint32_t>(4800,quiet+1);
  }
 }
};
}
