#include "core/AVAdjustment.hpp"
#include "core/AVCalibration.hpp"
#include <iostream>
#include <stdexcept>
#include <cmath>
using namespace u2k;
#define REQUIRE(x) do{if(!(x))throw std::runtime_error(#x);}while(0)
int main(){try{
 size_t plans=0,onsets=0;
 for(Rate rate: {Rate{1,24},Rate{1,25},Rate{1,30},Rate{1,50},Rate{1,60},Rate{1001,24000},Rate{1001,30000},Rate{1001,60000}}){
  AVTiming timing(rate);const double period=double(rate.duration)*48000/rate.scale;
  for(int ms=-200;ms<=200;++ms)for(double ratio:{.9998,.999999,1.,1.000001,1.0002}){
   auto p=timing.plan(ms*48);AudioMapping m{1e10+.371,ratio,12345};
   const auto audio=p.audioSource(m,1000000),video=p.videoSource(m,1000000);
   REQUIRE(std::abs(video-audio-ms*48)<.00001);
   REQUIRE(std::abs(m.at(1000000)-audio-p.audioSourceDelay(m))<.00001);
   if(ms>=0){REQUIRE(p.videoFrames==0&&p.streamSamples==0);REQUIRE(audio==m.at(1000000)-ms*48);REQUIRE(video==m.at(1000000));}
   else{REQUIRE(p.streamSamples>=-ms*48);REQUIRE(p.streamSamples+ms*48<period+1e-8);REQUIRE(p.audioSourceDelay(m)>=-1.921);REQUIRE(p.audioSourceDelay(m)<=period*1.0002+1.921);}
   ++plans;
  }
 }
 // The production helper, not a duplicate formula, selects the same image N
 // output frames later at integer cadences, across all phases and drift limits.
 for(int fps:{25,50})for(double ratio:{.9998,.999999,1.,1.000001,1.0002})for(int phase=0;phase<16;++phase){
  const int64_t frame=48000/fps,epoch=10000000000LL,pulse=epoch+48000*100,index=(pulse-epoch)/frame;
  AudioMapping m{double(epoch)+double(phase*frame)/16,ratio,0};AVTiming timing(Rate{1,fps});FrameHistory history;history.configure(40,4096,frame*40);
  for(int k=-4;k<=4;++k){auto image=std::make_shared<Frame>(Size{1,1},uint64_t(index+k));history.publish(image,epoch+(index+k)*frame);}
  const auto centre=int64_t(std::floor((pulse-m.source)/m.ratio/frame));
  auto onset=[&](const AVDelay& p){for(int64_t out=centre-2;out<=centre+14;++out){auto chosen=history.at(int64_t(std::llround(p.videoSource(m,out*frame))),frame/2);if(chosen.frame&&chosen.frame->sequence==uint64_t(index))return out*frame;}throw std::runtime_error("Missing delayed onset");};
  const auto base=onset(timing.plan(0));
  for(int ms:{-1,-5,-19,-20,-21,-25,-39,-40,-41,-199,-200,0,1,200}){
   auto p=timing.plan(ms*48);const auto video=onset(p);REQUIRE(video-base==int64_t(p.videoFrames)*frame);
   const double actualChange=(p.audioSourceDelay(m)/m.ratio+base-video)/48;
   REQUIRE(std::abs(actualChange-ms/m.ratio)<1e-8);++onsets;
  }
 }
 bool rejected=false;try{AVTiming invalid(Rate{0,50});}catch(const std::invalid_argument&){rejected=true;}REQUIRE(rejected);
 REQUIRE(AVTiming(Rate{1,25}).recommendedHistoryFrames(2880)==16);
 REQUIRE(AVTiming(Rate{1,50}).recommendedHistoryFrames(2880)==20);
 REQUIRE(AVTiming(Rate{1001,60000}).recommendedHistoryFrames(2880)==24);
 // Reliable keeps >= 100 ms of audio at every cadence; Low latency stays frame based.
 REQUIRE(AVTiming(Rate{1,25}).videoFrames(3)==3);REQUIRE(AVTiming(Rate{1,50}).videoFrames(3)==5);REQUIRE(AVTiming(Rate{1001,60000}).videoFrames(3)==6);REQUIRE(AVTiming(Rate{1,60}).videoFrames(3)==6);REQUIRE(AVTiming(Rate{1001,60000}).videoFrames(2)==2);
 // Spare slots cover a 200 ms completion pause at every cadence and profile.
 REQUIRE(AVTiming(Rate{1,25}).videoPool(3)==8);REQUIRE(AVTiming(Rate{1,50}).videoPool(3)==15);REQUIRE(AVTiming(Rate{1001,60000}).videoPool(3)==18);REQUIRE(AVTiming(Rate{1001,60000}).videoPool(2)==14);REQUIRE(AVTiming(Rate{1,60}).videoPool(3)==18);
 for(Rate rate:{Rate{1,25},Rate{1,50},Rate{1001,60000},Rate{1,60}})for(uint32_t reserve:{2u,3u}){AVTiming t(rate);REQUIRE(t.videoPool(reserve)<=AVTiming::maxVideoFrames);REQUIRE((t.videoPool(reserve)-t.videoFrames(reserve))*rate.duration*48000>=AVTiming::completionPauseSamples*uint64_t(rate.scale));}
 // Topping up: after preroll nothing is due; a completion at a frame boundary leaves one
 // frame due; a 90 ms pause at 59.94p leaves six due, which only spare slots can supply.
 REQUIRE(videoFramesAhead(7,0,1001)==7);REQUIRE(videoFramesAhead(7,1001,1001)==6);REQUIRE(videoFramesAhead(7,1000,1001)==7);
 REQUIRE(videoFramesAhead(12,7*1001+3,1001)==5);REQUIRE(6-videoFramesAhead(12,(7+5)*1001+400,1001)==6);
 REQUIRE(AVTiming(Rate{1,25}).audioTarget(3)==5760);REQUIRE(AVTiming(Rate{1,50}).audioTarget(3)==4800);REQUIRE(AVTiming(Rate{1001,60000}).audioTarget(3)==4800);REQUIRE(AVTiming(Rate{1001,60000}).audioTarget(2)==1602);
 for(Rate rate:{Rate{1,25},Rate{1,50},Rate{1001,60000}}){
  AVTiming t(rate);const auto retained=t.recommendedHistoryFrames(2880);
  for(int ms=-200;ms<0;++ms)for(uint32_t reserve:{2u,3u}){
   REQUIRE(t.minimumHistoryFrames(ms*48,reserve,2880)<=retained);
   REQUIRE(retained*size_t(1920)*1080*8<=384u*1024*1024);
  }
 }
 // Render clock grid: every SDI selection time is returned once, in order, at 59.94p, under
 // rate correction and a common video delay; times between frames map to the next frame.
 for(double ratio:{1.,1.0002,.9998})for(double delay:{0.,2402.4}){
  const AudioMapping m{6'000'000'000.5,ratio,123'456};const int64_t duration=1001,scale=60000;int64_t host=6'000'100'000;
  for(int k=0;k<600;++k){const auto next=nextVideoSelectionTime(m,duration,scale,delay,host);REQUIRE(next&&*next>=host);
   const double frame=((double(*next)-m.source)/m.ratio+double(m.stream)+delay)*double(scale)/(48000.*double(duration));
   const auto exact=videoSelectionTime(m,duration,scale,uint64_t(std::llround(frame)),delay);REQUIRE(std::abs(double(*next)-exact)<1.);
   const auto again=nextVideoSelectionTime(m,duration,scale,delay,*next+1);REQUIRE(again&&*again-*next>=799&&*again-*next<=803);host=*next+400;}
 }
 REQUIRE(!nextVideoSelectionTime({0,0,0},1001,60000,0,1000));
 // Calibrated defaults exist only for measured cadences; manual adjustment adds and clamps.
 for(const Rate r:{Rate{1000,25000},Rate{1000,50000},Rate{1001,60000}})REQUIRE(calibratedSyncMs(r).has_value());
 REQUIRE(!calibratedSyncMs(Rate{1001,30000}));REQUIRE(!calibratedSyncMs(Rate{1000,60000}));
 REQUIRE(cefRateMultiplier(Rate{1000,25000})==2);REQUIRE(cefRateMultiplier(Rate{1000,50000})==1);REQUIRE(cefRateMultiplier(Rate{1001,60000})==1);
 REQUIRE(cefRateMultiplier(Rate{1000,60000})==1);REQUIRE(cefRateMultiplier(Rate{1001,30000})==2);REQUIRE(cefRateMultiplier(Rate{1001,24000})==2);
 REQUIRE(effectiveSyncMs(std::nullopt,-35)==-35);REQUIRE(effectiveSyncMs(30,15)==45);
 REQUIRE(effectiveSyncMs(150,100)==200);REQUIRE(effectiveSyncMs(-150,-100)==-200);
 std::cout<<"PASS plans="<<plans<<" physical-grid onset comparisons="<<onsets<<" including zero/positive unchanged and +/-200 ppm\n";
 return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
