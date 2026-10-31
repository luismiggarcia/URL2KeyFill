// Measures the DeckLink output clock against the Mac host clock (mach_absolute_time).
// Plays transparent black video and digital silence, so SDI OUT carries a real signal
// for the whole run. Writes one CSV row per sample and prints the fitted rate.
//
// decklink_clock [--seconds S] [--mode 1080p25|1080p50|1080p5994] [--csv FILE]
// --av-reference 1 replaces black/silence with explicitly scheduled flash/pulse.
#include "DeckLinkAPI.h"
#include <CoreFoundation/CoreFoundation.h>
#include <mach/mach_time.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

static std::atomic<bool> interrupted{false};
static double hostNs(){static const double tick=[]{mach_timebase_info_data_t t;mach_timebase_info(&t);return double(t.numer)/t.denom;}();return mach_absolute_time()*tick;}
static void check(HRESULT r,const char* what){if(r!=S_OK){std::fprintf(stderr,"%s failed (HRESULT %d)\n",what,int(r));std::exit(1);}}

static bool referenceFrame(IDeckLinkVideoFrame* frame,int64_t index,int64_t duration,int64_t scale){
    IDeckLinkVideoBuffer* buffer=nullptr;if(frame->QueryInterface(IID_IDeckLinkVideoBuffer,reinterpret_cast<void**>(&buffer))!=S_OK)return false;
    bool ok=buffer->StartAccess(bmdBufferAccessWrite)==S_OK;
    if(ok){void* bytes=nullptr;ok=buffer->GetBytes(&bytes)==S_OK&&bytes;
        if(ok){const bool flash=(index*duration*48000/scale)%48000<1920;auto pixels=static_cast<uint32_t*>(bytes);std::fill_n(pixels,size_t(frame->GetRowBytes())*frame->GetHeight()/4,flash?0xffffffffu:0xff000000u);}
        if(buffer->EndAccess(bmdBufferAccessWrite)!=S_OK)ok=false;
    }buffer->Release();return ok;
}

class VideoCallback final:public IDeckLinkVideoOutputCallback {
    IDeckLinkOutput* output_;BMDTimeValue duration_;BMDTimeScale scale_;bool reference_;
public:
    std::atomic<int64_t> next{0};std::atomic<uint64_t> completed{0},late{0},dropped{0},errors{0};
    VideoCallback(IDeckLinkOutput* o,BMDTimeValue d,BMDTimeScale s,bool reference):output_(o),duration_(d),scale_(s),reference_(reference){}
    HRESULT QueryInterface(REFIID,LPVOID* p)override{*p=nullptr;return E_NOINTERFACE;}
    ULONG AddRef()override{return 1;}ULONG Release()override{return 1;}
    HRESULT ScheduledFrameCompleted(IDeckLinkVideoFrame* f,BMDOutputFrameCompletionResult r)override{
        if(r==bmdOutputFrameFlushed||interrupted)return S_OK;
        ++completed;if(r==bmdOutputFrameDisplayedLate)++late;if(r==bmdOutputFrameDropped)++dropped;
        const auto index=next.fetch_add(1);if(reference_&&!referenceFrame(f,index,duration_,scale_)){++errors;return E_FAIL;}if(output_->ScheduleVideoFrame(f,index*duration_,duration_,scale_)!=S_OK){++errors;return E_FAIL;}return S_OK;
    }
    HRESULT ScheduledPlaybackHasStopped()override{return S_OK;}
};

