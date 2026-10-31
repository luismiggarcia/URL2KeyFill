#include "core/Scene.hpp"
#include "core/AudioSchedule.hpp"
#include "core/MediaClock.hpp"
#include <iostream>
#include <stdexcept>
using namespace u2k;
#define REQUIRE(x) do{if(!(x))throw std::runtime_error(#x);}while(0)
int main(){try{
 REQUIRE(samplesFromNanoseconds(1'000'000'000)==48000);
 REQUIRE(samplesFromNanoseconds(9'000'000'000'000'000'000LL)==432'000'000'000'000LL);
 REQUIRE(mediaSampleTime()>0);
 ClockGapDetector gaps;
 REQUIRE(!gaps.observe({100,500,0}));REQUIRE(!gaps.observe({1000,1400,0}));
 REQUIRE(gaps.observe({1000,1'000'001'400,0}));
 REQUIRE(!gaps.observe({2000,1'000'002'400,0}));
 REQUIRE(!gaps.observe({3000,2'000'003'400,100'000'000}));
 REQUIRE(!gaps.observe({4000,1'000'004'400,0}));
 AudioSchedule schedule(48000-5760,5760);
 // The source offset is independent of the size of the device preroll queue.
 for(uint32_t target:{2880u,5760u}){
  auto raw=AudioSchedule::fromHostAnchor(480000,target,2880);raw.advance(target);
  REQUIRE(raw.readStart()==480000-2880);
 }
 auto aligned=AudioSchedule::fromHostAnchor(1000000,5760,2880);aligned.advance(5760);
 REQUIRE(aligned.calibrate(1003360,0,2880)==3360);REQUIRE(aligned.readStart()==1003360-2880);
 REQUIRE(aligned.bufferedSpan(100,0)==5760); // Bound preroll writes before the device starts.
 REQUIRE(aligned.recover(6000,480));REQUIRE(aligned.cursor()==6480);
 REQUIRE(aligned.bufferedSpan(0,6000)==480);
 aligned.advance(5760-480);REQUIRE(aligned.readStart()==1003360+6000-2880);
 REQUIRE(audioTransitionWeight(0,240)==0);REQUIRE(audioTransitionWeight(239,240)==1);
 REQUIRE(audioTransitionWeight(17,240)<audioTransitionWeight(18,240));
 // Calibrate against drained audio position, independent of video stream phase.
 auto queue=AudioSchedule::fromHostAnchor(1000000,5760,2880);queue.advance(12000);
 queue.calibrateQueue(1100000,4000,2880);queue.advance(5760-4000);
 REQUIRE(queue.readStart()==1100000-2880);
 ClockPhaseAcquisition acquisition;acquisition.reset(1000);
 REQUIRE(!acquisition.observe(1000000,0,1));REQUIRE(!acquisition.observe(1000100,100,1));
 REQUIRE(!acquisition.observe(1000200,200,1));REQUIRE(!acquisition.observe(1000300,300,1));
 REQUIRE(!acquisition.observe(1007000,1000,1)); // Startup offset changed: reacquire.
 REQUIRE(!acquisition.observe(1007100,1100,1));REQUIRE(!acquisition.observe(1007200,1200,1));
 REQUIRE(acquisition.observe(1007300,1300,1));
 // Initial silence ends exactly on a frame boundary, even after partial writes.
 for(uint32_t lead:{960u,1920u}){
  auto startup=AudioSchedule::fromHostAnchor(1000000-lead,lead*3,2880);
  uint32_t buffered=0,padding=0;
  while(startup.paddingRemaining(lead)){const auto count=startup.blockSize(buffered,lead);REQUIRE(count<=startup.paddingRemaining(lead));const auto written=std::min(count,137u);padding+=written;buffered+=written;startup.advance(written);}
  REQUIRE(padding==lead);REQUIRE(startup.cursor()==lead);REQUIRE(startup.paddingRemaining(lead)==0);
  const auto firstProgramme=startup.readStart();REQUIRE(firstProgramme==1000000-int64_t(lead*3)-2880);
  REQUIRE(startup.blockSize(buffered,lead)==std::min(1024u,lead*2));
  startup.advance(startup.blockSize(buffered,lead));REQUIRE(startup.paddingRemaining(lead)==0);
 }
 REQUIRE(schedule.readStart()==42240);REQUIRE(schedule.blockSize(0)==1024);
 REQUIRE(schedule.blockSize(5700)==60);REQUIRE(schedule.blockSize(5760)==0);
 schedule.advance(400);REQUIRE(schedule.readStart()==42640);REQUIRE(schedule.cursor()==400);
 REQUIRE(!schedule.catchUp(399));REQUIRE(schedule.catchUp(1000));REQUIRE(schedule.readStart()==43240);
 schedule.advance(0);REQUIRE(schedule.cursor()==1000);
 // Silence is covered audio; meters/monitor must not affect SDI diagnostics.
 auto lane=std::make_shared<AudioLane>();float silent[480]={};const float* data[]={silent,silent};
 lane->push(data,480,2,1000);std::vector<float> out(960,0);
 lane->sample(48000,480,1,out);REQUIRE(lane->outputReads==0);
 lane->mix(48000,480,1,out);REQUIRE(lane->missingFrames==0);REQUIRE(lane->sourceHeadroom==0);
 // Positive headroom does not imply continuous coverage: an internal 10 ms gap.
 lane->push(data,480,2,1020);out.assign(2400,0);
 lane->mix(48000,1200,1,out);REQUIRE(lane->missingFrames==480);REQUIRE(lane->sourceHeadroom==240);
 // Overlapping packets count coverage once, even though the current mixer sums them.
 auto overlap=std::make_shared<AudioLane>();overlap->push(data,480,2,1000);overlap->push(data,480,2,1005);
 out.assign(1440,0);overlap->mix(48000,720,1,out);REQUIRE(overlap->missingFrames==0);
 // Old retained history does not cover a later window: report the real deficit.
 out.assign(960,0);overlap->mix(96000,480,1,out);REQUIRE(overlap->missingFrames==480);REQUIRE(overlap->sourceHeadroom==-47760);
 overlap->clear();overlap->mix(96000,480,1,out);REQUIRE(overlap->missingFrames==480);REQUIRE(overlap->noSourceReads==1);REQUIRE(overlap->sourceHeadroom==INT64_MIN);
 // Route expectation includes cross-channel and mono behaviour.
 REQUIRE(!audioRouteEnabled(false,true,false,false,false,true));
 REQUIRE(audioRouteEnabled(true,true,false,false,false,true));
 REQUIRE(audioRouteEnabled(false,true,false,true,false,true));
 REQUIRE(!audioRouteEnabled(true,false,false,true,true,true));
 AudioMixer mixer;mixer.inputs({{lane,true,1,false,true,false}});mixer.left=false;
 auto reads=lane->outputReads.load();mixer.mix(48000,480);REQUIRE(lane->outputReads==reads);
 mixer.mono=true;mixer.mix(48000,480);REQUIRE(lane->outputReads==reads+1);
 mixer.gain=0;reads=lane->outputReads.load();mixer.mix(48000,480);REQUIRE(lane->outputReads==reads);
 // Destination PAUSE must not alter the shared mixer's monitor state.
 mixer.gain=1;for(auto v:mixer.mix(48000,480,false))REQUIRE(v==0);
 REQUIRE(mixer.enabled);REQUIRE(lane->outputReads==reads);
 auto w=lane->takeReadWindow();REQUIRE(w.reads==3);REQUIRE(w.missing==480);
 REQUIRE(w.headroomCount==3);REQUIRE(w.headroomMin==0);REQUIRE(w.headroomMax==960);
 REQUIRE(lane->takeReadWindow().reads==0);REQUIRE(lane->missingFrames==480);
 // Duplicates and old packets must not amplify sound or erase valid history.
 auto continuous=std::make_shared<AudioLane>();float tone[480];std::fill_n(tone,480,.25f);const float* stereo[]={tone,tone};
 continuous->push(stereo,480,2,1000);continuous->push(stereo,480,2,1000);
 out.assign(960,0);continuous->sample(48000,480,1,out);REQUIRE(out[0]==.25f);REQUIRE(continuous->duplicatePackets==1);
 continuous->push(stereo,480,2,990);REQUIRE(continuous->reorderedPackets==1);
 out.assign(960,0);continuous->sample(48000,480,1,out);REQUIRE(out[0]==.25f);
 // One millisecond of timestamp quantisation preserves the sample counter.
 continuous->push(stereo,480,2,1009);REQUIRE(continuous->timestampAdjustments==1);
 out.assign(1920,0);continuous->sample(48000,960,1,out);for(auto v:out)REQUIRE(v==.25f);
 continuous->clear();continuous->push(stereo,480,2,1000);continuous->push(stereo,480,2,1005);
 REQUIRE(continuous->trimmedFrames==240);out.assign(1440,0);continuous->sample(48000,720,1,out);for(auto v:out)REQUIRE(v==.25f);
 continuous->push(stereo,480,2,1020);REQUIRE(continuous->timestampGapFrames==240);
 out.assign(2400,0);continuous->mix(48000,1200,1,out);REQUIRE(continuous->missingFrames==240);
 // A new generation can restart timestamps; sub-ms equal stamps are not duplicates.
 const auto generation=continuous->generation.load();continuous->clear();REQUIRE(continuous->generation==generation+1);
 continuous->push(stereo,3,2,0);continuous->push(stereo,3,2,0);
 out.assign(12,0);continuous->sample(0,6,1,out);for(auto v:out)REQUIRE(v==.25f);
 // Preroll before a stream existed is silence padding, not lost stream data.
 AudioLane fresh;fresh.push(stereo,480,2,1000);out.assign(40,0);fresh.mix(47990,20,1,out);
 REQUIRE(fresh.preSourcePaddingFrames==10);REQUIRE(fresh.missingFrames==0);
 // A backward startup correction can read retained data without inventing a gap.
 AudioLane rewind;rewind.push(stereo,480,2,1000);rewind.push(stereo,480,2,1010);
 out.assign(960,0);rewind.mix(48480,480,1,out);out.assign(960,0);rewind.mix(48000,480,1,out);
 for(auto v:out)REQUIRE(v==.25f);REQUIRE(rewind.missingFrames==0);
 float ramp[480];for(int i=0;i<480;++i)ramp[i]=.2f+float(i)/1000;const float* rampStereo[]={ramp,ramp};
 AudioLane trimmed;trimmed.push(stereo,480,2,1000);trimmed.push(rampStereo,480,2,1005);
 out.assign(480,0);trimmed.sample(48480,240,1,out);REQUIRE(out[0]==ramp[240]);REQUIRE(out[478]==ramp[479]);
 AudioLane invalid;invalid.push(stereo,480,2,-1);invalid.push(stereo,480,2,INT64_MAX);REQUIRE(invalid.received==0);
 // Reproduce the observed 12-frame tail deficit with contiguous packet timestamps.
 AudioLane late;late.push(stereo,480,2,1000);out.assign(960,0);
 late.mix(48012,480,1,out);for(size_t i=0;i<480;++i)REQUIRE(out[i*2]==(i<468?.25f:0.f));REQUIRE(late.missingFrames==12);REQUIRE(late.sourceHeadroom==-12);
 auto gap=late.takeReadWindow().gap;REQUIRE(gap.sequence==1);REQUIRE(gap.missing==12);
 REQUIRE(gap.start==48012&&gap.end==48492&&gap.newestEnd==48480);
 REQUIRE(gap.newestPublished>=gap.newestArrival);REQUIRE(!gap.recoveryPublished);
 late.push(stereo,480,2,1010);gap=late.takeReadWindow().gap;
 REQUIRE(gap.sequence==1);REQUIRE(gap.recoveryEnd==48960);REQUIRE(gap.recoveryStart==48480);
 REQUIRE(gap.recoveryPublished>=gap.readHost);REQUIRE(late.timestampGapFrames==0);
 REQUIRE(late.timestampAdjustments==0);REQUIRE(late.takeReadWindow().gap.sequence==1);
 // The missing tail really was silence, and subsequent data is not consumed twice.
 out.assign(960,0);late.mix(48492,480,1,out);REQUIRE(late.missingFrames==24);
 gap=late.takeReadWindow().gap;REQUIRE(gap.sequence==2);late.clear();
 late.push(stereo,480,2,1000);REQUIRE(!late.takeReadWindow().gap.recoveryPublished);
 // Source pulse diagnostics use accepted sample PTS and are independent of output reads.
 AudioLane sourceProbe;sourceProbe.syncDiagnostic=true;sourceProbe.push(stereo,480,2,1000);
 REQUIRE(sourceProbe.syncPulsePts==48000);REQUIRE(sourceProbe.syncPulseCount==1);
 sourceProbe.push(stereo,480,2,1010);REQUIRE(sourceProbe.syncPulseCount==1);
 for(int i=0;i<10;++i)sourceProbe.push(data,480,2,1020+i*10);
 sourceProbe.push(stereo,480,2,1120);REQUIRE(sourceProbe.syncPulseCount==2);REQUIRE(sourceProbe.outputReads==0);
 sourceProbe.clear();REQUIRE(sourceProbe.syncPulsePts==INT64_MIN);
 // Exercise clear/publication/read interleaving with immutable source buffers.
 AudioLane concurrent;std::atomic<bool> start{false},finished{false};
 std::thread producer([&]{while(!start.load())std::this_thread::yield();for(int i=0;i<500;++i)concurrent.push(stereo,480,2,1000+i*10);finished=true;});
 std::thread reader([&]{std::vector<float> buffer(960);while(!start.load())std::this_thread::yield();while(!finished.load()){std::fill(buffer.begin(),buffer.end(),0);concurrent.mix(48000,480,1,buffer);concurrent.takeReadWindow();}});
 start=true;for(int i=0;i<500;++i)concurrent.clear();producer.join();reader.join();concurrent.clear();
 REQUIRE(concurrent.sourceLevels(48000,480).peakLeft==0);
 std::cout<<"PASS: source coverage, headroom, silent packets, overlap, monitor isolation and effective routing\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
