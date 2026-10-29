#pragma once
#include "core/Core.hpp"
#include "core/Scene.hpp"
namespace u2k {
struct Mode {uint32_t id=0,pixelFormat=0;std::string name;Size size;Rate rate;bool keying=false,fillOnlySupported=true;};
struct Device {std::string id,name;std::vector<Mode> modes;};
struct OutputStatus {bool running=false,onAir=false;uint64_t completed=0,late=0,dropped=0;std::string message;uint64_t audioFrames=0,audioErrors=0;};
class Output {
public:
    virtual ~Output()=default;
    virtual std::vector<Device> enumerate()=0;
    virtual void start(const std::string& device,const Mode&,const LatestFrame&)=0;
    virtual void onAir(bool)=0;
    virtual void audio(std::shared_ptr<AudioMixer>)=0;
    virtual void stop()=0;
    virtual OutputStatus status() const=0;
};
std::unique_ptr<Output> makeOutput();
}
