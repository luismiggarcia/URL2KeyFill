// Deterministic model of the SDI audio path: CEF packets arrive on the Mac clock and the
// DeckLink AudioCallback (src/output/DeckLink.cpp) consumes them on the device clock.
// The callback arithmetic is reproduced exactly; mixing uses the real AudioLane/AudioMixer.
//
// drift_sim [--ppm P] [--margin MS] [--seconds S] [--callback-ms MS] [--fps F] [--trace FILE] [--reports N]
//   --ppm       device clock error against the Mac clock (positive = device faster)
//   --margin    delta between AudioCallback construction (epoch_) and the device starting
//               to consume audio; the SDI read then ends at "now - margin"
//   --trace     per-packet arrival lateness taken from an analyze_trace.py-compatible trace
//               (cycled); otherwise a constant 1.4 ms, the median of the first audit trace
#include "core/Scene.hpp"
#include "core/AudioSchedule.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
using namespace u2k;

static std::vector<double> lateness(const char* path){
    std::vector<double> v;std::ifstream in(path);std::string line;
    while(std::getline(in,line)){
        std::istringstream s(line);std::vector<std::string> c;for(std::string x;s>>x;)c.push_back(x);
        if(c.size()==9)v.push_back(std::stoll(c[5])/1000.-std::stoll(c[2]));
        else if(c.size()==6)v.push_back(std::stoll(c[1])/1000.-std::stoll(c[5]));
    }
    return v;
}
int main(int argc,char** argv){
    double ppm=0,margin=3,seconds=900,callbackMs=9.7,fps=25;const char* trace=nullptr;int reports=10;
    for(int i=1;i+1<argc;i+=2){
        if(!std::strcmp(argv[i],"--ppm"))ppm=std::atof(argv[i+1]);
        else if(!std::strcmp(argv[i],"--margin"))margin=std::atof(argv[i+1]);
        else if(!std::strcmp(argv[i],"--seconds"))seconds=std::atof(argv[i+1]);
        else if(!std::strcmp(argv[i],"--callback-ms"))callbackMs=std::atof(argv[i+1]);
        else if(!std::strcmp(argv[i],"--fps"))fps=std::atof(argv[i+1]);
        else if(!std::strcmp(argv[i],"--trace"))trace=argv[i+1];
        else if(!std::strcmp(argv[i],"--reports"))reports=std::atoi(argv[i+1]);
        else {std::fprintf(stderr,"Unknown option %s\n",argv[i]);return 2;}
    }
    std::vector<double> late=trace?lateness(trace):std::vector<double>{1.4};
    if(late.empty()){std::fprintf(stderr,"No packets in trace\n");return 2;}
    auto lane=std::make_shared<AudioLane>();AudioMixer mixer;mixer.inputs({{lane,true,1}});
    std::vector<float> l(480,.5f),r(480,.5f);const float* data[]={l.data(),r.data()};
    const int64_t base=1'800'000'000'000;             // epoch_ in ms; packets start 1 s earlier
    // DeckLink.cpp: three video frames, clamped to 30-250 ms.
    const int64_t target=std::clamp<int64_t>(int64_t(std::ceil(3*48000/fps)),1440,12000);
    const int64_t epoch=base*48;int64_t packet=0;AudioSchedule schedule(epoch-target,uint32_t(target));
    std::printf("device %+g ppm, margin %g ms, callback %g ms, target %lld samples, lateness %s\n",ppm,margin,callbackMs,(long long)target,trace?trace:"1.4 ms");
    std::printf("%10s %12s %16s %16s\n","time (s)","missing %","min headroom ms","max headroom ms");
    uint64_t missing=0,total=0;double minHead=1e9,maxHead=-1e9,next=seconds/reports;
    auto arrival=[&](int64_t k){return double(base-1000+k*10)+late[size_t(k)%late.size()];};
    for(double t=0;t<=seconds;t+=callbackMs/1000){
        const double now=base+margin+t*1000;      // Mac clock, ms
        while(arrival(packet)<=now){lane->push(data,480,2,base-1000+packet*10);++packet;}
        const int64_t played=int64_t(t*48000*(1+ppm*1e-6));
        schedule.catchUp(played);              // Shared DeckLink scheduling arithmetic
        int64_t buffered=schedule.cursor()-played;
        for(int block=0;buffered<target&&block<24;block++){
            const uint32_t count=schedule.blockSize(uint32_t(buffered));
            const auto pcm=mixer.mix(schedule.readStart(),count);
            for(size_t i=0;i<pcm.size();i+=2){++total;missing+=pcm[i]==0;}
            schedule.advance(count);buffered+=count;
        }
        // Proposed diagnostic: newest received sample minus the end of the request.
        int64_t newestEnd=(base-1000+(packet-1)*10)*48+480;
        double head=(newestEnd-schedule.readStart())/48.;
        minHead=std::min(minHead,head);maxHead=std::max(maxHead,head);
        if(t>=next||t+callbackMs/1000>seconds){
            std::printf("%10.0f %12.3f %16.2f %16.2f\n",t,total?100.*missing/total:0,minHead,maxHead);
            missing=total=0;minHead=1e9;maxHead=-1e9;next+=seconds/reports;
        }
    }
}
