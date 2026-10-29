#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

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
bool allowedUrl(const std::string&);
// Owned, top-left BGRA8, premultiplied sRGB. Never retain a CEF callback pointer.
struct Frame {
    Size size;
    size_t stride;
    uint64_t sequence=0;
    std::vector<uint8_t> pixels;
    explicit Frame(Size s, uint64_t sequence=0);
    static std::shared_ptr<const Frame> copy(Size, const void*, size_t stride, uint64_t sequence);
};
class LatestFrame {
    std::shared_ptr<const Frame> frame_;
public:
    void publish(std::shared_ptr<const Frame> f) {std::atomic_store(&frame_, std::move(f));}
    std::shared_ptr<const Frame> get() const {return std::atomic_load(&frame_);}
    void clear(){publish(nullptr);}
};
// Bilinear resampling on premultiplied pixels; transparent letterbox.
std::shared_ptr<const Frame> resize(const Frame&, Size);
// SDR Rec.709 encoded, straight RGB. Alpha stays full-range coverage.
// Input sRGB -> unpremultiply -> linear -> BT.709 OETF (straight alpha for SDK).
void toSDR(const Frame&, uint8_t* destination, size_t stride, bool onAir, bool argb=false);
}
