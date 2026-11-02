#pragma once
#include "AudioClock.hpp"
#include <vector>
#include <array>
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
// Keep the callback window around the first abnormal interval/recovery.
// Single callback writer; chronological reads only after callback removal.
template<class T,size_t N=512,size_t After=128> struct SyncIncidentWindow {
 static_assert(After<N);std::array<T,N> values{};uint64_t total=0,freezeAt=UINT64_MAX;bool triggered=false;
 void append(const T& value,bool incident){if(total>=freezeAt)return;values[total%N]=value;++total;if(incident&&!triggered){triggered=true;freezeAt=total+After;}}
 size_t size()const{return size_t(std::min<uint64_t>(total,N));}
 const T& at(size_t i)const{return values[(total-size()+i)%N];}
};
struct SyncCallbackEvent {
 int64_t host=0,entryGap=0,querySpan=0,stream=0,cursor=0,consumed=0,duration=0;
 uint32_t buffered=0;int reason=0;double phase=0,candidateRatio=1;bool preroll=false;
};
struct SyncScheduleEvent {int64_t frame=0,begin=0,end=0,completion=0,stream=INT64_MIN,streamHost=0,scheduled=0;};
struct SyncCompletionEvent {int64_t host=0,frame=-1;int result=0;};
struct SyncAudioEvent {
 int64_t stream=0,host=0;double source=0;AudioMapping mapping;
 double delay=0;bool transition=false;
};
struct SyncVideoEvent {
 int64_t stream=0,host=0,paint=0,content=INT64_MIN,historyTime=INT64_MIN,maxAhead=0,identity=-1;
 uint64_t sequence=0;double target=0;AudioMapping mapping;int32_t adjustment=0;
 uint32_t commonFrames=0;double commonStreamSamples=0;
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
               const AudioMapping& mapping,double delay,bool transition){
  for(uint32_t j=0;j<written;++j){
   if(std::abs(int64_t(stereo[size_t(j)*2]))>6442450){
    if(quiet>=4800)append({stream+j,host,mapping.at(stream+j)-delay,mapping,delay,transition});
    quiet=0;
   }else quiet=std::min<uint32_t>(4800,quiet+1);
  }
 }
};
}
