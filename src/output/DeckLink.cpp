#include "Output.hpp"
#include "DeckLinkAPI.h"
#include <CoreFoundation/CoreFoundation.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>

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
    std::atomic<bool> accepting{false};
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
        if(r==bmdOutputFrameDropped)++state_->dropped;
        if(r!=bmdOutputFrameFlushed)++state_->completed;
        if(state_->accepting&&r!=bmdOutputFrameFlushed){std::lock_guard<std::mutex> l(state_->mutex);if(state_->free.size()<3)state_->free.push_back(f);state_->cv.notify_one();}return S_OK;
    }
    HRESULT ScheduledPlaybackHasStopped()override{state_->cv.notify_all();return S_OK;}
};
class AudioCallback final:public IDeckLinkAudioOutputCallback {
 std::atomic<ULONG> refs_{1};IDeckLinkOutput* output_;std::shared_ptr<AudioMixer> mixer_;std::mutex mutex_;
 int64_t cursor_=0,epoch_;uint32_t target_;bool accepting_=true;
public:
 std::atomic<uint64_t> frames{0},errors{0};std::atomic<bool> failed{false};
 AudioCallback(IDeckLinkOutput* o,std::shared_ptr<AudioMixer> m,const Mode& mode):output_(o),mixer_(std::move(m)){
  output_->AddRef();target_=uint32_t(std::clamp<int64_t>((3*mode.rate.duration*48000+mode.rate.scale-1)/mode.rate.scale,1440,12000));epoch_=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()*48;
 }
 ~AudioCallback(){output_->Release();}
 HRESULT QueryInterface(REFIID iid,LPVOID* p)override{if(!p)return E_INVALIDARG;*p=nullptr;const auto unknown=CFUUIDGetUUIDBytes(IUnknownUUID);if(!std::memcmp(&iid,&unknown,sizeof(iid))||!std::memcmp(&iid,&IID_IDeckLinkAudioOutputCallback,sizeof(iid))){*p=static_cast<IDeckLinkAudioOutputCallback*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
 ULONG AddRef()override{return ++refs_;}ULONG Release()override{auto n=--refs_;if(!n)delete this;return n;}
 void stop(){std::lock_guard<std::mutex> lock(mutex_);accepting_=false;}
 HRESULT RenderAudioSamples(bool)override{
  std::lock_guard<std::mutex> lock(mutex_);if(!accepting_)return S_OK;
  uint32_t buffered=0;if(output_->GetBufferedAudioSampleFrameCount(&buffered)!=S_OK){++errors;failed=true;return E_FAIL;}
  BMDTimeValue time=0;double speed=0;if(output_->GetScheduledStreamTime(48000,&time,&speed)==S_OK&&time>cursor_){cursor_=time;++errors;}
  try{for(int block=0;buffered<target_&&block<24;block++){
   uint32_t count=std::min<uint32_t>(1024,target_-buffered),written=0;auto pcm=mixer_->mix(epoch_+cursor_-target_,count);
   if(output_->ScheduleAudioSamples(pcm.data(),count,cursor_,48000,&written)!=S_OK){++errors;failed=true;return E_FAIL;}
   if(!written)break;cursor_+=written;buffered+=written;frames+=written;
  }}catch(...){++errors;failed=true;return E_FAIL;}return S_OK;
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
    std::thread worker_;Mode mode_;const LatestFrame* source_=nullptr;uint64_t next_=0;
    void message(std::string s){std::lock_guard<std::mutex> l(messageMutex_);message_=std::move(s);}
    void schedule(IDeckLinkVideoFrame* f){
        Com<IDeckLinkVideoBuffer> buffer;
        check(f->QueryInterface(IID_IDeckLinkVideoBuffer,reinterpret_cast<void**>(buffer.put())),"Get SDK 16.0 buffer");
        check(buffer->StartAccess(bmdBufferAccessWrite),"Access video buffer");
        void* bytes=nullptr;auto result=buffer->GetBytes(&bytes);
        try {
            check(result,"Get video memory");auto image=source_->get();
            const bool air=air_;const size_t length=size_t(f->GetRowBytes())*f->GetHeight();
            if(converted_.size()!=length||cachedSource_!=image||cachedAir_!=air){
                converted_.assign(length,0);
                if(image&&image->size==mode_.size)toSDR(*image,converted_.data(),f->GetRowBytes(),air,mode_.pixelFormat==bmdFormat8BitARGB);
                cachedSource_=image;cachedAir_=air;
            }
            std::memcpy(bytes,converted_.data(),length);
        }catch(...){buffer->EndAccess(bmdBufferAccessWrite);throw;}
        check(buffer->EndAccess(bmdBufferAccessWrite),"Release video buffer access");
        check(output_->ScheduleVideoFrame(f,mode_.rate.timestamp(next_),mode_.rate.duration,mode_.rate.scale),"Schedule video frame");++next_;
    }
    void work(){
        try{while(!quit_){
            std::unique_lock<std::mutex> l(state_->mutex);
            if(!state_->cv.wait_for(l,std::chrono::seconds(2),[this]{return quit_||!state_->free.empty();}))throw std::runtime_error("No SDI callbacks for 2 seconds: device disconnected, profile changed or playback interrupted.");
            if(quit_)break;auto f=state_->free.front();state_->free.pop_front();l.unlock();
            // After a late/drop completion, recover the scheduling cursor without accumulating latency.
            BMDTimeValue time=0;double speed=0;
            if(output_->GetScheduledStreamTime(mode_.rate.scale,&time,&speed)==S_OK&&time>=mode_.rate.timestamp(next_))next_=uint64_t(time/mode_.rate.duration)+1;
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
    void start(const std::string& id,const Mode& requested,const LatestFrame& source)override{
        stop();auto devices=enumerate();bool found=false;
        for(auto& d:devices)if(d.id==id)for(auto& m:d.modes)if(m.id==requested.id&&m.pixelFormat==requested.pixelFormat&&(requested.keying?m.keying:m.fillOnlySupported)){mode_=m;mode_.keying=requested.keying;found=true;}
        if(!found)throw std::runtime_error("Selected device, profile or mode is no longer available.");
        try{
            Com<IDeckLinkIterator> it(CreateDeckLinkIteratorInstance());if(!it)throw std::runtime_error("Driver unavailable.");
            for(Com<IDeckLink> d;it->Next(d.put())==S_OK;)if(identity(d.get())==id){device_=std::move(d);break;}
            if(!device_)throw std::runtime_error("Device disconnected.");
            check(device_->QueryInterface(IID_IDeckLinkOutput,reinterpret_cast<void**>(output_.put())),"Get video output");
            check(output_->EnableVideoOutput(BMDDisplayMode(mode_.id),bmdVideoOutputFlagDefault),"Enable output (device may be busy)");
            if(mode_.keying){check(device_->QueryInterface(IID_IDeckLinkKeyer,reinterpret_cast<void**>(keyer_.put())),"Get keyer");check(keyer_->Enable(true),"Enable external keying");check(keyer_->SetLevel(255),"Set key level");}
            source_=&source;next_=0;quit_=false;air_=true;state_=std::make_shared<CompletionState>();callback_=Com<Callback>(new Callback(state_));
            check(output_->SetScheduledFrameCompletionCallback(callback_.get()),"Register video callback");
            int32_t stride=0;check(output_->RowBytesForPixelFormat(BMDPixelFormat(mode_.pixelFormat),mode_.size.width,&stride),"Get video stride");
            for(int n=0;n<3;n++){
                Com<IDeckLinkMutableVideoFrame> f;check(output_->CreateVideoFrame(mode_.size.width,mode_.size.height,stride,BMDPixelFormat(mode_.pixelFormat),bmdFrameFlagDefault,f.put()),"Create video frame");
                Com<IDeckLinkVideoFrameMutableMetadataExtensions> metadata;
                check(f->QueryInterface(IID_IDeckLinkVideoFrameMutableMetadataExtensions,reinterpret_cast<void**>(metadata.put())),"Get SDR metadata");
                check(metadata->SetInt(bmdDeckLinkFrameMetadataColorspace,bmdColorspaceRec709),"Set Rec.709 metadata");
                pool_.push_back(std::move(f));schedule(pool_.back().get());
            }
            check(output_->EnableAudioOutput(bmdAudioSampleRate48kHz,bmdAudioSampleType32bitInteger,2,bmdAudioOutputStreamTimestamped),"Enable 48 kHz stereo SDI audio");
            mixer_->enabled=true;audioCallback_=Com<AudioCallback>(new AudioCallback(output_.get(),mixer_,mode_));
            check(output_->SetAudioCallback(audioCallback_.get()),"Register SDI audio callback");
            check(output_->BeginAudioPreroll(),"Begin SDI audio preroll");check(audioCallback_->RenderAudioSamples(true),"Preroll audio");check(output_->EndAudioPreroll(),"End SDI audio preroll");
            state_->accepting=true;check(output_->StartScheduledPlayback(0,mode_.rate.scale,1.0),"Start playback");running_=true;
            message(mode_.keying?"SDI active · Fill + Key · stereo audio":"SDI active · Fill only · stereo audio");worker_=std::thread([this]{work();});
        }catch(...){stop();throw;}
    }
    void onAir(bool value)override{air_=value;mixer_->enabled=value;}
    void audio(std::shared_ptr<AudioMixer> mixer)override{if(running_)throw std::runtime_error("Stop output before changing the audio mixer.");mixer_=std::move(mixer);}
    void stop()override{
        quit_=true;state_->accepting=false;state_->cv.notify_all();if(worker_.joinable())worker_.join();
        if(audioCallback_)audioCallback_->stop();
        if(output_){output_->SetAudioCallback(nullptr);output_->DisableAudioOutput();output_->StopScheduledPlayback(0,nullptr,mode_.rate.scale);output_->SetScheduledFrameCompletionCallback(nullptr);if(keyer_)keyer_->Disable();output_->DisableVideoOutput();}
        running_=false;audioCallback_=Com<AudioCallback>();cachedSource_.reset();converted_.clear();pool_.clear();callback_=Com<Callback>();keyer_=Com<IDeckLinkKeyer>();output_=Com<IDeckLinkOutput>();device_=Com<IDeckLink>();
    }
    OutputStatus status()const override{std::lock_guard<std::mutex> l(messageMutex_);return {running_,air_,state_->completed,state_->late,state_->dropped,message_,audioCallback_?audioCallback_->frames.load():0,audioCallback_?audioCallback_->errors.load():0};}
};
std::unique_ptr<Output> makeOutput(){return std::make_unique<DeckLink>();}
}
