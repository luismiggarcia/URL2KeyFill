#pragma once
#include "core/Core.hpp"
#include "core/Scene.hpp"
#include "core/AudioClock.hpp"
namespace u2k {
struct Mode {uint32_t id=0,pixelFormat=0;std::string name;Size size;Rate rate;bool keying=false,fillOnlySupported=true;};
struct Device {std::string id,name;std::vector<Mode> modes;};
struct OutputStatus {bool running=false,onAir=false;uint64_t completed=0,late=0,dropped=0;std::string message;uint64_t audioFrames=0,audioErrors=0,audioRecoveries=0;uint32_t audioBuffered=0,audioTarget=0,audioSourceAge=0;int64_t audioReadAge=INT64_MIN;bool audioClockCalibrated=false;int64_t audioStartupShift=0,audioStreamQueuePhase=0;uint32_t prerollAudio=0,prerollVideo=0;int64_t firstDroppedFrame=-1;uint32_t startupSyncFrames=0;int64_t startupSyncSamples=0;uint32_t startupLeadFrames=0,startupAudioPadding=0;uint64_t videoHistoryReads=0,videoHistoryMisses=0;int64_t videoSelectionAge=INT64_MIN;size_t videoHistoryBytes=0;int64_t audioSourceOrigin=0,scheduledFlashPts=INT64_MIN;uint64_t scheduledFlashCount=0,videoHistoryStartupMisses=0,videoPreparedReads=0,videoConversionFallbacks=0;bool videoWorkerQos=false;double audioRatePpm=0,audioEstimatedRatePpm=0,audioPhaseError=0;double audioRateUncertaintyPpm=0;bool audioDriftReady=false;uint64_t audioDriftRejected=0;AudioMapping audioMapping;uint32_t reserveFrames=3;int32_t syncOffsetSamples=0;};
class Output {
public:
    virtual ~Output()=default;
    virtual std::vector<Device> enumerate()=0;
    virtual void start(const std::string& device,const Mode&,const LatestFrame&)=0;
    virtual void onAir(bool)=0;
    virtual void audio(std::shared_ptr<AudioMixer>)=0;
    virtual void audioSourceAge(uint32_t samples)=0;
    virtual void syncAdjustment(int32_t){}
    virtual void timing(uint32_t,int32_t){}
    virtual void syncDiagnostic(bool){}
    virtual void referenceDiagnostic(bool){}
    virtual void stop()=0;
    virtual OutputStatus status() const=0;
};
std::unique_ptr<Output> makeOutput();
}
