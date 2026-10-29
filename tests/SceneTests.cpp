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
 auto frames=std::make_shared<LatestFrame>();frames->publish(front);Scene scene;scene.configure({{frames,true}},{1,1},{4,2});for(int i=0;i<100&&!scene.frames.get();i++)std::this_thread::sleep_for(std::chrono::milliseconds(2));auto image=scene.frames.get();REQUIRE(image);REQUIRE(image->pixels[3]==0);REQUIRE(image->pixels[7]==128);scene.configure({{frames,false}},{1,1},{4,2});for(int i=0;i<100;i++){auto current=scene.frames.get();if(current&&current->pixels[7]==0)break;std::this_thread::sleep_for(std::chrono::milliseconds(2));}REQUIRE(scene.frames.get()->pixels[7]==0);
 std::cout<<"PASS: composition order, premultiplied alpha, transparent fit, visibility, stereo mixing, gain, limiting, mute, bounded queues\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
