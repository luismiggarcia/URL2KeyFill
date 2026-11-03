#include "Scene.hpp"
#include "MediaClock.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace u2k {
void AudioLane::push(const float* const* data,int n,int channels,int64_t pts){
 if(pts<0||pts>(INT64_MAX-48048)/48)return;
 if(!data||n<1||n>48000||channels<1||channels>8)return;
 const auto arrival=mediaSampleTime();
 const auto capturedGeneration=generation.load(std::memory_order_acquire);
 AudioPacket p;p.expectedForOutput=outputExpected.load(std::memory_order_relaxed);p.start=pts*48;p.samples.resize(size_t(n)*2);uint64_t signal=0;float peaks[2]={0,0};
 for(int i=0;i<n;i++)for(int c=0;c<2;c++){float v=data[std::min(c,channels-1)][i];v=std::isfinite(v)?v:0;p.samples[i*2+c]=v;peaks[c]=std::max(peaks[c],std::abs(v));if(std::abs(v)>.00001f)++signal;}
 // Capture source peaks before gain/mute; reading a meter never consumes audio packets.
 auto retain=[](std::atomic<float>& target,float value){float old=target.load(std::memory_order_relaxed);while(old<value&&!target.compare_exchange_weak(old,value,std::memory_order_relaxed)){};};
 received+=n;
 std::lock_guard<std::mutex> lock(mutex_);
 if(generation.load(std::memory_order_relaxed)!=capturedGeneration){++stalePackets;return;}
 p.arrival=arrival;
 const auto rawStart=p.start;
 if(timelineReady_){
  // Identical millisecond stamps can be legitimate for sub-millisecond packets.
  if(rawStart==lastRawStart_&&n==lastFrames_&&n>=48){++duplicatePackets;return;}
  if(rawStart<lastRawStart_){++reorderedPackets;return;}
  const auto delta=rawStart-nextSample_;
  if(delta>=-48&&delta<=48){p.start=nextSample_;if(delta)++timestampAdjustments;}
  else if(delta<0){
   const auto overlap=uint64_t(nextSample_-rawStart);trimmedFrames+=std::min<uint64_t>(overlap,uint64_t(n));
   lastRawStart_=rawStart;lastFrames_=n;
   if(overlap>=uint64_t(n))return;
   p.offset=size_t(overlap);p.start=nextSample_;
  }else timestampGapFrames+=uint64_t(delta);
 }
 timelineReady_=true;lastRawStart_=rawStart;lastFrames_=n;nextSample_=p.start+int64_t(p.frames());latestTimestamp=p.start;
 retain(peakLeft_,peaks[0]);retain(peakRight_,peaks[1]);nonZero+=signal;
 if(referenceDiagnostic)for(size_t j=0;j<p.frames();++j)referenceClock.sample(int16_t(std::clamp<long>(std::lround(p.samples[(j+p.offset)*2]*32768.f),-32768,32767)),p.start+int64_t(j));
 if(syncDiagnostic)for(size_t j=0;j<p.frames();++j){const float value=std::abs(p.samples[(j+p.offset)*2]);if(value>.003f){if(syncQuiet_>=4800){syncPulsePts=p.start+int64_t(j);++syncPulseCount;}syncQuiet_=0;}else syncQuiet_=std::min(syncQuiet_+1,4800);}
 meterPackets_.push_back(p);
 while(meterPackets_.size()>64||(!meterPackets_.empty()&&meterPackets_.back().start-meterPackets_.front().start>24000))meterPackets_.pop_front();
 if(!sourceSeen_)firstSample_=p.start;packets_.push_back(std::move(p));sourceSeen_=true;
 while(packets_.size()>64||(!packets_.empty()&&packets_.back().start-packets_.front().start>24000)){
  if(packets_.front().expectedForOutput&&outputExpected.load(std::memory_order_relaxed))++outputOverruns;else ++unusedExpired;
  packets_.pop_front();++discarded;
 }
 // Mark publication after copies/pruning, immediately before releasing the lane lock.
 const auto published=mediaSampleTime();packets_.back().published=published;meterPackets_.back().published=published;
 const auto& ready=packets_.back();
 if(gap_.sequence&&gap_.generation==capturedGeneration&&!gap_.recoveryPublished&&gap_.newestEnd!=INT64_MIN&&gap_.newestEnd<gap_.end&&ready.start<=gap_.newestEnd&&nextSample_>=gap_.end){gap_.recoveryArrival=ready.arrival;gap_.recoveryPublished=published;gap_.recoveryStart=ready.start;gap_.recoveryEnd=nextSample_;}
}
std::pair<float,float> AudioLane::takePeaks(){return {peakLeft_.exchange(0,std::memory_order_relaxed),peakRight_.exchange(0,std::memory_order_relaxed)};}
void AudioLane::clear(){std::lock_guard<std::mutex> lock(mutex_);++generation;packets_.clear();meterPackets_.clear();sourceSeen_=false;timelineReady_=false;syncQuiet_=4800;syncPulsePts=INT64_MIN;referenceClock.clear();sourceHeadroom=INT64_MIN;peakLeft_=0;peakRight_=0;}
AudioReadWindow AudioLane::takeReadWindow(){std::lock_guard<std::mutex> lock(mutex_);auto result=readWindow_;result.gap=gap_;readWindow_={};return result;}
void AudioLane::mix(int64_t start,size_t n,float gain,std::vector<float>& out,bool mono,bool left,bool right){sum(start,n,gain,out,true,mono,left,right);}
void AudioLane::sample(int64_t start,size_t n,float gain,std::vector<float>& out,bool mono,bool left,bool right){sum(start,n,gain,out,false,mono,left,right);}
void AudioLane::sum(int64_t start,size_t n,float gain,std::vector<float>& out,bool prune,bool mono,bool left,bool right){
 std::lock_guard<std::mutex> lock(mutex_);
 while(prune&&!packets_.empty()&&packets_.front().start+int64_t(packets_.front().frames())<=start)packets_.pop_front();
 // A bounded backward phase correction may need packets already pruned by the
 // previous read cursor. Retained history is immutable and shared with meters.
 const bool retained=prune&&(packets_.empty()||start<packets_.front().start);
 const auto& history=prune&&!retained?packets_:meterPackets_;
 if(prune){
  int64_t coveredEnd=start,newestEnd=INT64_MIN;const AudioPacket* newest=nullptr;size_t covered=0;
  for(const auto& p:history){
   const int64_t end=p.start+int64_t(p.frames());if(end>newestEnd){newestEnd=end;newest=&p;}
   const auto first=std::max({start,p.start,coveredEnd}),last=std::min(start+int64_t(n),end);
   if(last>first){covered+=size_t(last-first);coveredEnd=last;}
  }
  ++readWindow_.reads;
  if(sourceSeen_){const auto padding=start<firstSample_?std::min<uint64_t>(n,uint64_t(firstSample_)-uint64_t(start)):0;preSourcePaddingFrames+=padding;const auto missing=n-covered-padding;missingFrames+=missing;readWindow_.missing+=missing;
   if(missing){const auto sequence=gap_.sequence+1;gap_={};gap_.sequence=sequence;gap_.generation=generation.load(std::memory_order_relaxed);gap_.missing=missing;gap_.start=start;gap_.end=start+int64_t(n);gap_.readHost=mediaSampleTime();gap_.newestEnd=newestEnd;if(newest){gap_.newestArrival=newest->arrival;gap_.newestPublished=newest->published;}}}else{++noSourceReads;++readWindow_.noSource;}
  // No packet is represented by reads/missingFrames, not a fictitious headroom.
  sourceHeadroom=newestEnd==INT64_MIN?INT64_MIN:newestEnd-(start+int64_t(n));
  if(newestEnd!=INT64_MIN){const auto h=newestEnd-(start+int64_t(n));++readWindow_.headroomCount;readWindow_.headroomSum+=h;readWindow_.headroomMin=std::min(readWindow_.headroomMin,h);readWindow_.headroomMax=std::max(readWindow_.headroomMax,h);}
  ++outputReads;
 }
 for(const auto& p:history){auto first=std::max(start,p.start),last=std::min(start+int64_t(n),p.start+int64_t(p.frames()));for(auto i=first;i<last;++i){const size_t src=(size_t(i-p.start)+p.offset)*2,dst=size_t(i-start)*2;if(mono){const float value=((left?p.samples[src]:0)*.5f+(right?p.samples[src+1]:0)*.5f)*gain;out[dst]+=value;out[dst+1]+=value;}else for(int c=0;c<2;c++)out[dst+c]+=(c==0?left:right)?p.samples[src+c]*gain:0;}}
}
static AudioLevels measureAudio(const std::vector<float>& samples){
 AudioLevels levels;double powerLeft=0,powerRight=0;const size_t frames=samples.size()/2;if(!frames)return levels;
 for(size_t i=0;i<frames;++i){const float l=std::clamp(samples[i*2],-1.f,1.f),r=std::clamp(samples[i*2+1],-1.f,1.f);powerLeft+=double(l)*l;powerRight+=double(r)*r;levels.peakLeft=std::max(levels.peakLeft,std::abs(l));levels.peakRight=std::max(levels.peakRight,std::abs(r));}
 levels.rmsLeft=float(std::sqrt(powerLeft/frames));levels.rmsRight=float(std::sqrt(powerRight/frames));return levels;
}
AudioLevels AudioLane::sourceLevels(int64_t start,size_t n,bool mono,bool left,bool right){if(n>48000)throw std::invalid_argument("Audio meter block exceeds one second");std::vector<float> samples(n*2,0);sample(start,n,1,samples,mono,left,right);return measureAudio(samples);}
void AudioMixer::inputs(std::vector<AudioInput> v){std::lock_guard<std::mutex> lock(mutex_);inputs_=std::move(v);}
std::vector<float> AudioMixer::mixFloat(int64_t start,size_t n,bool programActive){
 lastMixTimestamp=start;
 if(n>48000)throw std::invalid_argument("Audio block exceeds one second");
 std::vector<AudioInput> inputs;{std::lock_guard<std::mutex> lock(mutex_);inputs=inputs_;}
 std::vector<float> buffer(n*2,0);if(programActive&&enabled&&!muted)for(auto& i:inputs)if(i.enabled&&i.lane&&i.gain>0&&gain>0&&audioRouteEnabled(i.mono,i.left,i.right,mono,left,right))i.lane->mix(start,n,std::clamp(i.gain,0.f,2.f),buffer,i.mono,i.left,i.right);
 for(size_t j=0;j<n;j++){float l=buffer[j*2],r=buffer[j*2+1];if(mono)l=r=(l+r)*.5f;buffer[j*2]=left?l*gain:0;buffer[j*2+1]=right?r*gain:0;}
 return buffer;
}
std::vector<int32_t> AudioMixer::toPCM(const std::vector<float>& buffer){
 uint64_t limited=0,audible=0;std::vector<int32_t> pcm(buffer.size());for(size_t i=0;i<pcm.size();++i){double v=buffer[i];if(std::abs(v)>1)++limited;pcm[i]=int32_t(std::clamp(v,-1.,1.)*2147483647.);if(pcm[i])++audible;}clipped+=limited;nonSilent+=audible;return pcm;
}
std::vector<int32_t> AudioMixer::mix(int64_t start,size_t n,bool active){return toPCM(mixFloat(start,n,active));}
std::vector<int32_t> AudioMixer::mixResampled(double start,size_t n,double ratio,const AudioResampler& filter,bool active){
 if(!std::isfinite(start)||start<0||start>double(INT64_MAX/2)||!n||n>1024)throw std::invalid_argument("Invalid source position");
 const auto first=int64_t(std::floor(start))-AudioResampler::left;
 const auto last=int64_t(std::floor(start+double(n-1)*ratio))+AudioResampler::right+1;
 auto input=mixFloat(first,size_t(last-first),active);if(std::all_of(input.begin(),input.end(),[](float v){return v==0;}))return std::vector<int32_t>(n*2,0);std::vector<float> result(n*2);filter.render(input.data(),input.size()/2,start-first,ratio,result.data(),n);return toPCM(result);
}
std::pair<float,float> AudioMixer::sourcePeaks(int64_t start,size_t n){const auto levels=sourceLevels(start,n);return {levels.peakLeft,levels.peakRight};}
std::vector<float> AudioMixer::sample(int64_t start,size_t n,bool respectMute,bool applyMaster){
 if(n>48000)throw std::invalid_argument("Audio block exceeds one second");
 std::vector<AudioInput> inputs;{std::lock_guard<std::mutex> lock(mutex_);inputs=inputs_;}
 std::vector<float> samples(n*2,0);if(applyMaster&&respectMute&&(!enabled||muted))return samples;
 for(auto& input:inputs)if(input.enabled&&input.lane)input.lane->sample(start,n,std::clamp(input.gain,0.f,2.f),samples,input.mono,input.left,input.right);
 if(!applyMaster)return samples;
 for(size_t j=0;j<n;j++){float l=samples[j*2],r=samples[j*2+1];if(mono)l=r=(l+r)*.5f;samples[j*2]=left?l*gain:0;samples[j*2+1]=right?r*gain:0;}
 return samples;
}
AudioLevels AudioMixer::sourceLevels(int64_t start,size_t n){return measureAudio(sample(start,n,false));}
std::shared_ptr<const Frame> composite(const std::vector<std::shared_ptr<const Frame>>& layers,Size size){
 std::shared_ptr<const Frame> single;size_t visible=0;
 for(const auto& layer:layers)if(layer){single=layer;++visible;}
 if(visible==1&&single->size==size)return single;
 auto out=std::make_shared<Frame>(size);for(const auto& layer:layers)if(layer)out->paintTime=std::max(out->paintTime,layer->paintTime);
 for(auto it=layers.rbegin();it!=layers.rend();++it){if(!*it)continue;auto src=(*it)->size==size?*it:resize(**it,size);
  for(size_t i=0;i<out->pixels.size();i+=4){const int a=src->pixels[i+3];if(!a)continue;if(a==255){std::memcpy(out->pixels.data()+i,src->pixels.data()+i,4);continue;}for(int c=0;c<4;c++)out->pixels[i+c]=uint8_t(std::min(255,int(src->pixels[i+c])+(int(out->pixels[i+c])*(255-a)+127)/255));}
 }return out;
}
Scene::Scene(){frames.history.configure(16,192*1024*1024);worker_=std::thread([this]{run();});}
Scene::~Scene(){{std::lock_guard<std::mutex> lock(mutex_);quit_=true;for(auto& l:layers_)if(l.frames)l.frames->unsubscribe(signal_);}signal_->notify();worker_.join();}
void Scene::configure(std::vector<ImageInput> v,Size viewport,Size target){validate(viewport);validate(target);std::lock_guard<std::mutex> lock(mutex_);if(layers_==v&&viewport_==viewport&&target_==target)return;for(auto& l:layers_)if(l.frames)l.frames->unsubscribe(signal_);
 // Keep history across layer changes: SDI selects ~audio delay in the past, so clearing
 // would leave it without a frame (transparent) until the new composition ages. The
 // next composite is stamped "now", the barrier. Only a size change invalidates it.
 if(!(viewport_==viewport&&target_==target))frames.history.clear();layers_=std::move(v);for(auto& l:layers_)if(l.visible&&l.frames)l.frames->subscribe(signal_);viewport_=viewport;target_=target;++revision_;signal_->notify();}
