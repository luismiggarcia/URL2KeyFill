#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <deque>
#include <optional>
#include <mutex>
#include <condition_variable>

namespace u2k {
struct Size { int width=1920, height=1080; bool operator==(const Size& b) const {return width==b.width&&height==b.height;} };
struct Rect { int x,y,width,height; };
void validate(Size);
Rect fit(Size source, Size destination);
struct Rate {
    int64_t duration=1001, scale=30000;
    void validate() const;
    int64_t timestamp(uint64_t frame) const;
    std::string label() const;
};
struct Settings {
    std::string url="u2k://test/index.html", device;
    Size viewport;
    uint32_t mode=0;
    bool matchOutput=true;
    Rate rate;
    void validate() const;
    void save(const std::filesystem::path&) const;
    static Settings load(const std::filesystem::path&);
};
std::string canonicalBuiltinUrl(std::string);
bool allowedUrl(const std::string&);
// Owned, top-left BGRA8, premultiplied sRGB. Never retain a CEF callback pointer.
struct Frame {
    Size size;
    size_t stride;
    uint64_t sequence=0;
    int64_t contentTime=INT64_MIN; // Experimental identified-reference PTS in the audio source domain; unset normally.
    int64_t paintTime=INT64_MIN; // Uptime samples at paint arrival, not a decoded content PTS.
    std::vector<uint8_t> pixels;
    explicit Frame(Size s, uint64_t sequence=0);
    static std::shared_ptr<const Frame> copy(Size, const void*, size_t stride, uint64_t sequence, int64_t paintTime=INT64_MIN, int64_t contentTime=INT64_MIN);
};
struct FrameSignal {
    std::mutex mutex;std::condition_variable cv;uint64_t revision=1;
    void notify(){ {std::lock_guard<std::mutex> lock(mutex);++revision;}cv.notify_one();}
};
struct TimedFrame {std::shared_ptr<const Frame> frame;int64_t time=INT64_MIN;std::shared_ptr<const std::vector<uint8_t>> prepared;};
class FrameHistory {
    mutable std::mutex mutex_;std::deque<TimedFrame> frames_;size_t bytes_=0,maxFrames_=0,maxBytes_=0;int64_t maxSpan_=24000;uint64_t generation_=0;std::optional<bool> argb_;
    void trim();
public:
    void configure(size_t frames,size_t bytes,int64_t span=24000);
    void retainFrames(size_t frames);
    void prepareOutput(std::optional<bool> argb,size_t budget);
    void publish(std::shared_ptr<const Frame>,int64_t time);
    TimedFrame at(int64_t time,int64_t maxAhead=0)const;
    // Entries strictly between two selection times: frames an output cadence skipped.
    size_t countBetween(int64_t after,int64_t before)const;
    void clear();
    size_t bytes()const;
};
class LatestFrame {
    std::shared_ptr<const Frame> frame_;
    std::mutex observersMutex_;
    std::vector<std::weak_ptr<FrameSignal>> observers_;
public:
    mutable FrameHistory history;
    void publish(std::shared_ptr<const Frame> f,int64_t time=INT64_MIN) {
        history.publish(f,time==INT64_MIN&&f?f->paintTime:time);
        std::atomic_store(&frame_, std::move(f));
        std::lock_guard<std::mutex> lock(observersMutex_);
        for(auto it=observers_.begin();it!=observers_.end();){if(auto signal=it->lock()){signal->notify();++it;}else it=observers_.erase(it);}
    }
    std::shared_ptr<const Frame> get() const {return std::atomic_load(&frame_);}
    void clear(){publish(nullptr);}
    void subscribe(const std::shared_ptr<FrameSignal>& signal){
        std::lock_guard<std::mutex> lock(observersMutex_);
        for(const auto& weak:observers_)if(weak.lock()==signal)return;
        observers_.push_back(signal);
    }
    void unsubscribe(const std::shared_ptr<FrameSignal>& signal){
        std::lock_guard<std::mutex> lock(observersMutex_);
        for(auto it=observers_.begin();it!=observers_.end();)if(auto held=it->lock();!held||held==signal)it=observers_.erase(it);else ++it;
    }
};
// Bilinear resampling on premultiplied pixels; transparent letterbox.
std::shared_ptr<const Frame> resize(const Frame&, Size);
// SDR Rec.709 encoded, straight RGB. Alpha stays full-range coverage.
// Input sRGB -> unpremultiply -> linear -> BT.709 OETF (straight alpha for SDK).
void toSDR(const Frame&, uint8_t* destination, size_t stride, bool onAir, bool argb=false);
}
