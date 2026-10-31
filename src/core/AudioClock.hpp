#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <optional>
namespace u2k {
// Host/source samples per device sample; updates pivot at the unwritten cursor.
struct AudioMapping {
 double source=0,ratio=1;int64_t stream=0;
 double at(int64_t position)const{return source+double(position-stream)*ratio;}
 void rateAt(int64_t position,double rate){source=at(position);stream=position;ratio=rate;}
};
// Bounded snapshot reads: video never spins waiting for a preempted audio callback.
class PublishedAudioMapping {
 std::atomic<uint64_t> version_{0};std::atomic<double> source_{0},ratio_{1};std::atomic<int64_t> stream_{0};
public:
 void publish(const AudioMapping& m){version_.fetch_add(1,std::memory_order_seq_cst);source_.store(m.source,std::memory_order_seq_cst);ratio_.store(m.ratio,std::memory_order_seq_cst);stream_.store(m.stream,std::memory_order_seq_cst);version_.fetch_add(1,std::memory_order_seq_cst);}
 std::optional<AudioMapping> read()const{for(int i=0;i<4;++i){const auto a=version_.load(std::memory_order_seq_cst);if(a&1)continue;AudioMapping m{source_.load(std::memory_order_seq_cst),ratio_.load(std::memory_order_seq_cst),stream_.load(std::memory_order_seq_cst)};if(a==version_.load(std::memory_order_seq_cst))return m;}return {};}
};
class AudioDriftServo {
 struct Point{double host=0,device=0;};std::array<Point,240> points_{};size_t size_=0,next_=0;
 int64_t epochHost_=0,epochDevice_=0,lastHost_=0,lastDevice_=0;double filteredPhase_=0;int outside_=0;
public:
 AudioMapping mapping;double estimatedRatio=1,phaseError=0,rateUncertainty=0;uint64_t rejected=0;bool ready=false;
 static constexpr double limitPpm=200,slewPpmPerSecond=5;
 void reset(int64_t host,int64_t device,double sourceOrigin){mapping={sourceOrigin,1,0};epochHost_=lastHost_=host;epochDevice_=lastDevice_=device;size_=next_=0;filteredPhase_=0;estimatedRatio=1;phaseError=0;rateUncertainty=0;outside_=0;ready=false;}
 // Returns false for an uncorrectable clock, requesting bounded coarse recovery.
 bool observe(int64_t host,int64_t consumed,int64_t cursor,uint32_t target,uint32_t age,int64_t querySpan){
  if(host-lastHost_>480000){++rejected;return false;}
  if(querySpan>48||host<=lastHost_||consumed<lastDevice_){++rejected;return true;}
  if(host-lastHost_<24000)return true;
  const double dt=double(host-lastHost_)/48000;lastHost_=host;lastDevice_=consumed;
  phaseError=double(host)-target-age-mapping.at(consumed);
  if(std::abs(phaseError)>4800){++rejected;return false;}
  filteredPhase_+=(1-std::exp(-dt/10))*(phaseError-filteredPhase_);
  points_[next_]={double(host-epochHost_),double(consumed-epochDevice_)};next_=(next_+1)%points_.size();size_=std::min(size_+1,points_.size());
  if(size_<20||host-epochHost_<20*48000)return true;
  double x=0,y=0;for(size_t i=0;i<size_;++i){x+=points_[i].device;y+=points_[i].host;}x/=size_;y/=size_;
  double xx=0,xy=0;for(size_t i=0;i<size_;++i){const auto a=points_[i].device-x;xx+=a*a;xy+=a*(points_[i].host-y);}if(xx<=0)return true;
  const double ratio=xy/xx;
  if(std::abs(ratio-1)>limitPpm*1e-6){++rejected;return ++outside_<10;}
  double residual=0;for(size_t i=0;i<size_;++i){const double error=(points_[i].host-y)-ratio*(points_[i].device-x);residual+=error*error;}
  rateUncertainty=std::sqrt(residual/double(size_-2)/xx);
  outside_=0;estimatedRatio=ratio;ready=true;
  // Queue queries are noisy. Do not turn an uncertain short fit into clock drift.
  const double trusted=std::abs(ratio-1)>std::max(.5e-6,4*rateUncertainty)?ratio:1.;
  // A 1 ms deadband and 60 s phase return do not chase 10 ms producer packets.
  const double phase=std::copysign(std::max(0.,std::abs(filteredPhase_)-48),filteredPhase_);
  const double requested=std::clamp(trusted+std::clamp(phase/(60*48000),-50e-6,50e-6),1-limitPpm*1e-6,1+limitPpm*1e-6);
  const double slew=slewPpmPerSecond*1e-6*dt;
  mapping.rateAt(cursor,mapping.ratio+std::clamp(requested-mapping.ratio,-slew,slew));return true;
 }
};
}