void Scene::run(){uint64_t previous=UINT64_MAX;int64_t previousStamp=INT64_MIN;std::vector<std::shared_ptr<const Frame>> held;
 uint64_t observed=0;
 for(;;){std::unique_lock<std::mutex> wake(signal_->mutex);signal_->cv.wait(wake,[&]{return signal_->revision!=observed;});observed=signal_->revision;wake.unlock();
  std::unique_lock<std::mutex> lock(mutex_);if(quit_)return;auto layers=layers_;auto viewport=viewport_,target=target_;auto revision=revision_;lock.unlock();
  std::vector<std::shared_ptr<const Frame>> next;for(auto& l:layers)next.push_back(l.visible?l.frames->get():nullptr);if(next==held&&revision==previous)continue;
  try{auto image=composite(next,viewport);auto result=viewport==target?image:resize(*image,target);std::lock_guard<std::mutex> current(mutex_);if(quit_)return;if(revision!=revision_)continue;const auto stamp=std::max(previousStamp,(revision!=previous||(result->contentTime==INT64_MIN&&result->paintTime==INT64_MIN))?mediaSampleTime():(result->contentTime!=INT64_MIN?result->contentTime:result->paintTime));previousStamp=stamp;frames.publish(std::move(result),stamp);++composed;}catch(...){frames.clear();}held=std::move(next);previous=revision;
 }
}
}
