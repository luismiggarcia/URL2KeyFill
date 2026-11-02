#include "core/SyncDiagnostics.hpp"
#include "core/Core.hpp"
#include <iostream>
#include <stdexcept>
#include <cmath>
using namespace u2k;
#define REQUIRE(x) do{if(!(x))throw std::runtime_error(#x);}while(0)
int main(){try{
 // Partial writes: scanning the full attempted block would record the second
 // pulse too early and suppress it when the accepted suffix is resubmitted.
 std::vector<int32_t> pcm(12000*2);pcm[7*2]=10000000;pcm[6007*2]=10000000;
 AudioMapping map{1000000,1.000001,0};SyncAudioTrace split,whole;
 split.accepted(nullptr,0,100,200,map,960,false);REQUIRE(split.events.empty());
 split.accepted(pcm.data(),6000,100,200,map,960,false);
 split.accepted(pcm.data()+12000,6000,6100,300,map,960,false);
 whole.accepted(pcm.data(),12000,100,200,map,960,false);
 REQUIRE(split.events.size()==2&&whole.events.size()==2);
 for(size_t i=0;i<2;++i){REQUIRE(split.events[i].stream==whole.events[i].stream);REQUIRE(split.events[i].source==whole.events[i].source);}
 REQUIRE(split.events[0].stream==107&&split.events[1].stream==6107);
 const auto capacity=split.events.capacity();
 for(size_t i=0;i<SyncAudioTrace::limit+3;++i)split.append({});
 REQUIRE(split.events.size()==SyncAudioTrace::limit);REQUIRE(split.omitted==5);REQUIRE(split.events.capacity()==capacity);
 // A large negative sample must not overflow the absolute-value detector.
 SyncAudioTrace negative;int32_t low[]={INT32_MIN,INT32_MIN};negative.accepted(low,1,0,0,map,0,true);REQUIRE(negative.events.size()==1&&negative.events[0].transition);
 // Actual FrameHistory selection over 48 simulated hours, matched and mixed
 // source/output cadence, fixed paint lag. Sweep all initial frame phases.
 // This isolates grid quantisation, not producer jitter or content provenance.
 for(int sourceFps:{25,50})for(int fps:{25,50})for(double ppm:{-1.,0.,1.})for(int phase=0;phase<16;++phase){
  const int64_t frame=48000/fps,sourceFrame=48000/sourceFps,lag=1440,delay=1440;AudioMapping m{double(phase*frame)/16,1+ppm*1e-6,0};
  double lo=1e9,hi=-1e9,last=0,maxStep=0;FrameHistory history;
  for(int second=60;second<=48*3600;second+=60){
   const int64_t pulse=int64_t(second)*48000,index=pulse/sourceFrame;
   history.configure(8,1024,std::max(frame,sourceFrame)*8);
   for(int k=-3;k<=3;++k){auto image=std::make_shared<Frame>(Size{1,1},uint64_t(index+k));image->paintTime=(index+k)*sourceFrame+lag;history.publish(image,image->paintTime);}
   const int64_t centre=int64_t(std::floor((pulse+lag-m.source)/m.ratio/frame));
   int64_t first=INT64_MIN;
   for(int64_t out=centre-2;out<=centre+3;++out){auto chosen=history.at(int64_t(std::llround(m.at(out*frame))),frame/2);if(chosen.frame&&chosen.frame->sequence>=uint64_t(index)&&chosen.frame->sequence<uint64_t(index+sourceFps/25)){first=out*frame;break;}}
   REQUIRE(first!=INT64_MIN);const double audio=(pulse+delay-m.source)/m.ratio;
   const double av=(audio-first)/48;lo=std::min(lo,av);hi=std::max(hi,av);if(second>60)maxStep=std::max(maxStep,std::abs(av-last));last=av;
   const double advance=double(std::min(frame,sourceFrame))/96;
   REQUIRE(av>=advance-double(frame)/48-1./48&&av<=advance+1./48);
  }
  REQUIRE(hi-lo<=double(frame)/48+1./48);
  if(ppm!=0){REQUIRE(hi-lo>double(frame)/48-1);REQUIRE(maxStep>double(frame)/48-1);}
  else REQUIRE(hi-lo<1e-8);
  std::cout<<"grid source_fps="<<sourceFps<<" output_fps="<<fps<<" ppm="<<ppm<<" phase="<<phase<<" min_ms="<<lo<<" max_ms="<<hi<<" largest_step_ms="<<maxStep<<'\n';
 }
 // Retain the historical quantised policy as a diagnostic comparison. The
 // production AVAdjustment helper has its own variable-rate and rational tests.
 for(int fps:{25,50})for(int ms:{-5,-20,-25}){
  const int64_t frame=48000/fps,pulse=48000*100,desired=ms*48;
  double lo=1e9,hi=-1e9;
  for(int phase=0;phase<16;++phase){
   AudioMapping m{double(phase*frame)/16,1,0};FrameHistory history;history.configure(12,1024,frame*12);
   const auto index=pulse/frame;
   for(int k=-4;k<=4;++k){auto image=std::make_shared<Frame>(Size{1,1},uint64_t(index+k));history.publish(image,(index+k)*frame);}
   const auto centre=int64_t(std::floor((pulse-m.source)/frame));
   auto onset=[&](int64_t sourceDelay,int64_t streamDelay){
    for(int64_t out=centre-2;out<=centre+6;++out){const auto selected=history.at(int64_t(std::llround(m.at(out*frame-streamDelay)-sourceDelay)),frame/2);if(selected.frame&&selected.frame->sequence==uint64_t(index))return out*frame;}
    throw std::runtime_error("Missing calibration onset");
   };
   const auto base=onset(0,0),current=onset(-desired,0);
   const double delta=double(base-current)/48;lo=std::min(lo,delta);hi=std::max(hi,delta);
   REQUIRE(std::abs(delta-ms)<=double(frame)/48);
   const auto common=((-desired+frame-1)/frame)*frame;
   const auto alternative=onset(0,common);
   REQUIRE(alternative-base==common);
   REQUIRE(double(common+desired+base-alternative)/48==ms);
  }
  std::cout<<"negative_adjustment fps="<<fps<<" requested_ms="<<ms<<" legacy_min_ms="<<lo<<" legacy_max_ms="<<hi<<" common_frame_prototype_delta_ms="<<ms<<" prototype_only=1\n";
 }
 return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
