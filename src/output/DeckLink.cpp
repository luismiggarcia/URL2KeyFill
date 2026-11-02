#include "Output.hpp"
#include "core/AudioSchedule.hpp"
#include "core/MediaClock.hpp"
#include "core/SyncDiagnostics.hpp"
#include "core/AVAdjustment.hpp"
#include "DeckLinkAPI.h"
#include <CoreFoundation/CoreFoundation.h>
#include <algorithm>
#include <array>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <pthread/qos.h>

namespace u2k {
namespace {
template<class T> class Com {
    T* p_=nullptr;
public:
    Com()=default;explicit Com(T* p):p_(p){}
    ~Com(){if(p_)p_->Release();}
    Com(const Com&)=delete;Com& operator=(const Com&)=delete;
    Com(Com&& x)noexcept:p_(x.p_){x.p_=nullptr;}
    Com& operator=(Com&& x)noexcept{if(this!=&x){if(p_)p_->Release();p_=x.p_;x.p_=nullptr;}return *this;}
    T* get()const{return p_;}T* operator->()const{return p_;}explicit operator bool()const{return p_;}
    T** put(){if(p_)p_->Release();p_=nullptr;return &p_;}
};
void check(HRESULT r,const char* what){if(r!=S_OK)throw std::runtime_error(std::string(what)+" (HRESULT "+std::to_string(r)+"). Check connection, profile and whether another application is using the device.");}
std::string str(CFStringRef s){if(!s)return {};char buffer[2048]={};CFStringGetCString(s,buffer,sizeof(buffer),kCFStringEncodingUTF8);CFRelease(s);return buffer;}
std::string identity(IDeckLink* d){Com<IDeckLinkProfileAttributes> a;int64_t id=0;if(d->QueryInterface(IID_IDeckLinkProfileAttributes,reinterpret_cast<void**>(a.put()))==S_OK&&a->GetInt(BMDDeckLinkPersistentID,&id)==S_OK)return std::to_string(id);return {};}
struct CompletionState {
    std::mutex mutex;std::condition_variable cv;
    std::deque<IDeckLinkVideoFrame*> free;
    std::atomic<uint64_t> completed{0},late{0},dropped{0};
    std::atomic<bool> accepting{false},workerQos{false};
    static constexpr uint32_t startupLeadFrames=1;
    std::array<std::atomic<IDeckLinkVideoFrame*>,3> slots{};std::array<std::atomic<int64_t>,3> scheduled{};
    std::atomic<int64_t> firstDroppedFrame{-1},warmupSamples{0};std::atomic<uint32_t> warmupFrames{0};
};
class Callback final:public IDeckLinkVideoOutputCallback {
    std::atomic<ULONG> refs_{1};std::shared_ptr<CompletionState> state_;
public:
    explicit Callback(std::shared_ptr<CompletionState> s):state_(std::move(s)){}
    HRESULT QueryInterface(REFIID iid,LPVOID* pp)override {
        if(!pp)return E_INVALIDARG;*pp=nullptr;
        const auto unknown=CFUUIDGetUUIDBytes(IUnknownUUID);
        if(!std::memcmp(&iid,&unknown,sizeof(iid))||!std::memcmp(&iid,&IID_IDeckLinkVideoOutputCallback,sizeof(iid))){*pp=static_cast<IDeckLinkVideoOutputCallback*>(this);AddRef();return S_OK;}return E_NOINTERFACE;
    }
    ULONG AddRef()override{return ++refs_;}
    ULONG Release()override{auto n=--refs_;if(!n)delete this;return n;}
    HRESULT ScheduledFrameCompleted(IDeckLinkVideoFrame* f,BMDOutputFrameCompletionResult r)override{
        if(r==bmdOutputFrameDisplayedLate)++state_->late;
        if(r==bmdOutputFrameDropped){++state_->dropped;for(size_t i=0;i<state_->slots.size();++i)if(state_->slots[i]==f){int64_t expected=-1;state_->firstDroppedFrame.compare_exchange_strong(expected,state_->scheduled[i].load());}}
        if(r!=bmdOutputFrameFlushed)++state_->completed;
        if(state_->accepting&&r!=bmdOutputFrameFlushed){std::lock_guard<std::mutex> l(state_->mutex);if(state_->free.size()<3)state_->free.push_back(f);state_->cv.notify_one();}return S_OK;
    }
    HRESULT ScheduledPlaybackHasStopped()override{state_->cv.notify_all();return S_OK;}
};
class AudioCallback final:public IDeckLinkAudioOutputCallback {
 std::atomic<ULONG> refs_{1};IDeckLinkOutput* output_;std::shared_ptr<AudioMixer> mixer_;std::mutex mutex_;std::condition_variable startCv_;
 int64_t playbackScale_;uint32_t startupPadding_,reserve_;AVTiming timing_;AVDelay delay_,oldDelay_;std::shared_ptr<std::atomic<int32_t>> adjustment_;AudioMapping oldMapping_;
 AudioDriftServo servo_;AudioResampler resampler_;uint32_t driftResets_=0;
 std::unique_ptr<SyncAudioTrace> syncTrace_;
 std::unique_ptr<SyncEventBuffer<SyncClockEvent>> clockTrace_;int64_t lastClockTrace_=INT64_MIN;
 AudioSchedule schedule_;ClockPhaseAcquisition phase_;bool accepting_=true;uint32_t sourceAge_,fadePosition_=240;
public:
 std::atomic<uint64_t> frames{0},errors{0},recoveries{0};std::atomic<bool> failed{false};
 std::atomic<bool> programActive{true};std::atomic<uint32_t> bufferedFrames{0};
 std::atomic<int64_t> readAge{INT64_MIN};
 std::atomic<bool> clockCalibrated{false};std::atomic<int64_t> startupShift{0};
 std::atomic<bool> playbackStarted{false};
 PublishedAudioMapping timeline;std::atomic<double> ratePpm{0},estimatedRatePpm{0},phaseError{0},rateUncertaintyPpm{0};std::atomic<bool> driftReady{false};std::atomic<uint64_t> driftRejected{0};
 std::atomic<int64_t> sourceOrigin{0};
 std::atomic<uint32_t> startupPaddingWritten{0};std::atomic<int64_t> streamQueuePhase{0};std::atomic<uint32_t> prerollAudio{0},prerollVideo{0};
 uint32_t targetFrames()const{return schedule_.target();}
 void enableSyncTrace(){syncTrace_=std::make_unique<SyncAudioTrace>();clockTrace_=std::make_unique<SyncEventBuffer<SyncClockEvent>>();}
 // Called after stop() has excluded the callback writer.
 void dumpSyncTrace(){if(!syncTrace_)return;
  for(const auto& e:syncTrace_->events)std::cerr<<std::fixed<<std::setprecision(9)<<"SYNC_AUDIO stream_samples="<<e.stream<<" host_samples="<<e.host<<" source_samples="<<e.source<<" mapping_source="<<e.mapping.source<<" mapping_stream="<<e.mapping.stream<<" ratio="<<e.mapping.ratio<<" delay="<<e.delay<<" transition="<<e.transition<<'\n';
  if(clockTrace_){for(const auto& e:clockTrace_->events)std::cerr<<std::fixed<<std::setprecision(12)<<"SYNC_CLOCK queue_before="<<e.queueBefore<<" queue_after="<<e.queueAfter<<" consumed="<<e.consumed<<" stream="<<e.stream<<" hardware_before="<<e.hardwareBefore<<" hardware_after="<<e.hardwareAfter<<" hardware="<<e.hardware<<" hardware_valid="<<e.hardwareValid<<" ratio="<<e.ratio<<" phase_error="<<e.phaseError<<'\n';std::cerr<<"SYNC_TRACE kind=clock stored="<<clockTrace_->events.size()<<" omitted="<<clockTrace_->omitted<<'\n';clockTrace_.reset();}
  std::cerr<<"SYNC_TRACE kind=audio stored="<<syncTrace_->events.size()<<" omitted="<<syncTrace_->omitted<<'\n';syncTrace_.reset();
 }
 AudioCallback(IDeckLinkOutput* o,std::shared_ptr<AudioMixer> m,const Mode& mode,uint32_t sourceAge,uint32_t reserve,std::shared_ptr<std::atomic<int32_t>> adjustment):output_(o),mixer_(std::move(m)),playbackScale_(mode.rate.scale),startupPadding_(uint32_t((mode.rate.duration*48000+mode.rate.scale-1)/mode.rate.scale)),reserve_(reserve),timing_(mode.rate),delay_(timing_.plan(adjustment->load())),adjustment_(std::move(adjustment)),sourceAge_(sourceAge){
  output_->AddRef();const auto target=timing_.audioTarget(reserve);schedule_=AudioSchedule::fromHostAnchor(mediaSampleTime()-startupPadding_,target,sourceAge);phase_.reset(startupPadding_+2*target);sourceOrigin=schedule_.sourceOrigin();servo_.reset(mediaSampleTime(),0,schedule_.sourceOrigin());timeline.publish(servo_.mapping);
 }
 ~AudioCallback(){output_->Release();}
 HRESULT QueryInterface(REFIID iid,LPVOID* p)override{if(!p)return E_INVALIDARG;*p=nullptr;const auto unknown=CFUUIDGetUUIDBytes(IUnknownUUID);if(!std::memcmp(&iid,&unknown,sizeof(iid))||!std::memcmp(&iid,&IID_IDeckLinkAudioOutputCallback,sizeof(iid))){*p=static_cast<IDeckLinkAudioOutputCallback*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
 ULONG AddRef()override{return ++refs_;}ULONG Release()override{auto n=--refs_;if(!n)delete this;return n;}
 bool waitForStart(){std::unique_lock<std::mutex> lock(mutex_);return startCv_.wait_for(lock,std::chrono::seconds(5),[this]{return playbackStarted||failed;})&&playbackStarted;}
 HRESULT fail(){++errors;failed=true;startCv_.notify_all();return E_FAIL;}
 void stop(){std::lock_guard<std::mutex> lock(mutex_);accepting_=false;startCv_.notify_all();}
 HRESULT RenderAudioSamples(bool preroll)override{
  std::lock_guard<std::mutex> lock(mutex_);if(!accepting_)return S_OK;
  uint32_t buffered=0;const auto queueBefore=mediaSampleTime();if(output_->GetBufferedAudioSampleFrameCount(&buffered)!=S_OK){return fail();}const auto queueAfter=mediaSampleTime();
  BMDTimeValue time=0;double speed=0;const bool haveTime=output_->GetScheduledStreamTime(48000,&time,&speed)==S_OK&&time>=0&&time<=queueAfter+48000;
  if(playbackStarted&&!haveTime){return fail();}
  const bool activeTime=playbackStarted&&haveTime&&!preroll&&speed>0;
  if(activeTime)streamQueuePhase=time-(schedule_.cursor()-int64_t(buffered));
  const bool recovered=activeTime&&buffered==0&&schedule_.recover(time,480);
  if(recovered){++recoveries;clockCalibrated=false;phase_.reset(schedule_.cursor()+schedule_.target());}
  const auto consumed=schedule_.cursor()-int64_t(buffered);const auto midpoint=queueBefore+(queueAfter-queueBefore)/2;
  bool driftReset=false;
  if(activeTime&&clockCalibrated&&!recovered&&!servo_.observe(midpoint,consumed,schedule_.cursor(),schedule_.target(),sourceAge_,queueAfter-queueBefore)){
   driftReset=true;++recoveries;if(++driftResets_>=3)return fail();
  }
  if(activeTime&&(recovered||driftReset||(!clockCalibrated&&phase_.observe(midpoint,consumed,queueAfter-queueBefore)))){
   oldMapping_=servo_.mapping;oldDelay_=delay_;startupShift=schedule_.calibrateQueue(midpoint,buffered,sourceAge_);fadePosition_=startupShift?0:240;sourceOrigin=schedule_.sourceOrigin();clockCalibrated=!recovered;servo_.reset(midpoint,consumed,schedule_.sourceOrigin());
  }
  const auto requested=adjustment_->load();if(requested!=delay_.signedSamples){oldMapping_=servo_.mapping;oldDelay_=delay_;delay_=timing_.plan(requested);fadePosition_=0;}
  if(clockTrace_&&activeTime&&(lastClockTrace_==INT64_MIN||queueAfter-lastClockTrace_>=48000)){
   BMDTimeValue hardware=0,inFrame=0,ticks=0;const auto before=mediaSampleTime();const bool valid=output_->GetHardwareReferenceClock(48000,&hardware,&inFrame,&ticks)==S_OK;const auto after=mediaSampleTime();
   clockTrace_->append({queueBefore,queueAfter,consumed,time,before,after,hardware,valid,servo_.mapping.ratio,servo_.phaseError});lastClockTrace_=queueAfter;
  }
  timeline.publish(servo_.mapping);ratePpm=(servo_.mapping.ratio-1)*1e6;estimatedRatePpm=(servo_.estimatedRatio-1)*1e6;phaseError=servo_.phaseError;rateUncertaintyPpm=servo_.rateUncertainty*1e6;driftReady=servo_.ready;driftRejected=servo_.rejected;
  // The SDK stream clock and audio queue can have different phase. The producer
  // clock is paired with actual queue consumption, not the video stream position.
  const uint32_t deviceBuffered=buffered;if(!playbackStarted)buffered=schedule_.bufferedSpan(buffered,0);uint32_t writtenTotal=0;
  try{for(int block=0;buffered<schedule_.target()&&block<24;block++){
   uint32_t count=schedule_.blockSize(buffered,startupPadding_),written=0;const bool padding=schedule_.paddingRemaining(startupPadding_)>0;
   const double sourceStart=delay_.audioSource(servo_.mapping,schedule_.cursor());
   if(!padding)readAge=mediaSampleTime()-int64_t(std::llround(sourceStart+servo_.mapping.ratio*count));
   auto old=(!padding&&fadePosition_<240&&programActive)?mixer_->mixResampled(oldDelay_.audioSource(oldMapping_,schedule_.cursor()),count,oldMapping_.ratio,resampler_,programActive):std::vector<int32_t>{};
   auto pcm=padding?std::vector<int32_t>(count*2,0):mixer_->mixResampled(sourceStart,count,servo_.mapping.ratio,resampler_,programActive);
   if(!old.empty())for(uint32_t j=0;j<count&&fadePosition_+j<240;++j){const double t=audioTransitionWeight(fadePosition_+j,240);for(int c=0;c<2;++c){const auto i=j*2+c;pcm[i]=int32_t(double(old[i])*(1-t)+double(pcm[i])*t);}}
   if(output_->ScheduleAudioSamples(pcm.data(),count,schedule_.cursor(),48000,&written)!=S_OK){return fail();}
   if(syncTrace_&&written)syncTrace_->accepted(pcm.data(),written,schedule_.cursor(),queueAfter,servo_.mapping,delay_.audioSourceDelay(servo_.mapping),fadePosition_<240);
   if(!written)break;if(padding)startupPaddingWritten+=written;fadePosition_=std::min<uint32_t>(240,fadePosition_+written);schedule_.advance(written);buffered+=written;writtenTotal+=written;frames+=written;
  }}catch(...){return fail();}bufferedFrames=deviceBuffered+writtenTotal;
  // Begin playback only from a real SDK preroll callback, once both queues are
  // reported ready. Scheduled writes alone do not establish driver readiness.
  if(preroll&&!playbackStarted){uint32_t videoBuffered=0;
   if(output_->GetBufferedVideoFrameCount(&videoBuffered)!=S_OK){return fail();}
   if(deviceBuffered>=schedule_.target()&&videoBuffered>=reserve_){
    prerollAudio=deviceBuffered;prerollVideo=videoBuffered;
    if(output_->StartScheduledPlayback(0,playbackScale_,1.0)!=S_OK){return fail();}
    playbackStarted=true;startCv_.notify_all();
   }
  }return S_OK;
 }
};

}
class DeckLink final:public Output {
    Com<IDeckLink> device_;Com<IDeckLinkOutput> output_;Com<IDeckLinkKeyer> keyer_;
    Com<Callback> callback_;Com<AudioCallback> audioCallback_;std::shared_ptr<AudioMixer> mixer_=std::make_shared<AudioMixer>();std::vector<Com<IDeckLinkMutableVideoFrame>> pool_;
    std::shared_ptr<CompletionState> state_=std::make_shared<CompletionState>();
    std::atomic<bool> running_{false},air_{true},quit_{false};
    mutable std::mutex messageMutex_;std::string message_="Output stopped.";
    std::shared_ptr<const Frame> cachedSource_;std::vector<uint8_t> converted_;bool cachedAir_=false;
    std::thread worker_;Mode mode_;AVTiming frameTiming_;const LatestFrame* source_=nullptr;uint64_t next_=0;
    bool referenceDiagnostic_=false;std::vector<std::array<double,8>> referenceSchedule_;
    std::unique_ptr<SyncEventBuffer<SyncVideoEvent>> syncTrace_;
    bool syncDiagnostic_=false,syncWhite_=false;std::atomic<int64_t> scheduledFlashPts_{INT64_MIN};std::atomic<uint64_t> scheduledFlashCount_{0};
    std::atomic<uint64_t> historyMisses_{0},historyReads_{0},historyStartupMisses_{0},preparedReads_{0},conversionFallbacks_{0};std::atomic<int64_t> selectedAge_{INT64_MIN};
    uint32_t reserve_=3;std::shared_ptr<std::atomic<int32_t>> syncOffset_=std::make_shared<std::atomic<int32_t>>(0);AudioMapping videoMapping_;mutable AudioMapping statusMapping_;
    uint32_t sourceAge_=2880; // Provisional 60 ms on raw PTS; independent of the SDI reserve.
    bool videoEnabled_=false,audioEnabled_=false,videoCallbackSet_=false,audioCallbackSet_=false,keyerEnabled_=false,playbackStarted_=false;
    void message(std::string s){std::lock_guard<std::mutex> l(messageMutex_);message_=std::move(s);}
    void schedule(IDeckLinkVideoFrame* f){
        Com<IDeckLinkVideoBuffer> buffer;
        check(f->QueryInterface(IID_IDeckLinkVideoBuffer,reinterpret_cast<void**>(buffer.put())),"Get SDK 16.0 buffer");
        check(buffer->StartAccess(bmdBufferAccessWrite),"Access video buffer");
        void* bytes=nullptr;auto result=buffer->GetBytes(&bytes);
        try {
            check(result,"Get video memory");const auto syncAtFrame=syncOffset_->load();const auto frameDelay=frameTiming_.plan(syncAtFrame);int64_t selectionTime=INT64_MIN;std::shared_ptr<const Frame> image;std::shared_ptr<const std::vector<uint8_t>> prepared;
            if(audioCallback_){
                const auto streamPosition=mode_.rate.timestamp(next_+CompletionState::startupLeadFrames)*48000/mode_.rate.scale;
                if(auto mapping=audioCallback_->timeline.read())videoMapping_=*mapping;
                const auto target=int64_t(std::llround(frameDelay.videoSource(videoMapping_,streamPosition)));
                auto selected=source_->history.at(target,mode_.rate.duration*48000/(2*mode_.rate.scale));if(selected.frame&&selected.frame->size!=mode_.size)selected={};image=selected.frame;prepared=selected.prepared;selectionTime=selected.time;++historyReads_;
                if(!image){if(audioCallback_->clockCalibrated)++historyMisses_;else ++historyStartupMisses_;selectedAge_=INT64_MIN;}
                else selectedAge_=target-selected.time;
            }
            if(referenceDiagnostic_&&referenceSchedule_.size()<4096&&image){const auto id=referenceFrameId(image->pixels.data(),image->size.width,image->size.height,image->stride);if(id)referenceSchedule_.push_back({double(mode_.rate.timestamp(next_+CompletionState::startupLeadFrames)*48000/mode_.rate.scale),double(*id),double(audioCallback_->sourceOrigin.load()),double(image->paintTime),double(image->contentTime),frameDelay.videoSource(videoMapping_,mode_.rate.timestamp(next_+CompletionState::startupLeadFrames)*48000/mode_.rate.scale),videoMapping_.ratio,double(syncAtFrame)});}
            const bool air=air_;
            if(syncDiagnostic_){
                bool white=false;
                if(air&&image&&image->size==mode_.size){
                    const auto pixel=image->pixels.data()+(size_t(image->size.height/2)*image->size.width+image->size.width/2)*4;
                    white=pixel[0]>200&&pixel[1]>200&&pixel[2]>200&&pixel[3]>200;
                }
                if(white&&!syncWhite_){
                    const auto stream=mode_.rate.timestamp(next_+CompletionState::startupLeadFrames)*48000/mode_.rate.scale;
                    scheduledFlashPts_=stream;++scheduledFlashCount_;
                    if(syncTrace_){
                        const auto id=referenceDiagnostic_?referenceFrameId(image->pixels.data(),image->size.width,image->size.height,image->stride):std::optional<uint16_t>{};
                        syncTrace_->append({stream,mediaSampleTime(),image->paintTime,image->contentTime,selectionTime,
                            mode_.rate.duration*48000/(2*mode_.rate.scale),id?int64_t(*id):-1,image->sequence,
                            frameDelay.videoSource(videoMapping_,stream),videoMapping_,syncAtFrame,frameDelay.videoFrames,frameDelay.streamSamples});
                    }
                }
                syncWhite_=white;
            }
            const size_t length=size_t(f->GetRowBytes())*f->GetHeight();
            if(prepared&&image&&image->size==mode_.size&&air){
                ++preparedReads_;
                for(int y=0;y<image->size.height;++y){auto row=static_cast<uint8_t*>(bytes)+size_t(y)*f->GetRowBytes();std::memcpy(row,prepared->data()+size_t(y)*image->stride,image->stride);if(size_t(f->GetRowBytes())>image->stride)std::memset(row+image->stride,0,size_t(f->GetRowBytes())-image->stride);}
            }else{
            if(converted_.size()!=length||cachedSource_!=image||cachedAir_!=air){
                converted_.assign(length,0);
                if(image&&image->size==mode_.size){if(air)++conversionFallbacks_;toSDR(*image,converted_.data(),f->GetRowBytes(),air,mode_.pixelFormat==bmdFormat8BitARGB);}
                cachedSource_=image;cachedAir_=air;
            }
            std::memcpy(bytes,converted_.data(),length);
            }
        }catch(...){buffer->EndAccess(bmdBufferAccessWrite);throw;}
        check(buffer->EndAccess(bmdBufferAccessWrite),"Release video buffer access");
        for(size_t i=0;i<state_->slots.size();++i)if(state_->slots[i]==f)state_->scheduled[i]=int64_t(next_);
        check(output_->ScheduleVideoFrame(f,mode_.rate.timestamp(next_+CompletionState::startupLeadFrames),mode_.rate.duration,mode_.rate.scale),"Schedule video frame");++next_;
    }
    void work(){
        state_->workerQos=pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE,0)==0;
        try{while(!quit_){
            std::unique_lock<std::mutex> l(state_->mutex);
            if(!state_->cv.wait_for(l,std::chrono::seconds(2),[this]{return quit_||!state_->free.empty();})){
                if(audioCallback_&&!audioCallback_->playbackStarted&&!audioCallback_->failed)continue;
                throw std::runtime_error("No SDI callbacks for 2 seconds: device disconnected, profile changed or playback interrupted.");
            }
            if(quit_)break;auto f=state_->free.front();state_->free.pop_front();l.unlock();
            // After a late/drop completion, recover the scheduling cursor without accumulating latency.
            BMDTimeValue time=0;double speed=0;
            if(output_->GetScheduledStreamTime(mode_.rate.scale,&time,&speed)==S_OK&&time>=mode_.rate.timestamp(next_+CompletionState::startupLeadFrames))next_=uint64_t(time/mode_.rate.duration)+1-CompletionState::startupLeadFrames;
            if(audioCallback_&&audioCallback_->failed)throw std::runtime_error("SDI audio output failed. Stop and reconnect the device.");
            schedule(f);
        }}catch(const std::exception& e){message(e.what());running_=false;air_=false;}
    }
public:
    ~DeckLink()override{stop();}
    std::vector<Device> enumerate()override{
        if(running_)throw std::runtime_error("Stop output before refreshing devices.");
        std::vector<Device> result;Com<IDeckLinkIterator> it(CreateDeckLinkIteratorInstance());
        if(!it){message("Desktop Video is unavailable. Install and enable the driver.");return result;}
        for(Com<IDeckLink> d;it->Next(d.put())==S_OK;){
            Com<IDeckLinkOutput> out;if(d->QueryInterface(IID_IDeckLinkOutput,reinterpret_cast<void**>(out.put()))!=S_OK)continue;
            Device dev;dev.id=identity(d.get());if(dev.id.empty())continue;
            CFStringRef name=nullptr;d->GetDisplayName(&name);dev.name=str(name);
            Com<IDeckLinkProfileAttributes> attr;bool external=false;
            if(d->QueryInterface(IID_IDeckLinkProfileAttributes,reinterpret_cast<void**>(attr.put()))==S_OK)attr->GetFlag(BMDDeckLinkSupportsExternalKeying,&external);
            Com<IDeckLinkDisplayModeIterator> modes;if(out->GetDisplayModeIterator(modes.put())!=S_OK)continue;
            for(Com<IDeckLinkDisplayMode> m;modes->Next(m.put())==S_OK;){
                // OSR emits progressive frames. Interlaced modes need temporal field rendering and are intentionally excluded.
                if(m->GetFieldDominance()!=bmdProgressiveFrame||m->GetHeight()<720)continue;
                for(auto pixel:{bmdFormat8BitBGRA,bmdFormat8BitARGB}){
                    bool supported=false,key=false;BMDDisplayMode actual=bmdModeUnknown;
                    // Keying can support RGB modes rejected for ordinary playback
                    // (e.g. HD Mini 1080p50/59.94/60). Query both independently.
                    if(out->DoesSupportVideoMode(bmdVideoConnectionSDI,m->GetDisplayMode(),pixel,bmdNoVideoOutputConversion,bmdSupportedVideoModeDefault,&actual,&supported)!=S_OK||actual!=m->GetDisplayMode())supported=false;
                    if(external){actual=bmdModeUnknown;if(out->DoesSupportVideoMode(bmdVideoConnectionSDI,m->GetDisplayMode(),pixel,bmdNoVideoOutputConversion,bmdSupportedVideoModeKeying,&actual,&key)!=S_OK||actual!=m->GetDisplayMode())key=false;}
                    if(!supported&&!key)continue;
                    Mode mode;mode.id=m->GetDisplayMode();mode.pixelFormat=pixel;mode.size={int(m->GetWidth()),int(m->GetHeight())};mode.keying=key;mode.fillOnlySupported=supported;
                    m->GetFrameRate(&mode.rate.duration,&mode.rate.scale);mode.rate.validate();name=nullptr;m->GetName(&name);mode.name=str(name)+" · "+mode.rate.label()+(key?" · Fill/Key":" · Fill only")+(pixel==bmdFormat8BitBGRA?" · BGRA":" · ARGB");dev.modes.push_back(mode);
                }
            }
            // Highest resolution first, then exact descending frame rate.
            // Keep the preferred BGRA format before ARGB for equivalent modes.
            std::stable_sort(dev.modes.begin(),dev.modes.end(),[](const Mode& a,const Mode& b){
                const auto areaA=int64_t(a.size.width)*a.size.height,areaB=int64_t(b.size.width)*b.size.height;
                if(areaA!=areaB)return areaA>areaB;
                if(a.size.width!=b.size.width)return a.size.width>b.size.width;
                const auto rateA=int64_t(a.rate.scale)*b.rate.duration,rateB=int64_t(b.rate.scale)*a.rate.duration;
                if(rateA!=rateB)return rateA>rateB;
                if(a.pixelFormat!=b.pixelFormat)return a.pixelFormat==bmdFormat8BitBGRA;
                return false;
            });
            if(!dev.modes.empty())result.push_back(std::move(dev));
        }message(result.empty()?"No compatible SDI output device detected.":"Devices checked in the active profile.");return result;
    }
    void validateVideoDelay(int32_t offset,const LatestFrame& source)const{
        if(offset>=0)return;
        const AVTiming geometry(mode_.rate);const auto plan=geometry.plan(offset);
        const size_t frameBytes=size_t(mode_.size.width)*mode_.size.height*8;
        if(geometry.minimumHistoryFrames(offset,reserve_,sourceAge_)>(384u*1024*1024)/frameBytes)
            throw std::runtime_error("A/V adjustment exceeds the bounded video history for this output resolution. Use a smaller negative adjustment.");
        const auto target=mediaSampleTime()-geometry.audioTarget(reserve_)-sourceAge_-int64_t(std::ceil(plan.streamSamples*1.0002));
        const auto selected=source.history.at(target,int64_t(mode_.rate.duration*48000/(2*mode_.rate.scale)));
        if(!selected.frame||selected.frame->size!=mode_.size)
            throw std::runtime_error("Video history is not ready for this A/V adjustment. Allow the source to play before applying it.");
    }
    void start(const std::string& id,const Mode& requested,const LatestFrame& source)override{
        stop();auto devices=enumerate();bool found=false;
        for(auto& d:devices)if(d.id==id)for(auto& m:d.modes)if(m.id==requested.id&&m.pixelFormat==requested.pixelFormat&&(requested.keying?m.keying:m.fillOnlySupported)){mode_=m;mode_.keying=requested.keying;found=true;}
        if(!found)throw std::runtime_error("Selected device, profile or mode is no longer available.");
        validateVideoDelay(syncOffset_->load(),source);
        try{
            Com<IDeckLinkIterator> it(CreateDeckLinkIteratorInstance());if(!it)throw std::runtime_error("Driver unavailable.");
            for(Com<IDeckLink> d;it->Next(d.put())==S_OK;)if(identity(d.get())==id){device_=std::move(d);break;}
            if(!device_)throw std::runtime_error("Device disconnected.");
            check(device_->QueryInterface(IID_IDeckLinkOutput,reinterpret_cast<void**>(output_.put())),"Get video output");
            check(output_->EnableVideoOutput(BMDDisplayMode(mode_.id),bmdVideoOutputFlagDefault),"Enable output (device may be busy)");
            videoEnabled_=true;
            if(mode_.keying){check(device_->QueryInterface(IID_IDeckLinkKeyer,reinterpret_cast<void**>(keyer_.put())),"Get keyer");check(keyer_->Enable(true),"Enable external keying");keyerEnabled_=true;check(keyer_->SetLevel(255),"Set key level");}
            // Configure both streams before queuing any video, matching the SDK
            // SignalGenerator startup order; enabling audio may initialise transport.
            check(output_->EnableAudioOutput(bmdAudioSampleRate48kHz,bmdAudioSampleType32bitInteger,2,bmdAudioOutputStreamTimestamped),"Enable 48 kHz stereo SDI audio");
            audioEnabled_=true;
            frameTiming_=AVTiming(mode_.rate);source_=&source;source_->history.prepareOutput(mode_.pixelFormat==bmdFormat8BitARGB,384*1024*1024);next_=0;quit_=false;air_=true;state_=std::make_shared<CompletionState>();callback_=Com<Callback>(new Callback(state_));
            check(output_->SetScheduledFrameCompletionCallback(callback_.get()),"Register video callback");
            videoCallbackSet_=true;
            int32_t stride=0;check(output_->RowBytesForPixelFormat(BMDPixelFormat(mode_.pixelFormat),mode_.size.width,&stride),"Get video stride");
            for(uint32_t n=0;n<reserve_;n++){
                Com<IDeckLinkMutableVideoFrame> f;check(output_->CreateVideoFrame(mode_.size.width,mode_.size.height,stride,BMDPixelFormat(mode_.pixelFormat),bmdFrameFlagDefault,f.put()),"Create video frame");
                Com<IDeckLinkVideoFrameMutableMetadataExtensions> metadata;
                check(f->QueryInterface(IID_IDeckLinkVideoFrameMutableMetadataExtensions,reinterpret_cast<void**>(metadata.put())),"Get SDR metadata");
                check(metadata->SetInt(bmdDeckLinkFrameMetadataColorspace,bmdColorspaceRec709),"Set Rec.709 metadata");
                state_->slots[size_t(n)]=f.get();pool_.push_back(std::move(f));
            }
            // Establish the physical video path with transparent black before the
            // scheduled programme begins; this does not enlarge steady-state queues.
            {
                Com<IDeckLinkVideoBuffer> blank;check(pool_.front()->QueryInterface(IID_IDeckLinkVideoBuffer,reinterpret_cast<void**>(blank.put())),"Get startup video buffer");
                check(blank->StartAccess(bmdBufferAccessWrite),"Access startup video buffer");void* bytes=nullptr;
                const auto result=blank->GetBytes(&bytes);if(result!=S_OK||!bytes){blank->EndAccess(bmdBufferAccessWrite);check(result==S_OK?E_FAIL:result,"Get startup video bytes");}
                std::memset(bytes,0,size_t(stride)*mode_.size.height);check(blank->EndAccess(bmdBufferAccessWrite),"Release startup video buffer");
                const auto before=mediaSampleTime();check(output_->DisplayVideoFrameSync(pool_.front().get()),"Prepare physical video output");state_->warmupSamples=mediaSampleTime()-before;state_->warmupFrames=1;
            }
            mixer_->enabled=true;audioCallback_=Com<AudioCallback>(new AudioCallback(output_.get(),mixer_,mode_,sourceAge_,reserve_,syncOffset_));
            if(syncDiagnostic_){audioCallback_->enableSyncTrace();syncTrace_=std::make_unique<SyncEventBuffer<SyncVideoEvent>>();}
            if(auto mapping=audioCallback_->timeline.read())videoMapping_=*mapping;
            referenceSchedule_.clear();if(referenceDiagnostic_)referenceSchedule_.reserve(4096);
            historyMisses_=0;historyStartupMisses_=0;preparedReads_=0;conversionFallbacks_=0;historyReads_=0;selectedAge_=INT64_MIN;syncWhite_=false;scheduledFlashPts_=INT64_MIN;scheduledFlashCount_=0;
            for(auto& f:pool_)schedule(f.get());
            check(output_->SetAudioCallback(audioCallback_.get()),"Register SDI audio callback");
            audioCallbackSet_=true;
            // Have the video worker waiting before the first hardware completion.
            state_->accepting=true;worker_=std::thread([this]{work();});
            check(output_->BeginAudioPreroll(),"Begin SDI audio preroll");
            if(!audioCallback_->waitForStart())throw std::runtime_error("SDI preroll failed or timed out: audio/video queues did not become ready.");
            playbackStarted_=true;running_=true;
            message(mode_.keying?"SDI active · Fill + Key · stereo audio":"SDI active · Fill only · stereo audio");
        }catch(...){stop();throw;}
    }
    void syncAdjustment(int32_t offset)override{if(std::abs(int64_t(offset))>9600)throw std::invalid_argument("A/V adjustment exceeds 200 ms");if(running_&&source_)validateVideoDelay(offset,*source_);*syncOffset_=offset;}
    void timing(uint32_t reserve,int32_t offset)override{if(running_)throw std::runtime_error("Stop output before changing timing.");if(reserve<2||reserve>3||std::abs(int64_t(offset))>9600)throw std::invalid_argument("Invalid output timing");reserve_=reserve;*syncOffset_=offset;}
    void referenceDiagnostic(bool enabled)override{if(running_)throw std::runtime_error("Stop output before changing reference diagnostics.");referenceDiagnostic_=enabled;}
    void syncDiagnostic(bool enabled)override{if(running_)throw std::runtime_error("Stop output before changing diagnostics.");syncDiagnostic_=enabled;}
    void onAir(bool value)override{air_=value;if(audioCallback_)audioCallback_->programActive=value;}
    void audio(std::shared_ptr<AudioMixer> mixer)override{if(running_)throw std::runtime_error("Stop output before changing the audio mixer.");mixer_=std::move(mixer);}
    void audioSourceAge(uint32_t samples)override{if(running_)throw std::runtime_error("Stop output before changing source age.");if(samples<480||samples>9600)throw std::invalid_argument("Source age must be 10–200 ms");sourceAge_=samples;}
    void stop()override{
        quit_=true;state_->accepting=false;state_->cv.notify_all();if(worker_.joinable())worker_.join();
        if(audioCallback_){audioCallback_->stop();playbackStarted_=playbackStarted_||audioCallback_->playbackStarted.load();}
        // A failed Enable on an occupied device must not disable another owner's output.
        if(output_){if(audioCallbackSet_)output_->SetAudioCallback(nullptr);if(audioEnabled_)output_->DisableAudioOutput();if(playbackStarted_)output_->StopScheduledPlayback(0,nullptr,mode_.rate.scale);if(videoCallbackSet_)output_->SetScheduledFrameCompletionCallback(nullptr);if(keyer_&&keyerEnabled_)keyer_->Disable();if(videoEnabled_)output_->DisableVideoOutput();}
        videoEnabled_=audioEnabled_=videoCallbackSet_=audioCallbackSet_=keyerEnabled_=playbackStarted_=false;
        for(const auto& r:referenceSchedule_)std::cerr<<std::fixed<<std::setprecision(9)<<"REFERENCE_SCHEDULE stream_samples="<<int64_t(r[0])<<" id="<<int64_t(r[1])<<" audio_output_origin="<<int64_t(r[2])<<" paint_samples="<<int64_t(r[3])<<" content_time="<<int64_t(r[4])<<" source_target="<<r[5]<<" source_ratio="<<r[6]<<" sync_offset="<<int64_t(r[7])<<'\n';referenceSchedule_.clear();
        if(audioCallback_)audioCallback_->dumpSyncTrace();
        if(syncTrace_){for(const auto& e:syncTrace_->events)std::cerr<<std::fixed<<std::setprecision(9)<<"SYNC_VIDEO stream_samples="<<e.stream<<" host_samples="<<e.host<<" paint_samples="<<e.paint<<" content_time="<<e.content<<" selection_samples="<<e.historyTime<<" max_ahead="<<e.maxAhead<<" id="<<e.identity<<" sequence="<<e.sequence<<" source_target="<<e.target<<" mapping_source="<<e.mapping.source<<" mapping_stream="<<e.mapping.stream<<" ratio="<<e.mapping.ratio<<" adjustment="<<e.adjustment<<" common_frames="<<e.commonFrames<<" common_stream_samples="<<e.commonStreamSamples<<" video_delay_source="<<e.commonStreamSamples*e.mapping.ratio<<'\n';std::cerr<<"SYNC_TRACE kind=video stored="<<syncTrace_->events.size()<<" omitted="<<syncTrace_->omitted<<'\n';syncTrace_.reset();}
        running_=false;air_=true;mixer_->enabled=true;audioCallback_=Com<AudioCallback>();if(source_)source_->history.prepareOutput(std::nullopt,192*1024*1024);source_=nullptr;cachedSource_.reset();converted_.clear();pool_.clear();callback_=Com<Callback>();keyer_=Com<IDeckLinkKeyer>();output_=Com<IDeckLinkOutput>();device_=Com<IDeckLink>();
    }
    OutputStatus status()const override{std::lock_guard<std::mutex> l(messageMutex_);if(audioCallback_){if(auto m=audioCallback_->timeline.read())statusMapping_=*m;}else statusMapping_={};return {running_,air_,state_->completed,state_->late,state_->dropped,message_,audioCallback_?audioCallback_->frames.load():0,audioCallback_?audioCallback_->errors.load():0,audioCallback_?audioCallback_->recoveries.load():0,audioCallback_?audioCallback_->bufferedFrames.load():0,audioCallback_?audioCallback_->targetFrames():0,sourceAge_,audioCallback_?audioCallback_->readAge.load():INT64_MIN,audioCallback_?audioCallback_->clockCalibrated.load():false,audioCallback_?audioCallback_->startupShift.load():0,audioCallback_?audioCallback_->streamQueuePhase.load():0,audioCallback_?audioCallback_->prerollAudio.load():0,audioCallback_?audioCallback_->prerollVideo.load():0,state_->firstDroppedFrame.load(),state_->warmupFrames.load(),state_->warmupSamples.load(),CompletionState::startupLeadFrames,audioCallback_?audioCallback_->startupPaddingWritten.load():0,historyReads_.load(),historyMisses_.load(),selectedAge_.load(),source_?source_->history.bytes():0,audioCallback_?audioCallback_->sourceOrigin.load():0,scheduledFlashPts_.load(),scheduledFlashCount_.load(),historyStartupMisses_.load(),preparedReads_.load(),conversionFallbacks_.load(),state_->workerQos.load(),audioCallback_?audioCallback_->ratePpm.load():0,audioCallback_?audioCallback_->estimatedRatePpm.load():0,audioCallback_?audioCallback_->phaseError.load():0,audioCallback_?audioCallback_->rateUncertaintyPpm.load():0,audioCallback_?audioCallback_->driftReady.load():false,audioCallback_?audioCallback_->driftRejected.load():0,statusMapping_,reserve_,syncOffset_->load()};}
};
std::unique_ptr<Output> makeOutput(){return std::make_unique<DeckLink>();}
}
