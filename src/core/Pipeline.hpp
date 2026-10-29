#pragma once
#include "Core.hpp"
#include <condition_variable>
#include <mutex>
#include <thread>
namespace u2k {
class Pipeline {
    LatestFrame raw_, fitted_;
    std::mutex mutex_;
    std::condition_variable cv_;
    Size target_;
    bool quit_=false, dirty_=false;
    std::thread worker_;
    std::atomic<uint64_t> received_{0}, processed_{0};
    void run();
public:
    Pipeline();
    ~Pipeline();
    void submit(std::shared_ptr<const Frame>);
    void target(Size);
    const LatestFrame& frames() const {return fitted_;}
    void clear();
    uint64_t received()const{return received_;}
    uint64_t processed()const{return processed_;}
};
}
