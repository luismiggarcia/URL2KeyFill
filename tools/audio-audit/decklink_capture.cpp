// Read-only SDI input probe. Does not enable, stop or disable any output resources.
// Captures timestamped centre-patch luminance and stereo pulse onsets in memory.
#include "DeckLinkAPI.h"
#include <CoreFoundation/CoreFoundation.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
static std::atomic<bool> interrupted{false};
static void check(HRESULT r,const char* what){if(r!=S_OK)throw std::runtime_error(std::string(what)+" HRESULT="+std::to_string(r));}
struct Row {const char* kind;int64_t pts,duration;double value;};
class Capture final:public IDeckLinkInputCallback {
 std::atomic<ULONG> refs_{1};bool armed_[2]={true,true};int quiet_[2]={0,0};
public:
 std::mutex mutex;std::vector<Row> rows;uint64_t noSignal=0,errors=0,overflow=0,audioFrames=0;bool accepting=true;
 Capture(){rows.reserve(30000);}
 HRESULT QueryInterface(REFIID iid,LPVOID* p)override{if(!p)return E_INVALIDARG;*p=nullptr;const auto unknown=CFUUIDGetUUIDBytes(IUnknownUUID);if(!std::memcmp(&iid,&unknown,sizeof(iid))||!std::memcmp(&iid,&IID_IDeckLinkInputCallback,sizeof(iid))){*p=static_cast<IDeckLinkInputCallback*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
 ULONG AddRef()override{return ++refs_;}ULONG Release()override{auto n=--refs_;if(!n)delete this;return n;}
 void add(const char* kind,int64_t pts,int64_t duration,double value){if(rows.size()<rows.capacity())rows.push_back({kind,pts,duration,value});else ++overflow;}
 HRESULT VideoInputFormatChanged(BMDVideoInputFormatChangedEvents,IDeckLinkDisplayMode*,BMDDetectedVideoInputFormatFlags)override{std::lock_guard<std::mutex> lock(mutex);++errors;return S_OK;}
 HRESULT VideoInputFrameArrived(IDeckLinkVideoInputFrame* video,IDeckLinkAudioInputPacket* audio)override{
  std::lock_guard<std::mutex> lock(mutex);if(!accepting)return S_OK;
  if(video){
   if(video->GetFlags()&bmdFrameHasNoInputSource)++noSignal;
   else {
    BMDTimeValue pts=0,duration=0;IDeckLinkVideoBuffer* buffer=nullptr;
    if(video->GetPixelFormat()!=bmdFormat8BitYUV||video->GetStreamTime(&pts,&duration,48000)!=S_OK||video->QueryInterface(IID_IDeckLinkVideoBuffer,reinterpret_cast<void**>(&buffer))!=S_OK)++errors;
    else {if(buffer->StartAccess(bmdBufferAccessRead)!=S_OK)++errors;else{
      void* bytes=nullptr;if(buffer->GetBytes(&bytes)!=S_OK||!bytes)++errors;else{
       const int w=int(video->GetWidth()),h=int(video->GetHeight());double sum=0;int count=0;
       // Patch wholly inside the diagnostic flash, away from captions.
       for(int y=h/2-4;y<h/2+4;++y)for(int x=w/2-4;x<w/2+4;++x){sum+=static_cast<unsigned char*>(bytes)[y*video->GetRowBytes()+x*2+1];++count;}
       add("video",pts,duration,sum/count);
      }if(buffer->EndAccess(bmdBufferAccessRead)!=S_OK)++errors;
     }buffer->Release();}
   }
  }
  if(audio){BMDTimeValue pts=0;void* bytes=nullptr;const auto n=audio->GetSampleFrameCount();
   if(audio->GetPacketTime(&pts,48000)!=S_OK||audio->GetBytes(&bytes)!=S_OK||!bytes)++errors;
   else{auto pcm=static_cast<const int32_t*>(bytes);audioFrames+=n;double peaks[2]={0,0};
    for(long i=0;i<n;++i)for(int c=0;c<2;++c){const double value=std::abs(double(pcm[i*2+c]))/2147483648.;peaks[c]=std::max(peaks[c],value);
     if(value>.003){if(armed_[c])add(c?"audio_R":"audio_L",pts+i,0,value);armed_[c]=false;quiet_[c]=0;}
     else if(++quiet_[c]>=4800){armed_[c]=true;quiet_[c]=4800;}
    }
    add("packet",pts,n,std::max(peaks[0],peaks[1]));
   }
  }return S_OK;
 }
};
int main(int argc,char** argv){
 IDeckLink* device=nullptr;IDeckLinkInput* input=nullptr;Capture* capture=new Capture;bool video=false,audio=false,callback=false,started=false;int result=1;
 double seconds=30;std::string mode="1080p25",csv;
 try{
  for(int i=1;i<argc;i+=2){if(i+1>=argc)throw std::runtime_error("Missing option value");const std::string key=argv[i];if(key=="--seconds")seconds=std::stod(argv[i+1]);else if(key=="--mode")mode=argv[i+1];else if(key=="--csv")csv=argv[i+1];else throw std::runtime_error("Unknown option");}
  if(!std::isfinite(seconds)||seconds<1||seconds>300||csv.empty())throw std::runtime_error("Use --seconds 1..300 --mode 1080p25|1080p50|1080p5994 --csv path");
  BMDDisplayMode display=mode=="1080p25"?bmdModeHD1080p25:mode=="1080p50"?bmdModeHD1080p50:mode=="1080p5994"?bmdModeHD1080p5994:bmdModeUnknown;
  if(display==bmdModeUnknown)throw std::runtime_error("Invalid mode");
  std::signal(SIGINT,[](int){interrupted=true;});std::signal(SIGTERM,[](int){interrupted=true;});
  auto it=CreateDeckLinkIteratorInstance();if(!it)throw std::runtime_error("Desktop Video unavailable");
  while(it->Next(&device)==S_OK){if(device->QueryInterface(IID_IDeckLinkInput,reinterpret_cast<void**>(&input))==S_OK)break;device->Release();device=nullptr;}it->Release();if(!input)throw std::runtime_error("No input device");
  check(input->EnableVideoInput(display,bmdFormat8BitYUV,bmdVideoInputFlagDefault),"EnableVideoInput");video=true;
  check(input->EnableAudioInput(bmdAudioSampleRate48kHz,bmdAudioSampleType32bitInteger,2),"EnableAudioInput");audio=true;
  check(input->SetCallback(capture),"SetCallback");callback=true;check(input->StartStreams(),"StartStreams");started=true;
  std::fprintf(stderr,"READY SDI capture %s, %.0f seconds\n",mode.c_str(),seconds);std::fflush(stderr);
  const auto start=std::chrono::steady_clock::now();while(!interrupted&&std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<seconds)std::this_thread::sleep_for(std::chrono::milliseconds(50));
  result=0;
 }catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());}
 if(started&&input->StopStreams()!=S_OK)result=1;
 if(callback&&input->SetCallback(nullptr)!=S_OK)result=1;
 {std::lock_guard<std::mutex> lock(capture->mutex);capture->accepting=false;
  if(started){auto file=std::fopen(csv.c_str(),"w");if(!file){std::perror("CSV");result=1;}else{
   std::fprintf(file,"kind,pts_48k,duration_48k,value\n");for(const auto& r:capture->rows)std::fprintf(file,"%s,%lld,%lld,%.9f\n",r.kind,(long long)r.pts,(long long)r.duration,r.value);
   if(std::ferror(file))result=1;if(std::fclose(file))result=1;
  }}
  std::fprintf(stderr,"rows=%zu audio_frames=%llu no_signal=%llu errors=%llu overflow=%llu\n",capture->rows.size(),capture->audioFrames,capture->noSignal,capture->errors,capture->overflow);
  if(capture->errors||capture->overflow||!capture->audioFrames)result=1;
 }
 if(audio&&input->DisableAudioInput()!=S_OK)result=1;if(video&&input->DisableVideoInput()!=S_OK)result=1;
 if(input)input->Release();if(device)device->Release();capture->Release();return result;
}