// Use the SDK sample's readiness-driven preroll for the A/V reference.
class ReferenceAudio final:public IDeckLinkAudioOutputCallback {
 IDeckLinkOutput* output_;int64_t scale_;std::vector<int32_t> pcm_;
public:
 std::atomic<bool> started{false},requested{false};std::atomic<uint64_t> errors{0};std::atomic<int64_t> written{0};
 ReferenceAudio(IDeckLinkOutput* output,int64_t scale):output_(output),scale_(scale),pcm_(4800){}
 HRESULT QueryInterface(REFIID,LPVOID* p)override{if(p)*p=nullptr;return E_NOINTERFACE;}
 ULONG AddRef()override{return 1;}ULONG Release()override{return 1;}
 HRESULT RenderAudioSamples(bool preroll)override{
  uint32_t buffered=0;if(output_->GetBufferedAudioSampleFrameCount(&buffered)!=S_OK){++errors;return E_FAIL;}
  const auto before=buffered;
  for(int block=0;buffered<9600&&block<8;++block){const uint32_t n=std::min<uint32_t>(2400,9600-buffered);const auto start=written.load();
   for(uint32_t i=0;i<n;++i){const double t=double((start+i)%48000)/48000.;const double envelope=std::max(0.,std::min({1.,t/.002,(.04-t)/.002}));const auto v=int32_t(t<.04?std::sin(2*3.141592653589793*1000*t)*envelope*270352173.:0);pcm_[i*2]=pcm_[i*2+1]=v;}
   uint32_t done=0;if(output_->ScheduleAudioSamples(pcm_.data(),n,start,48000,&done)!=S_OK){++errors;return E_FAIL;}if(!done)break;written+=done;buffered+=done;
  }
  uint32_t frames=0;
  if(preroll&&before>=9600&&output_->GetBufferedVideoFrameCount(&frames)==S_OK&&frames>=4&&!requested.exchange(true)){
   if(output_->StartScheduledPlayback(0,scale_,1.0)!=S_OK){++errors;return E_FAIL;}started=true;
  }return S_OK;
 }
};

struct Sample{double host,callNs;int64_t stream,hardware,consumed;};
struct Fit{double ppm,residualRms,residualMax;};
static Fit fit(const std::vector<Sample>& s,double unitsPerSecond,int64_t Sample::*field){
    // Least squares of device time (seconds) against host time (seconds).
    double n=0,sx=0,sy=0,sxx=0,sxy=0;const double x0=s.front().host,y0=double(s.front().*field);
    for(auto& v:s){double x=(v.host-x0)/1e9,y=(v.*field-y0)/unitsPerSecond;n++;sx+=x;sy+=y;sxx+=x*x;sxy+=x*y;}
    const double slope=(n*sxy-sx*sy)/(n*sxx-sx*sx),intercept=(sy-slope*sx)/n;double sq=0,mx=0;
    for(auto& v:s){double x=(v.host-x0)/1e9,y=(v.*field-y0)/unitsPerSecond,e=(y-(intercept+slope*x))*1e3;sq+=e*e;mx=std::max(mx,std::abs(e));}
    return {(slope-1)*1e6,std::sqrt(sq/n),mx};
}

