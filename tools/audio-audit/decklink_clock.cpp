// Measures the DeckLink output clock against the Mac host clock (mach_absolute_time).
// Plays transparent black video and digital silence, so SDI OUT carries a real signal
// for the whole run. Writes one CSV row per sample and prints the fitted rate.
//
// decklink_clock [--seconds S] [--mode 1080p25|1080p50|1080p5994] [--csv FILE]
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

class VideoCallback final:public IDeckLinkVideoOutputCallback {
    IDeckLinkOutput* output_;BMDTimeValue duration_;BMDTimeScale scale_;
public:
    std::atomic<int64_t> next{0};std::atomic<uint64_t> completed{0},late{0},dropped{0};
    VideoCallback(IDeckLinkOutput* o,BMDTimeValue d,BMDTimeScale s):output_(o),duration_(d),scale_(s){}
    HRESULT QueryInterface(REFIID,LPVOID* p)override{*p=nullptr;return E_NOINTERFACE;}
    ULONG AddRef()override{return 1;}ULONG Release()override{return 1;}
    HRESULT ScheduledFrameCompleted(IDeckLinkVideoFrame* f,BMDOutputFrameCompletionResult r)override{
        if(r==bmdOutputFrameFlushed||interrupted)return S_OK;
        ++completed;if(r==bmdOutputFrameDisplayedLate)++late;if(r==bmdOutputFrameDropped)++dropped;
        output_->ScheduleVideoFrame(f,next.fetch_add(1)*duration_,duration_,scale_);return S_OK;
    }
    HRESULT ScheduledPlaybackHasStopped()override{return S_OK;}
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
    double seconds=600;std::string modeName="1080p25",csv;
    for(int i=1;i+1<argc;i+=2){if(!std::strcmp(argv[i],"--seconds"))seconds=std::atof(argv[i+1]);else if(!std::strcmp(argv[i],"--mode"))modeName=argv[i+1];else if(!std::strcmp(argv[i],"--csv"))csv=argv[i+1];}
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
    VideoCallback callback(output,duration,scale);check(output->SetScheduledFrameCompletionCallback(&callback),"Video callback");
    std::vector<IDeckLinkMutableVideoFrame*> frames(4);
    for(auto& f:frames){
        check(output->CreateVideoFrame(width,height,width*4,bmdFormat8BitBGRA,bmdFrameFlagDefault,&f),"CreateVideoFrame");
        IDeckLinkVideoBuffer* buffer=nullptr;check(f->QueryInterface(IID_IDeckLinkVideoBuffer,(void**)&buffer),"Video buffer");
        void* bytes=nullptr;check(buffer->StartAccess(bmdBufferAccessWrite),"StartAccess");buffer->GetBytes(&bytes);std::memset(bytes,0,size_t(width)*4*height);buffer->EndAccess(bmdBufferAccessWrite);buffer->Release();
        check(output->ScheduleVideoFrame(f,callback.next.fetch_add(1)*duration,duration,scale),"ScheduleVideoFrame");
    }
    check(output->EnableAudioOutput(bmdAudioSampleRate48kHz,bmdAudioSampleType32bitInteger,2,bmdAudioOutputStreamTimestamped),"EnableAudioOutput");
    std::vector<int32_t> silence(2*2400,0);int64_t written=0;
    auto feed=[&](uint32_t target){uint32_t buffered=0;output->GetBufferedAudioSampleFrameCount(&buffered);
        while(buffered<target){uint32_t done=0;if(output->ScheduleAudioSamples(silence.data(),2400,written,48000,&done)!=S_OK||!done)break;written+=done;buffered+=done;}return buffered;};
    check(output->BeginAudioPreroll(),"BeginAudioPreroll");feed(9600);check(output->EndAudioPreroll(),"EndAudioPreroll");
    check(output->StartScheduledPlayback(0,scale,1.0),"StartScheduledPlayback");
    std::printf("Device: %s · %s BGRA%s · %lld/%lld · %.0f s\n",name,modeName.c_str(),keyed?" (Fill/Key)":"",(long long)scale,(long long)duration,seconds);

    std::vector<Sample> samples;const double start=hostNs();double nextReport=60;std::FILE* out=csv.empty()?nullptr:std::fopen(csv.c_str(),"w");
    if(out)std::fprintf(out,"host_ns,call_ns,stream_us,hardware_us,audio_consumed\n");
    while(!interrupted&&(hostNs()-start)/1e9<seconds){
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const uint32_t before=feed(9600);(void)before;
        const double t0=hostNs();BMDTimeValue stream=0,hardware=0,inFrame=0,ticks=0;double speed=0;uint32_t buffered=0;
        const bool ok=output->GetScheduledStreamTime(1000000,&stream,&speed)==S_OK&&output->GetHardwareReferenceClock(1000000,&hardware,&inFrame,&ticks)==S_OK&&output->GetBufferedAudioSampleFrameCount(&buffered)==S_OK;
        const double t1=hostNs();
        if(!ok||stream<=0)continue;
        Sample s{(t0+t1)/2,t1-t0,stream,hardware,written-int64_t(buffered)};samples.push_back(s);
        if(out)std::fprintf(out,"%.0f,%.0f,%lld,%lld,%lld\n",s.host,s.callNs,(long long)s.stream,(long long)s.hardware,(long long)s.consumed);
        const double elapsed=(t1-start)/1e9;
        if(elapsed>=nextReport&&samples.size()>100){auto v=fit(samples,1e6,&Sample::stream);std::printf("  %4.0f s: stream clock %+7.2f ppm · frames %llu late %llu dropped %llu\n",elapsed,v.ppm,(unsigned long long)callback.completed.load(),(unsigned long long)callback.late.load(),(unsigned long long)callback.dropped.load());std::fflush(stdout);nextReport+=60;}
    }
    interrupted=true;
    output->StopScheduledPlayback(0,nullptr,scale);output->DisableAudioOutput();output->SetScheduledFrameCompletionCallback(nullptr);
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
    std::printf("Implied drift of the current SDI audio mapping: %+.1f ms per hour\n",au.ppm*3.6);
}
