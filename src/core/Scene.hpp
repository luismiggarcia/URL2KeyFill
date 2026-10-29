#pragma once
#include "Core.hpp"
#include <mutex>
#include <deque>
#include <thread>
#include <condition_variable>
namespace u2k {
struct AudioPacket {int64_t start=0;std::vector<float> samples;};
class AudioLane {
    std::mutex mutex_;std::deque<AudioPacket> packets_;
public:
    std::atomic<uint64_t> received{0},discarded{0},nonZero{0};
    std::atomic<int64_t> latestTimestamp{0};
    void push(const float* const*,int frames,int channels,int64_t ptsMilliseconds);
    void clear();
    void mix(int64_t start,size_t frames,float gain,std::vector<float>&);
};
struct AudioInput {std::shared_ptr<AudioLane> lane;bool enabled=true;float gain=1;};
class AudioMixer {
    std::mutex mutex_;std::vector<AudioInput> inputs_;
public:
    std::atomic<bool> enabled{true};
    std::atomic<uint64_t> clipped{0},nonSilent{0};
    std::atomic<int64_t> lastMixTimestamp{0};
    void inputs(std::vector<AudioInput>);
    std::vector<int32_t> mix(int64_t start,size_t frames);
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
