#pragma once
#include "Core.hpp"
#include <mutex>
#include <deque>
#include <thread>
#include <condition_variable>
namespace u2k {
struct AudioLevels {float rmsLeft=0,rmsRight=0,peakLeft=0,peakRight=0;};
struct AudioPacket {int64_t start=0;std::vector<float> samples;bool expectedForOutput=false;};
class AudioLane {
    std::mutex mutex_;std::deque<AudioPacket> packets_,meterPackets_;
    std::atomic<float> peakLeft_{0},peakRight_{0};
    void sum(int64_t,size_t,float,std::vector<float>&,bool prune,bool mono,bool left=true,bool right=true);
public:
    std::atomic<uint64_t> received{0},discarded{0},nonZero{0},unusedExpired{0},outputOverruns{0};
    std::atomic<bool> outputExpected{false};
    std::atomic<int64_t> latestTimestamp{0};
    void push(const float* const*,int frames,int channels,int64_t ptsMilliseconds);
    std::pair<float,float> takePeaks();
    AudioLevels sourceLevels(int64_t start,size_t frames,bool mono=false,bool left=true,bool right=true);
    void clear();
    void mix(int64_t start,size_t frames,float gain,std::vector<float>&,bool mono=false,bool left=true,bool right=true);
    void sample(int64_t start,size_t frames,float gain,std::vector<float>&,bool mono=false,bool left=true,bool right=true);
};
struct AudioInput {std::shared_ptr<AudioLane> lane;bool enabled=true;float gain=1;bool mono=false;bool left=true,right=true;};
class AudioMixer {
    std::mutex mutex_;std::vector<AudioInput> inputs_;
public:
    std::atomic<bool> enabled{true},muted{false};
    std::atomic<float> gain{1};
    std::atomic<bool> mono{false},left{true},right{true};
    std::vector<float> sample(int64_t start,size_t frames,bool respectMute=true);
    std::atomic<uint64_t> clipped{0},nonSilent{0};
    std::atomic<int64_t> lastMixTimestamp{0};
    void inputs(std::vector<AudioInput>);
    std::vector<int32_t> mix(int64_t start,size_t frames);
    std::pair<float,float> sourcePeaks(int64_t start,size_t frames);
    AudioLevels sourceLevels(int64_t start,size_t frames);
};
struct ImageInput {std::shared_ptr<LatestFrame> frames;bool visible=true;};
std::shared_ptr<const Frame> composite(const std::vector<std::shared_ptr<const Frame>>&,Size);
class Scene {
    std::mutex mutex_;std::condition_variable cv_;bool quit_=false;uint64_t revision_=0;
    Size viewport_,target_;std::vector<ImageInput> layers_;std::thread worker_;
    void run();
public:
    LatestFrame frames;
    std::atomic<uint64_t> composed{0};
    Scene();~Scene();
    void configure(std::vector<ImageInput>,Size viewport,Size target);
};
}