int main(int argc,char** argv){
    bool avReference=false;double seconds=600;std::string modeName="1080p25",csv;
    for(int i=1;i+1<argc;i+=2){if(!std::strcmp(argv[i],"--seconds"))seconds=std::atof(argv[i+1]);else if(!std::strcmp(argv[i],"--mode"))modeName=argv[i+1];else if(!std::strcmp(argv[i],"--av-reference"))avReference=std::string(argv[i+1])=="1";else if(!std::strcmp(argv[i],"--csv"))csv=argv[i+1];}
    const BMDDisplayMode mode=modeName=="1080p50"?bmdModeHD1080p50:modeName=="1080p5994"?bmdModeHD1080p5994:bmdModeHD1080p25;
    std::signal(SIGINT,[](int){interrupted=true;});std::signal(SIGTERM,[](int){interrupted=true;});

    IDeckLinkIterator* it=CreateDeckLinkIteratorInstance();if(!it){std::fprintf(stderr,"Desktop Video driver unavailable\n");return 1;}
    IDeckLink* device=nullptr;IDeckLinkOutput* output=nullptr;
    while(it->Next(&device)==S_OK){if(device->QueryInterface(IID_IDeckLinkOutput,(void**)&output)==S_OK)break;device->Release();device=nullptr;}
    it->Release();if(!output){std::fprintf(stderr,"No DeckLink output device\n");return 1;}
    CFStringRef cfName=nullptr;char name[256]={};if(device->GetDisplayName(&cfName)==S_OK){CFStringGetCString(cfName,name,sizeof name,kCFStringEncodingUTF8);CFRelease(cfName);}

    BMDDisplayMode actual=bmdModeUnknown;bool plain=false,keyed=false;
    output->DoesSupportVideoMode(bmdVideoConnectionSDI,mode,bmdFormat8BitBGRA,bmdNoVideoOutputConversion,bmdSupportedVideoModeDefault,&actual,&plain);
    if(!plain)output->DoesSupportVideoMode(bmdVideoConnectionSDI,mode,bmdFormat8BitBGRA,bmdNoVideoOutputConversion,bmdSupportedVideoModeKeying,&actual,&keyed);
    if(!plain&&!keyed){std::fprintf(stderr,"Mode %s BGRA not supported\n",modeName.c_str());return 1;}
    IDeckLinkDisplayMode* display=nullptr;check(output->GetDisplayMode(mode,&display),"GetDisplayMode");
    BMDTimeValue duration;BMDTimeScale scale;display->GetFrameRate(&duration,&scale);const int width=int(display->GetWidth()),height=int(display->GetHeight());display->Release();

    check(output->EnableVideoOutput(mode,bmdVideoOutputFlagDefault),"EnableVideoOutput (device busy?)");
    IDeckLinkKeyer* keyer=nullptr;
    if(keyed){check(device->QueryInterface(IID_IDeckLinkKeyer,(void**)&keyer),"Get keyer");check(keyer->Enable(true),"Enable external keying");check(keyer->SetLevel(255),"Set key level");}
    VideoCallback callback(output,duration,scale,avReference);check(output->SetScheduledFrameCompletionCallback(&callback),"Video callback");
    std::vector<IDeckLinkMutableVideoFrame*> frames(4);
    for(auto& f:frames){
        check(output->CreateVideoFrame(width,height,width*4,bmdFormat8BitBGRA,bmdFrameFlagDefault,&f),"CreateVideoFrame");
        IDeckLinkVideoBuffer* buffer=nullptr;check(f->QueryInterface(IID_IDeckLinkVideoBuffer,(void**)&buffer),"Video buffer");
        void* bytes=nullptr;check(buffer->StartAccess(bmdBufferAccessWrite),"StartAccess");buffer->GetBytes(&bytes);std::memset(bytes,0,size_t(width)*4*height);buffer->EndAccess(bmdBufferAccessWrite);buffer->Release();
        const auto index=callback.next.fetch_add(1);if(avReference&&!referenceFrame(f,index,duration,scale))return 1;
        check(output->ScheduleVideoFrame(f,index*duration,duration,scale),"ScheduleVideoFrame");
    }
    check(output->EnableAudioOutput(bmdAudioSampleRate48kHz,bmdAudioSampleType32bitInteger,2,bmdAudioOutputStreamTimestamped),"EnableAudioOutput");
    std::vector<int32_t> silence(2*2400,0);int64_t written=0;ReferenceAudio referenceAudio(output,scale);
    auto feed=[&](uint32_t target){uint32_t buffered=0;output->GetBufferedAudioSampleFrameCount(&buffered);
        while(buffered<target){uint32_t done=0;if(output->ScheduleAudioSamples(silence.data(),2400,written,48000,&done)!=S_OK||!done)break;written+=done;buffered+=done;}return buffered;};
    if(avReference)check(output->SetAudioCallback(&referenceAudio),"Reference audio callback");
    check(output->BeginAudioPreroll(),"BeginAudioPreroll");
    if(avReference){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(!referenceAudio.started&&!referenceAudio.errors&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(10));if(!referenceAudio.started){std::fprintf(stderr,"Reference preroll did not become ready\n");output->SetAudioCallback(nullptr);output->DisableAudioOutput();output->DisableVideoOutput();return 1;}}
    else{feed(9600);check(output->EndAudioPreroll(),"EndAudioPreroll");check(output->StartScheduledPlayback(0,scale,1.0),"StartScheduledPlayback");}
    if(avReference){std::fprintf(stderr,"READY SDK reference output\n");std::fflush(stderr);}
    std::printf("Device: %s · %s BGRA%s · %lld/%lld · %.0f s\n",name,modeName.c_str(),keyed?" (Fill/Key)":"",(long long)scale,(long long)duration,seconds);

    std::vector<Sample> samples;const double start=hostNs();double nextReport=60;std::FILE* out=csv.empty()?nullptr:std::fopen(csv.c_str(),"w");
    if(out)std::fprintf(out,"host_ns,call_ns,stream_us,hardware_us,audio_consumed\n");
    while(!interrupted&&(hostNs()-start)/1e9<seconds){
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if(!avReference)feed(9600);
        const double t0=hostNs();BMDTimeValue stream=0,hardware=0,inFrame=0,ticks=0;double speed=0;uint32_t buffered=0;
        const bool ok=output->GetScheduledStreamTime(1000000,&stream,&speed)==S_OK&&output->GetHardwareReferenceClock(1000000,&hardware,&inFrame,&ticks)==S_OK&&output->GetBufferedAudioSampleFrameCount(&buffered)==S_OK;
        const double t1=hostNs();
        if(!ok||stream<=0)continue;
        Sample s{(t0+t1)/2,t1-t0,stream,hardware,(avReference?referenceAudio.written.load():written)-int64_t(buffered)};samples.push_back(s);
        if(out)std::fprintf(out,"%.0f,%.0f,%lld,%lld,%lld\n",s.host,s.callNs,(long long)s.stream,(long long)s.hardware,(long long)s.consumed);
        const double elapsed=(t1-start)/1e9;
        if(elapsed>=nextReport&&samples.size()>100){auto v=fit(samples,1e6,&Sample::stream);std::printf("  %4.0f s: stream clock %+7.2f ppm · frames %llu late %llu dropped %llu\n",elapsed,v.ppm,(unsigned long long)callback.completed.load(),(unsigned long long)callback.late.load(),(unsigned long long)callback.dropped.load());std::fflush(stdout);nextReport+=60;}
    }
    interrupted=true;
    output->StopScheduledPlayback(0,nullptr,scale);if(avReference)output->SetAudioCallback(nullptr);output->DisableAudioOutput();output->SetScheduledFrameCompletionCallback(nullptr);
    if(keyer){keyer->Disable();keyer->Release();}output->DisableVideoOutput();
    for(auto f:frames)f->Release();output->Release();device->Release();if(out)std::fclose(out);
    if(samples.size()<10){std::fprintf(stderr,"Too few samples\n");return 1;}
    // Keep the cleanest host/device pairs: calls within the 90th percentile of duration.
    std::vector<double> calls;for(auto& s:samples)calls.push_back(s.callNs);std::sort(calls.begin(),calls.end());const double limit=calls[calls.size()*9/10];
    std::vector<Sample> clean;for(auto& s:samples)if(s.callNs<=limit)clean.push_back(s);
    const auto st=fit(clean,1e6,&Sample::stream),hw=fit(clean,1e6,&Sample::hardware),au=fit(clean,48000,&Sample::consumed);
    const double span=(clean.back().host-clean.front().host)/1e9;
    std::printf("Samples %zu (of %zu), span %.1f s, call p90 %.1f us\n",clean.size(),samples.size(),span,limit/1e3);
    std::printf("Stream clock vs host:     %+8.2f ppm  (residual rms %.3f ms, max %.3f ms)\n",st.ppm,st.residualRms,st.residualMax);
    std::printf("Hardware clock vs host:   %+8.2f ppm  (residual rms %.3f ms, max %.3f ms)\n",hw.ppm,hw.residualRms,hw.residualMax);
    std::printf("Audio consumption vs host:%+8.2f ppm  (residual rms %.3f ms, max %.3f ms)\n",au.ppm,au.residualRms,au.residualMax);
    std::printf("Video frames %llu · late %llu · dropped %llu\n",(unsigned long long)callback.completed.load(),(unsigned long long)callback.late.load(),(unsigned long long)callback.dropped.load());
    if(callback.errors||callback.late||callback.dropped||referenceAudio.errors){std::fprintf(stderr,"Reference output scheduling anomalies\n");return 1;}
    std::printf("Implied drift of the current SDI audio mapping: %+.1f ms per hour\n",au.ppm*3.6);
}
