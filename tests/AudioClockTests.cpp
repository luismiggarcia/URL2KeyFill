#include "core/AudioClock.hpp"
#include "core/AudioResampler.hpp"
#include "core/MediaClock.hpp"
#include "core/Scene.hpp"
#include <complex>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <thread>
#include <vector>
#include <chrono>
using namespace u2k;
#define REQUIRE(x) do{if(!(x))throw std::runtime_error(#x);}while(0)
int main(){try{
 for(int ageMs:{60,50,40})for(double ppm:{0.,.42,-.42,1.,-1.,20.,-20.,100.,-100.,999.}){
  AudioDriftServo servo;const int64_t epoch=6'000'000'000LL,target=5760,age=ageMs*48;servo.reset(epoch,0,epoch-target-age);
  double device=0,maxPhase=0,minCoverage=1e30,lastRate=1;int64_t lastRateHost=epoch;bool out=false;
  for(int k=1;k<=57600;++k){device+=24000*(1+ppm*1e-6);const auto host=epoch+int64_t(k)*24000;const auto consumed=int64_t(std::llround(device));const double jitter=std::sin(k*.73)*8;const auto observed=host+int64_t(jitter);
   const bool ok=servo.observe(observed,consumed,consumed+target,target,age,k%997==0?96:0);
   if(!ok){out=true;break;}
   if(servo.mapping.ratio!=lastRate){REQUIRE(std::abs(servo.mapping.ratio-lastRate)<=5e-6*double(observed-lastRateHost)/48000+1e-12);lastRateHost=observed;lastRate=servo.mapping.ratio;}
   if(k>240){const double late=k%107==0?30.7:1.6;const double available=std::floor((double(host)-late*48)/480)*480;const double needed=servo.mapping.at(consumed+target-1024)+1024*servo.mapping.ratio+AudioResampler::right;minCoverage=std::min(minCoverage,available-needed);}
   if(k>240)maxPhase=std::max(maxPhase,std::abs(servo.mapping.at(consumed)-double(host-target-age)));
  }
  if(ppm==999){REQUIRE(out);continue;}REQUIRE(!out);REQUIRE(maxPhase<100);REQUIRE((minCoverage>0)==(ageMs!=40));REQUIRE(std::abs((servo.estimatedRatio-1)*1e6+ppm)<.1);std::cout<<"8h margin ms="<<ageMs<<" ppm="<<ppm<<" max phase ms="<<maxPhase/48<<" min coverage ms="<<minCoverage/48<<" ratio ppm="<<(servo.mapping.ratio-1)*1e6<<'\n';
 }
 {AudioDriftServo s;const int64_t epoch=6'000'000'000LL;const int target=5760,age=2880;s.reset(epoch,0,epoch-target-age);double device=0,maxPhase=0;
  for(int k=1;k<=57600;++k){const double ppm=100*std::sin(k*2*std::numbers::pi/28800);device+=24000*(1+ppm*1e-6);const auto host=epoch+int64_t(k)*24000,consumed=int64_t(std::llround(device));REQUIRE(s.observe(host,consumed,consumed+target,target,age,0));if(k>240)maxPhase=std::max(maxPhase,std::abs(s.mapping.at(consumed)-double(host-target-age)));}REQUIRE(maxPhase<120);std::cout<<"8h changing drift max phase ms="<<maxPhase/48<<'\n';}
 {AudioMapping m{1000,1,0};const auto before=m.at(5678);m.rateAt(5678,1.0001);REQUIRE(m.at(5678)==before);REQUIRE(std::abs(m.at(10678)-before-5000.5)<1e-8);
  PublishedAudioMapping published;published.publish({0,1,0});std::atomic<bool> done{false},bad{false};std::thread writer([&]{for(int i=0;i<100000;++i)published.publish({double(i),1.,i});done=true;});while(!done){if(auto a=published.read())if(a->source!=double(a->stream))bad=true;}writer.join();REQUIRE(!bad);}
 {AudioDriftServo s;s.reset(1000000,0,991360);REQUIRE(s.observe(1024000,24000,29760,5760,2880,96));REQUIRE(!s.observe(1500001,500001,505761,5760,2880,96));}
 {RecoveryBudget b;REQUIRE(b.allow(0));REQUIRE(b.allow(1));REQUIRE(!b.allow(2)); // Third inside the window fails.
  RecoveryBudget spread;const auto w=RecoveryBudget::window;REQUIRE(spread.allow(0));REQUIRE(spread.allow(w/2));
  REQUIRE(spread.allow(w+1));REQUIRE(!spread.allow(w+2)); // Old incidents expire; recent ones still count.
  RecoveryBudget daily;for(int day=0;day<30;++day)REQUIRE(daily.allow(int64_t(day)*24*3600*48000));} // Isolated incidents never stop output.
 {const int64_t now=int64_t(86400)*48000; // One day of uptime, in samples.
  REQUIRE(cefPtsInUptimeDomain(now/48,now));REQUIRE(cefPtsInUptimeDomain(now/48-22,now)); // Measured ~21.5 ms capture age.
  REQUIRE(!cefPtsInUptimeDomain(now/48-2000,now));REQUIRE(!cefPtsInUptimeDomain(now/48+2000,now));
  REQUIRE(!cefPtsInUptimeDomain(1'793'000'000'000,now)); // Unix Epoch milliseconds, as the CEF header documents.
  REQUIRE(!cefPtsInUptimeDomain(-1,now));REQUIRE(!cefPtsInUptimeDomain(INT64_MAX/40,now));}
 AudioResampler filter;
 double ripple=0;for(double phase:{0.,.125,.5,.9999})for(int hz=0;hz<=18000;hz+=100){std::complex<double> h=0;for(int tap=0;tap<AudioResampler::taps;++tap)h+=filter.coefficient(tap,phase)*std::exp(std::complex<double>(0,-2*std::numbers::pi*hz/48000*(tap-AudioResampler::left-phase)));ripple=std::max(ripple,std::abs(20*std::log10(std::abs(h))));}REQUIRE(ripple<.1);std::cout<<"passband max error dB="<<ripple<<'\n';
 for(double ratio:{.9999,1.,1.0001})for(int hz:{1000,18000}){
  constexpr size_t n=48000;const double offset=80.371;std::vector<float> in((n+400)*2),out(n*2);
  for(size_t i=0;i<in.size()/2;++i)in[i*2]=in[i*2+1]=float(.5*std::sin(2*std::numbers::pi*hz*i/48000));
  filter.render(in.data(),in.size()/2,offset,ratio,out.data(),n);double error=0,power=0;
  for(size_t i=0;i<n;++i){const double ideal=.5*std::sin(2*std::numbers::pi*hz*(offset+i*ratio)/48000);error+=std::pow(out[i*2]-ideal,2);power+=ideal*ideal;REQUIRE(out[i*2]==out[i*2+1]);}
  const double residual=10*std::log10(error/power);REQUIRE(residual<-80);std::cout<<"sinc tone="<<hz<<" ratio="<<ratio<<" residual dB="<<residual<<'\n';
  std::vector<float> split(2048);filter.render(in.data(),in.size()/2,offset,ratio,split.data(),137);filter.render(in.data(),in.size()/2,offset+137*ratio,ratio,split.data()+274,1024-137);for(size_t i=0;i<2048;++i)REQUIRE(std::abs(split[i]-out[i])<1e-6);
 }
 double stop=0;for(double phase:{0.,.125,.5,.9999})for(int hz=23800;hz<=24000;hz+=10){std::complex<double> h=0;for(int tap=0;tap<AudioResampler::taps;++tap)h+=filter.coefficient(tap,phase)*std::exp(std::complex<double>(0,-2*std::numbers::pi*hz/48000*(tap-AudioResampler::left-phase)));stop=std::max(stop,std::abs(h));}REQUIRE(20*std::log10(stop)<-80);std::cout<<"stopband 23.8-24kHz dB="<<20*std::log10(stop)<<'\n';
 {auto lane=std::make_shared<AudioLane>();AudioMixer mix;mix.inputs({{lane,true,1}});std::vector<float> packet(480);for(int p=0;p<40;++p){for(int j=0;j<480;++j)packet[j]=float(.5*std::sin(2*std::numbers::pi*(p*480+j)/48));const float* data[]={packet.data(),packet.data()};lane->push(data,480,2,1000+p*10);}const double start=57600.37;auto pcm=mix.mixResampled(start,1024,1.0001,filter);REQUIRE(lane->missingFrames==0);REQUIRE(lane->outputReads>0);for(int i=0;i<1024;++i){const double expected=.5*std::sin(2*std::numbers::pi*(start-48000+i*1.0001)/48);REQUIRE(std::abs(double(pcm[i*2])/2147483647.-expected)<1e-5);}lane->clear();const float* data[]={packet.data(),packet.data()};lane->push(data,480,2,1000);lane->push(data,480,2,1020);mix.mixResampled(48480,100,1,filter);REQUIRE(lane->missingFrames>0);}
 {auto lane=std::make_shared<AudioLane>();AudioMixer mix;mix.inputs({{lane,true,1}});std::vector<float> zero(480,0);const float* data[]={zero.data(),zero.data()};for(int p=0;p<30;++p)lane->push(data,480,2,1000+p*10);auto pcm=mix.mixResampled(52800.5,1024,.9999,filter);REQUIRE(std::all_of(pcm.begin(),pcm.end(),[](int32_t v){return v==0;}));REQUIRE(lane->outputReads>0);REQUIRE(lane->missingFrames==0);REQUIRE(mix.nonSilent==0);lane->clear();lane->push(data,480,2,1000);lane->push(data,480,2,1020);mix.mixResampled(48480,100,1,filter);REQUIRE(lane->missingFrames>0);}
 std::vector<float> in(2400),out(2048);const auto start=std::chrono::steady_clock::now();for(int k=0;k<100;++k)filter.render(in.data(),1200,80.1,1.0001,out.data(),1024);std::cout<<"filter mean ms/1024="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/100<<'\n';
 return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
