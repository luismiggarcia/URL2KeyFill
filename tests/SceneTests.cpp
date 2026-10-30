#include "core/Scene.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace u2k;
#define REQUIRE(x) do{if(!(x))throw std::runtime_error(#x);}while(0)
int main(){try{
 auto front=std::make_shared<Frame>(Size{1,1});front->pixels={0,0,128,128};auto back=std::make_shared<Frame>(Size{1,1});back->pixels={255,0,0,255};
 auto combined=composite({front,back},{1,1});REQUIRE(combined->pixels==std::vector<uint8_t>({127,0,128,255}));auto reversed=composite({back,front},{1,1});REQUIRE(reversed->pixels==back->pixels);
 auto partial=composite({front,front},{1,1});REQUIRE(partial->pixels[3]==192);REQUIRE(partial->pixels[2]==192);REQUIRE(composite({}, {1,1})->pixels[3]==0);
 auto lane=std::make_shared<AudioLane>();std::vector<float> left(480,.25f),right(480,-.5f);const float* data[]={left.data(),right.data()};lane->push(data,480,2,1000);AudioMixer mixer;mixer.inputs({{lane,true,1}});auto pcm=mixer.mix(48000,480);REQUIRE(pcm[0]>530000000&&pcm[0]<540000000);REQUIRE(pcm[1]<-1000000000);
 mixer.inputs({{lane,false,1}});pcm=mixer.mix(48000,480);for(auto v:pcm)REQUIRE(v==0);mixer.inputs({{lane,true,2},{lane,true,2}});pcm=mixer.mix(48000,480);REQUIRE(pcm[0]==INT32_MAX);REQUIRE(pcm[1]==-INT32_MAX);REQUIRE(mixer.clipped>0);mixer.enabled=false;pcm=mixer.mix(48000,480);for(auto v:pcm)REQUIRE(v==0);
 lane->clear();mixer.enabled=true;pcm=mixer.mix(48000,480);for(auto v:pcm)REQUIRE(v==0);for(int i=0;i<100;i++)lane->push(data,480,2,1000+i*10);REQUIRE(lane->discarded>0);
 // Source meters must stay live while muted and must not consume or alter PCM.
 {auto meter=std::make_shared<AudioLane>();float l[]={.25f,-.75f,0},r[]={-.5f,.125f,0};const float* source[]={l,r};meter->push(source,3,2,1000);auto peaks=meter->takePeaks();REQUIRE(peaks.first==.75f&&peaks.second==.5f);auto empty=meter->takePeaks();REQUIRE(empty.first==0&&empty.second==0);
  std::vector<float> unchanged(6,0);meter->mix(48000,3,1,unchanged);REQUIRE(unchanged[0]==.25f&&unchanged[1]==-.5f);
  AudioMixer muted;muted.inputs({{meter,false,1}});meter->push(source,3,2,1010);auto silent=muted.mix(48480,3);for(auto sample:silent)REQUIRE(sample==0);peaks=meter->takePeaks();REQUIRE(peaks.first==.75f&&peaks.second==.5f);
  meter->push(source,3,2,1020);meter->clear();peaks=meter->takePeaks();REQUIRE(peaks.first==0&&peaks.second==0);}
 // Composite meters sum aligned PCM, honour layer gain/mute, and remain pre-master-mute.
 {auto a=std::make_shared<AudioLane>(),b=std::make_shared<AudioLane>();float positive[]={.25f,.25f},negative[]={-.25f,-.25f};const float* pa[]={positive,positive};const float* pb[]={negative,negative};a->push(pa,2,2,1000);b->push(pb,2,2,1000);AudioMixer mix;mix.inputs({{a,true,1},{b,true,1}});auto peaks=mix.sourcePeaks(48000,2);REQUIRE(peaks.first==0&&peaks.second==0);
  mix.inputs({{a,true,2},{b,false,1}});peaks=mix.sourcePeaks(48000,2);REQUIRE(peaks.first==.5f&&peaks.second==.5f);mix.muted=true;for(auto v:mix.mix(48000,2))REQUIRE(v==0);REQUIRE(mix.sourcePeaks(48000,2).first==.5f);mix.muted=false;mix.enabled=false;for(auto v:mix.mix(48000,2))REQUIRE(v==0);REQUIRE(mix.sourcePeaks(48000,2).first==.5f);
  REQUIRE(mix.sourcePeaks(96000,2).first==0);mix.enabled=true;REQUIRE(mix.mix(48000,2)[0]>1000000000);}
 // MONO folds L/R down by averaging and duplicates the result without a gain boost.
 {auto source=std::make_shared<AudioLane>();float l[]={.75f,.25f},r[]={-.25f,-.25f};const float* stereo[]={l,r};source->push(stereo,2,2,1000);AudioMixer mix;mix.inputs({{source,true,1,true}});auto peaks=mix.sourcePeaks(48000,2);REQUIRE(peaks.first==.25f&&peaks.second==.25f);auto pcm=mix.mix(48000,2);REQUIRE(pcm[0]==pcm[1]&&pcm[0]>530000000);REQUIRE(pcm[2]==0&&pcm[3]==0);mix.inputs({{source,true,1,false}});pcm=mix.mix(48000,2);REQUIRE(pcm[0]>0&&pcm[1]<0);}
 // RMS uses per-channel mean-square power, not peak height, and survives playback pruning.
 {AudioLane source;std::vector<float> l(100,.1f),r(100,0);r[0]=.4f;const float* data[]={l.data(),r.data()};source.push(data,100,2,1000);auto levels=source.sourceLevels(48000,100);REQUIRE(std::abs(levels.rmsLeft-.1f)<.000001f);REQUIRE(std::abs(levels.rmsRight-.04f)<.000001f);REQUIRE(levels.peakRight>levels.peakLeft&&levels.rmsRight<levels.rmsLeft);std::vector<float> discarded(2,0);source.mix(49000,1,1,discarded);auto retained=source.sourceLevels(48000,100);REQUIRE(retained.rmsLeft==levels.rmsLeft&&retained.rmsRight==levels.rmsRight);source.clear();REQUIRE(source.sourceLevels(48000,100).rmsLeft==0);}
 {AudioLane source;float l[]={1,0,-1,0},r[]={.25f,0,-.25f,0};const float* data[]={l,r};source.push(data,4,2,1000);auto levels=source.sourceLevels(48000,4);REQUIRE(std::abs(20*std::log10(levels.rmsLeft)+3.0103)<.0001);REQUIRE(std::abs(20*std::log10(levels.rmsLeft/levels.rmsRight)-12.0412)<.0001);}
 // Layer source meters use actual mono samples when MONO is enabled.
 {AudioLane source;float l[]={.75f,.25f},r[]={-.25f,-.25f};const float* data[]={l,r};source.push(data,2,2,1000);auto stereo=source.sourceLevels(48000,2),mono=source.sourceLevels(48000,2,true);REQUIRE(stereo.rmsLeft!=stereo.rmsRight);REQUIRE(mono.rmsLeft==mono.rmsRight&&mono.peakLeft==mono.peakRight);REQUIRE(std::abs(mono.rmsLeft-std::sqrt(.03125f))<.000001f);REQUIRE(mono.peakLeft==.25f);}
 // Queue expiry outside active routing is normal, not an output overrun.
 {AudioLane idle;for(int i=0;i<100;++i)idle.push(data,480,2,1000+i*10);REQUIRE(idle.unusedExpired>0&&idle.outputOverruns==0);REQUIRE(idle.discarded==idle.unusedExpired);
  AudioLane active;active.outputExpected=true;for(int i=0;i<100;++i)active.push(data,480,2,1000+i*10);REQUIRE(active.outputOverruns>0&&active.unusedExpired==0);
  AudioLane transition;for(int i=0;i<60;++i)transition.push(data,480,2,1000+i*10);auto normal=transition.unusedExpired.load();transition.outputExpected=true;transition.push(data,480,2,1600);REQUIRE(transition.unusedExpired>normal&&transition.outputOverruns==0);
  AudioLane muted;muted.outputExpected=true;for(int i=0;i<50;++i)muted.push(data,480,2,1000+i*10);muted.outputExpected=false;for(int i=50;i<110;++i)muted.push(data,480,2,1000+i*10);REQUIRE(muted.unusedExpired>0&&muted.outputOverruns==0);}
 // Local monitoring reads retained audio without consuming the SDI stream.
 {auto lane=std::make_shared<AudioLane>();float l[]={.5f,.5f},r[]={.25f,.25f};const float* channels[]={l,r};lane->push(channels,2,2,1000);AudioMixer mix;mix.inputs({{lane,true,1,false,false,true}});auto monitor=mix.sample(48000,2);REQUIRE(monitor[0]==0&&monitor[1]==.25f);auto output=mix.mix(48000,2);REQUIRE(output[0]==0&&output[1]>0);mix.inputs({{lane,true,1}});mix.gain=.5f;mix.mono=true;monitor=mix.sample(48000,2);REQUIRE(monitor[0]==.1875f&&monitor[1]==.1875f);mix.left=false;monitor=mix.sample(48000,2);REQUIRE(monitor[0]==0&&monitor[1]==.1875f);mix.muted=true;for(auto v:mix.sample(48000,2))REQUIRE(v==0);REQUIRE(mix.sourceLevels(48000,2).rmsRight==.1875f);}
 auto frames=std::make_shared<LatestFrame>();frames->publish(front);Scene scene;scene.configure({{frames,true}},{1,1},{4,2});for(int i=0;i<100&&!scene.frames.get();i++)std::this_thread::sleep_for(std::chrono::milliseconds(2));auto image=scene.frames.get();REQUIRE(image);REQUIRE(image->pixels[3]==0);REQUIRE(image->pixels[7]==128);scene.configure({{frames,false}},{1,1},{4,2});for(int i=0;i<100;i++){auto current=scene.frames.get();if(current&&current->pixels[7]==0)break;std::this_thread::sleep_for(std::chrono::milliseconds(2));}REQUIRE(scene.frames.get()->pixels[7]==0);
 std::cout<<"PASS: composition order, premultiplied alpha, transparent fit, visibility, stereo mixing, gain, limiting, mute, bounded queues\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
