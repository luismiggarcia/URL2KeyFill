#pragma once
#include "AudioClock.hpp"
#include "Core.hpp"
#include <cmath>
#include <optional>
namespace u2k {
struct AVDelay {
 int32_t signedSamples=0;uint32_t videoFrames=0;double streamSamples=0;
 double videoSourceDelay(const AudioMapping& m)const{return streamSamples*m.ratio;}
 double audioSourceDelay(const AudioMapping& m)const{return videoSourceDelay(m)+signedSamples;}
 double videoSource(const AudioMapping& m,int64_t position)const{return videoFrames?m.source+(double(position-m.stream)-streamSamples)*m.ratio:m.at(position);}
 double audioSource(const AudioMapping& m,int64_t position)const{return videoSource(m,position)-signedSamples;}
};
// Host time at which SDI stream frame `frame` selects its video source, exactly as
// DeckLink::schedule computes it (stream position, mapping, common video delay).
inline double videoSelectionTime(const AudioMapping& m,int64_t duration,int64_t scale,uint64_t frame,double delay){
 const auto stream=int64_t(frame)*duration*48000/scale;return m.source+(double(stream-m.stream)-delay)*m.ratio;
}
// First stream-frame selection time at or after `host`: the render clock for a source
// that should paint exactly on the output's selection grid.
inline std::optional<int64_t> nextVideoSelectionTime(const AudioMapping& m,int64_t duration,int64_t scale,double delay,int64_t host){
 if(!(m.ratio>0)||duration<=0||scale<=0)return {};
 const double stream=(double(host)-m.source)/m.ratio+double(m.stream)+delay;
 auto frame=std::max<int64_t>(0,int64_t(std::floor(stream*double(scale)/(48000.*double(duration))))-1);
 for(int k=0;k<4;++k,++frame){const auto t=videoSelectionTime(m,duration,scale,uint64_t(frame),delay);if(t>=double(host))return int64_t(std::ceil(t));}
 return {};
}
// Validate rational geometry once outside the callback. Runtime plans allocate
// nothing and preserve zero/positive adjustment without a common frame delay.
class AVTiming {
 int64_t numerator_=0,scale_=0;
public:
 explicit AVTiming(Rate rate={}){rate.validate();numerator_=rate.duration*48000;scale_=rate.scale;}
 AVDelay plan(int32_t samples)const noexcept{
  if(samples>=0)return {samples,0,0};
  const auto frames=uint32_t((-int64_t(samples)*scale_+numerator_-1)/numerator_);
  return {samples,frames,double(frames)*double(numerator_)/double(scale_)};
 }
 // The Reliable profile (3+ frames) keeps at least 100 ms of audio queued. DeckLink audio
 // callbacks arrive every ~20 ms and were seen to pause ~52 ms at 59.94p, which emptied a
 // 3-frame (50 ms) queue; 25p's 120 ms never underran in 8 hours (audit §11.5).
 static constexpr uint32_t reliableAudioFloor=4800;
 uint32_t audioTarget(uint32_t reserve)const noexcept{const auto frames=(reserve*numerator_+scale_-1)/scale_;return uint32_t(std::clamp<int64_t>(reserve>=3?std::max<int64_t>(frames,reliableAudioFloor):frames,1440,12000));}
 // Include preroll, source age and half-frame lookahead. One additional frame
 // is retained by recommendedHistoryFrames for scheduling/paint jitter.
 size_t minimumHistoryFrames(int32_t samples,uint32_t reserve,uint32_t age)const noexcept{
  const double period=double(numerator_)/scale_;
  const double lookback=audioTarget(reserve)+age+plan(samples).streamSamples*1.0002-period/2;
  return size_t(std::ceil(std::max(0.,lookback)/period))+1;
 }
 size_t recommendedHistoryFrames(uint32_t age)const noexcept{return std::max<size_t>(16,minimumHistoryFrames(-9600,3,age)+1);}
};
}
