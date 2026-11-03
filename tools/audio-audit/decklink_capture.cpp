// Read-only SDI input probe. Does not enable, stop or disable any output resources.
// Captures timestamped centre-patch luminance and stereo pulse onsets in memory.
#include "DeckLinkAPI.h"
#include <CoreFoundation/CoreFoundation.h>
#include <mach/mach_time.h>
#include <optional>
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
static int64_t hostSamples(){static const auto tb=[] {mach_timebase_info_data_t t;mach_timebase_info(&t);return t;}();return int64_t((static_cast<long double>(mach_absolute_time())*tb.numer/tb.denom)*48000/1000000000);}
static std::optional<int> frameId(const unsigned char* bytes,int width,int height,int stride){
 if(width!=1920||height!=1080)return {};uint64_t bits=0;
 for(int b=0;b<40;++b){int values[2];for(int row=0;row<2;++row){const auto y=bytes[size_t(72+row*12)*stride+(114+b*36)*2+1];if(y>192)values[row]=1;else if(y<64)values[row]=0;else return {};}if(values[0]!=values[1])return {};bits=(bits<<1)|values[0];}
 const int id=int((bits>>12)&4095);if(bits>>24!=0xa65c||id>=3000||(bits&4095)!=uint64_t((id^0xb6d)&4095))return {};return id;
}
struct Row {const char* kind;int64_t pts,duration;double value;int64_t host;};
class Capture final:public IDeckLinkInputCallback {
 int64_t callbackHost_=0;
 std::atomic<ULONG> refs_{1};bool armed_[2]={true,true};int quiet_[2]={0,0};
public:
 std::mutex mutex;std::vector<Row> rows;uint64_t noSignal=0,errors=0,overflow=0,audioFrames=0,emptyAudioPackets=0;bool accepting=true,recording=false,colorStats=false,colorsSampled=false;uint64_t consecutiveVideo=0;bool sawAudio=false;
 void beginRecording(){std::lock_guard<std::mutex> lock(mutex);rows.clear();noSignal=errors=overflow=audioFrames=emptyAudioPackets=0;armed_[0]=armed_[1]=true;quiet_[0]=quiet_[1]=0;colorsSampled=false;recording=true;}
 Capture(){rows.reserve(30000);}
 HRESULT QueryInterface(REFIID iid,LPVOID* p)override{if(!p)return E_INVALIDARG;*p=nullptr;const auto unknown=CFUUIDGetUUIDBytes(IUnknownUUID);if(!std::memcmp(&iid,&unknown,sizeof(iid))||!std::memcmp(&iid,&IID_IDeckLinkInputCallback,sizeof(iid))){*p=static_cast<IDeckLinkInputCallback*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
 ULONG AddRef()override{return ++refs_;}ULONG Release()override{auto n=--refs_;if(!n)delete this;return n;}
 void add(const char* kind,int64_t pts,int64_t duration,double value){if(rows.size()<rows.capacity())rows.push_back({kind,pts,duration,value,callbackHost_});else ++overflow;}
 HRESULT VideoInputFormatChanged(BMDVideoInputFormatChangedEvents,IDeckLinkDisplayMode*,BMDDetectedVideoInputFormatFlags)override{std::lock_guard<std::mutex> lock(mutex);++errors;return S_OK;}
 HRESULT VideoInputFrameArrived(IDeckLinkVideoInputFrame* video,IDeckLinkAudioInputPacket* audio)override{
  const auto arrival=hostSamples();std::lock_guard<std::mutex> lock(mutex);callbackHost_=arrival;if(!accepting)return S_OK;
  if(video){if(video->GetFlags()&bmdFrameHasNoInputSource)consecutiveVideo=0;else ++consecutiveVideo;}if(audio&&audio->GetSampleFrameCount()>0)sawAudio=true;
  if(!recording)return S_OK;
  if(video){
   if(video->GetFlags()&bmdFrameHasNoInputSource){++noSignal;BMDTimeValue pts=-1,d=0;video->GetStreamTime(&pts,&d,48000);add("no_signal",pts,d,0);}
   else {
    BMDTimeValue pts=0,duration=0;IDeckLinkVideoBuffer* buffer=nullptr;
    if(video->GetPixelFormat()!=bmdFormat8BitYUV||video->GetStreamTime(&pts,&duration,48000)!=S_OK||video->QueryInterface(IID_IDeckLinkVideoBuffer,reinterpret_cast<void**>(&buffer))!=S_OK)++errors;
    else {if(buffer->StartAccess(bmdBufferAccessRead)!=S_OK)++errors;else{
      void* bytes=nullptr;if(buffer->GetBytes(&bytes)!=S_OK||!bytes)++errors;else{
       const int w=int(video->GetWidth()),h=int(video->GetHeight());double sum=0,u=0,v=0;int count=0;
       // Patch wholly inside the diagnostic flash, away from captions.
       for(int y=h/2-4;y<h/2+4;++y)for(int x=w/2-4;x<w/2+4;++x){auto row=static_cast<unsigned char*>(bytes)+y*video->GetRowBytes();sum+=row[x*2+1];if(colorStats){u+=row[(x/2)*4];v+=row[(x/2)*4+2];}++count;}
       add("video",pts,duration,sum/count);if(colorStats){add("chroma_U",pts,duration,u/count);add("chroma_V",pts,duration,v/count);}if(colorStats&&!colorsSampled){
        static const char* names[8][3]={{"bar0_Y","bar0_U","bar0_V"},{"bar1_Y","bar1_U","bar1_V"},{"bar2_Y","bar2_U","bar2_V"},{"bar3_Y","bar3_U","bar3_V"},{"bar4_Y","bar4_U","bar4_V"},{"bar5_Y","bar5_U","bar5_V"},{"bar6_Y","bar6_U","bar6_V"},{"bar7_Y","bar7_U","bar7_V"}};
        for(int i=0;i<8;++i){const int x=int(w*(.04+.92*(i+.5)/8));const auto row=static_cast<unsigned char*>(bytes)+(h/2)*video->GetRowBytes();add(names[i][0],pts,duration,row[x*2+1]);add(names[i][1],pts,duration,row[(x/2)*4]);add(names[i][2],pts,duration,row[(x/2)*4+2]);}colorsSampled=true;
       }if(auto id=frameId(static_cast<unsigned char*>(bytes),w,h,video->GetRowBytes()))add("video_id",pts,duration,*id);
      }if(buffer->EndAccess(bmdBufferAccessRead)!=S_OK)++errors;
     }buffer->Release();}
   }
  }
  // Some input reacquisitions deliver bursts of empty packets; count them, do not log rows.
  if(audio&&audio->GetSampleFrameCount()==0){++emptyAudioPackets;audio=nullptr;}
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
  for(int i=1;i<argc;i+=2){if(i+1>=argc)throw std::runtime_error("Missing option value");const std::string key=argv[i];if(key=="--seconds")seconds=std::stod(argv[i+1]);else if(key=="--mode")mode=argv[i+1];else if(key=="--csv")csv=argv[i+1];else if(key=="--color-stats"){const std::string value=argv[i+1];if(value!="0"&&value!="1")throw std::runtime_error("Use --color-stats 0|1");capture->colorStats=value=="1";}else throw std::runtime_error("Unknown option");}
  if(!std::isfinite(seconds)||seconds<1||seconds>300||csv.empty())throw std::runtime_error("Use --seconds 1..300 --mode 1080p25|1080p50|1080p5994 --csv path");
  BMDDisplayMode display=mode=="1080p25"?bmdModeHD1080p25:mode=="1080p50"?bmdModeHD1080p50:mode=="1080p5994"?bmdModeHD1080p5994:bmdModeUnknown;
  if(display==bmdModeUnknown)throw std::runtime_error("Invalid mode");
  std::signal(SIGINT,[](int){interrupted=true;});std::signal(SIGTERM,[](int){interrupted=true;});
  auto it=CreateDeckLinkIteratorInstance();if(!it)throw std::runtime_error("Desktop Video unavailable");
  while(it->Next(&device)==S_OK){if(device->QueryInterface(IID_IDeckLinkInput,reinterpret_cast<void**>(&input))==S_OK)break;device->Release();device=nullptr;}it->Release();if(!input)throw std::runtime_error("No input device");
  check(input->EnableVideoInput(display,bmdFormat8BitYUV,bmdVideoInputFlagDefault),"EnableVideoInput");video=true;
  check(input->EnableAudioInput(bmdAudioSampleRate48kHz,bmdAudioSampleType32bitInteger,2),"EnableAudioInput");audio=true;
  check(input->SetCallback(capture),"SetCallback");callback=true;check(input->StartStreams(),"StartStreams");started=true;
  // Let the SDI receiver lock before establishing the measurement timeline.
  const auto lockDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(6);bool locked=false;
  const auto needed=mode=="1080p25"?25u:mode=="1080p50"?50u:60u;
  while(!interrupted&&std::chrono::steady_clock::now()<lockDeadline){
   {std::lock_guard<std::mutex> lock(capture->mutex);locked=capture->consecutiveVideo>=needed&&capture->sawAudio;}
   if(locked)break;std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  if(!locked)throw std::runtime_error("SDI input failed to lock for one second with audio");
  check(input->StopStreams(),"Stop lock acquisition");started=false;check(input->FlushStreams(),"Flush lock acquisition");capture->beginRecording();
  check(input->StartStreams(),"Start locked capture");started=true;
  std::fprintf(stderr,"READY SDI capture %s, %.0f seconds\n",mode.c_str(),seconds);std::fflush(stderr);
  const auto start=std::chrono::steady_clock::now();while(!interrupted&&std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<seconds)std::this_thread::sleep_for(std::chrono::milliseconds(50));
  result=0;
 }catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());}
 if(started){std::lock_guard<std::mutex> lock(capture->mutex);capture->recording=false;}
 if(started&&input->StopStreams()!=S_OK)result=1;
 if(callback&&input->SetCallback(nullptr)!=S_OK)result=1;
 {std::lock_guard<std::mutex> lock(capture->mutex);capture->accepting=false;
  if(started){auto file=std::fopen(csv.c_str(),"w");if(!file){std::perror("CSV");result=1;}else{
   std::fprintf(file,"kind,pts_48k,duration_48k,value,host_48k\n");for(const auto& r:capture->rows)std::fprintf(file,"%s,%lld,%lld,%.9f,%lld\n",r.kind,(long long)r.pts,(long long)r.duration,r.value,(long long)r.host);
   if(std::ferror(file))result=1;if(std::fclose(file))result=1;
  }}
  std::fprintf(stderr,"rows=%zu audio_frames=%llu no_signal=%llu errors=%llu overflow=%llu empty_audio_packets=%llu\n",capture->rows.size(),capture->audioFrames,capture->noSignal,capture->errors,capture->overflow,capture->emptyAudioPackets);
  if(capture->errors||capture->overflow||!capture->audioFrames)result=1;
 }
 if(audio&&input->DisableAudioInput()!=S_OK)result=1;if(video&&input->DisableVideoInput()!=S_OK)result=1;
 if(input)input->Release();if(device)device->Release();capture->Release();return result;
}
