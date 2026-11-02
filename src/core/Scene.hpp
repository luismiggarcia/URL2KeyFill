#pragma once
#include "Core.hpp"
#include "ReferenceTiming.hpp"
#include "AudioResampler.hpp"
#include <mutex>
#include <deque>
#include <thread>
#include <condition_variable>
namespace u2k {
struct AudioLevels {float rmsLeft=0,rmsRight=0,peakLeft=0,peakRight=0;};
struct AudioPacket {int64_t start=0;std::vector<float> samples;bool expectedForOutput=false;size_t offset=0;int64_t arrival=0,published=0;size_t frames()const{return samples.size()/2-offset;}};
// Latest missing read, retained across reporting intervals; sequence exposes replacement.
// Host fields are uptime in 48 kHz samples, not output presentation timestamps.
struct AudioGap {
    uint64_t sequence=0,generation=0,missing=0;
    int64_t start=0,end=0,readHost=0,newestEnd=0,newestArrival=0,newestPublished=0;
    int64_t recoveryArrival=0,recoveryPublished=0,recoveryStart=0,recoveryEnd=0;
};
struct AudioReadWindow {
    AudioGap gap;
    uint64_t reads=0,missing=0,noSource=0,headroomCount=0;
    int64_t headroomMin=INT64_MAX,headroomMax=INT64_MIN,headroomSum=0;
};
class AudioLane {
    std::mutex mutex_;std::deque<AudioPacket> packets_,meterPackets_;
    bool sourceSeen_=false;int64_t firstSample_=0;
    bool timelineReady_=false;int64_t nextSample_=0,lastRawStart_=0;int lastFrames_=0;
    AudioReadWindow readWindow_;AudioGap gap_;int syncQuiet_=4800;
    std::atomic<float> peakLeft_{0},peakRight_{0};
    void sum(int64_t,size_t,float,std::vector<float>&,bool prune,bool mono,bool left=true,bool right=true);
public:
    std::atomic<bool> referenceDiagnostic{false};ReferenceAudioClock referenceClock;
    std::atomic<bool> syncDiagnostic{false};std::atomic<int64_t> syncPulsePts{INT64_MIN};std::atomic<uint64_t> syncPulseCount{0};
    std::atomic<uint64_t> received{0},discarded{0},nonZero{0},unusedExpired{0},outputOverruns{0};
    std::atomic<bool> outputExpected{false};
    std::atomic<int64_t> latestTimestamp{0};
    std::atomic<uint64_t> generation{0},duplicatePackets{0},reorderedPackets{0},trimmedFrames{0},timestampAdjustments{0},timestampGapFrames{0},stalePackets{0};
    // SDI reads only: missing frames describe coverage, not signal amplitude.
    std::atomic<uint64_t> outputReads{0},missingFrames{0},noSourceReads{0},preSourcePaddingFrames{0};
    std::atomic<int64_t> sourceHeadroom{0};
    // Reporting only; called outside audio callbacks, resets the interval statistics.
    AudioReadWindow takeReadWindow();
    void push(const float* const*,int frames,int channels,int64_t ptsMilliseconds);
    std::pair<float,float> takePeaks();
    AudioLevels sourceLevels(int64_t start,size_t frames,bool mono=false,bool left=true,bool right=true);
    void clear();
    void mix(int64_t start,size_t frames,float gain,std::vector<float>&,bool mono=false,bool left=true,bool right=true);
    void sample(int64_t start,size_t frames,float gain,std::vector<float>&,bool mono=false,bool left=true,bool right=true);
};
struct AudioInput {std::shared_ptr<AudioLane> lane;bool enabled=true;float gain=1;bool mono=false;bool left=true,right=true;};
inline bool audioRouteEnabled(bool sourceMono,bool sourceLeft,bool sourceRight,bool outputMono,bool outputLeft,bool outputRight){
    if(sourceMono||outputMono)return (sourceLeft||sourceRight)&&(outputLeft||outputRight);
    return (sourceLeft&&outputLeft)||(sourceRight&&outputRight);
}
class AudioMixer {
    std::mutex mutex_;std::vector<AudioInput> inputs_;
public:
    std::atomic<bool> enabled{true},muted{false};
    std::atomic<float> gain{1};
    std::atomic<bool> mono{false},left{true},right{true};
    std::vector<float> sample(int64_t start,size_t frames,bool respectMute=true,bool applyMaster=true);
    // Program PFL: preserve every layer option, bypass all master controls.
    std::vector<float> samplePreMaster(int64_t start,size_t frames){return sample(start,frames,false,false);}
    std::atomic<uint64_t> clipped{0},nonSilent{0};
    std::atomic<int64_t> lastMixTimestamp{0};
    void inputs(std::vector<AudioInput>);
    std::vector<int32_t> mix(int64_t start,size_t frames,bool programActive=true);
    std::vector<float> mixFloat(int64_t start,size_t frames,bool programActive=true);
    std::vector<int32_t> mixResampled(double start,size_t frames,double ratio,const AudioResampler&,bool programActive=true);
    std::vector<int32_t> toPCM(const std::vector<float>&);
    std::pair<float,float> sourcePeaks(int64_t start,size_t frames);
    AudioLevels sourceLevels(int64_t start,size_t frames);
};
struct ImageInput {std::shared_ptr<LatestFrame> frames;bool visible=true;bool operator==(const ImageInput&)const=default;};
std::shared_ptr<const Frame> composite(const std::vector<std::shared_ptr<const Frame>>&,Size);
class Scene {
    std::mutex mutex_;std::shared_ptr<FrameSignal> signal_=std::make_shared<FrameSignal>();bool quit_=false;uint64_t revision_=0;
    Size viewport_,target_;std::vector<ImageInput> layers_;std::thread worker_;
    void run();
public:
    LatestFrame frames;
    std::atomic<uint64_t> composed{0};
    Scene();~Scene();
    void configure(std::vector<ImageInput>,Size viewport,Size target);
};
}
