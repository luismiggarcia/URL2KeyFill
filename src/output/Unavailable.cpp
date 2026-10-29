#include "Output.hpp"
#include <stdexcept>
namespace u2k {
// Explicit build without the proprietary SDK. No synthetic devices or playback.
class Unavailable final:public Output {
public:
 std::vector<Device> enumerate() override{return {};}
 void start(const std::string&,const Mode&,const LatestFrame&)override{throw std::runtime_error("DeckLink backend was not built: SDK 16.0 is missing.");}
 void onAir(bool)override{}
 void audio(std::shared_ptr<AudioMixer>)override{}
 void stop()override{}
 OutputStatus status()const override{return {false,false,0,0,0,"Preview only — DeckLink backend not built."};}
};
std::unique_ptr<Output> makeOutput(){return std::make_unique<Unavailable>();}
}
