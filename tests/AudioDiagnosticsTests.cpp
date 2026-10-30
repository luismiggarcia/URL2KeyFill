#include "core/Scene.hpp"
#include <iostream>
#include <stdexcept>
using namespace u2k;
#define REQUIRE(x) do{if(!(x))throw std::runtime_error(#x);}while(0)
int main(){try{
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
 // No retained packet: missing coverage, no invented numeric headroom.
 out.assign(960,0);overlap->mix(96000,480,1,out);REQUIRE(overlap->missingFrames==480);REQUIRE(overlap->sourceHeadroom==INT64_MIN);
 overlap->clear();overlap->mix(96000,480,1,out);REQUIRE(overlap->missingFrames==480);REQUIRE(overlap->noSourceReads==1);
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
 std::cout<<"PASS: source coverage, headroom, silent packets, overlap, monitor isolation and effective routing\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
