#pragma once
#include "AudioClock.hpp"
#include "Core.hpp"
namespace u2k {
struct AVDelay {
 int32_t signedSamples=0;uint32_t videoFrames=0;double streamSamples=0;
 double videoSourceDelay(const AudioMapping& m)const{return streamSamples*m.ratio;}
 double audioSourceDelay(const AudioMapping& m)const{return videoSourceDelay(m)+signedSamples;}
 double videoSource(const AudioMapping& m,int64_t position)const{return videoFrames?m.source+(double(position-m.stream)-streamSamples)*m.ratio:m.at(position);}
 double audioSource(const AudioMapping& m,int64_t position)const{return videoSource(m,position)-signedSamples;}
};
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
 uint32_t audioTarget(uint32_t reserve)const noexcept{return uint32_t(std::clamp<int64_t>((reserve*numerator_+scale_-1)/scale_,1440,12000));}
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
