#include "Scene.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace u2k {
void AudioLane::push(const float* const* data,int n,int channels,int64_t pts){
 if(pts>INT64_MAX/48||pts<INT64_MIN/48)return;
 if(!data||n<1||n>48000||channels<1||channels>8)return;
 AudioPacket p;p.expectedForOutput=outputExpected.load(std::memory_order_relaxed);p.start=pts*48;latestTimestamp=p.start;p.samples.resize(size_t(n)*2);uint64_t signal=0;float peaks[2]={0,0};
 for(int i=0;i<n;i++)for(int c=0;c<2;c++){float v=data[std::min(c,channels-1)][i];v=std::isfinite(v)?v:0;p.samples[i*2+c]=v;peaks[c]=std::max(peaks[c],std::abs(v));if(std::abs(v)>.00001f)++signal;}
 // Capture source peaks before gain/mute; reading a meter never consumes audio packets.
 auto retain=[](std::atomic<float>& target,float value){float old=target.load(std::memory_order_relaxed);while(old<value&&!target.compare_exchange_weak(old,value,std::memory_order_relaxed)){};};
 retain(peakLeft_,peaks[0]);retain(peakRight_,peaks[1]);
 nonZero+=signal;
 std::lock_guard<std::mutex> lock(mutex_);if(!packets_.empty()&&p.start<packets_.back().start){packets_.clear();meterPackets_.clear();}
 meterPackets_.push_back(p);
 while(meterPackets_.size()>64||(!meterPackets_.empty()&&meterPackets_.back().start-meterPackets_.front().start>24000))meterPackets_.pop_front();
 packets_.push_back(std::move(p));received+=n;
 while(packets_.size()>64||(!packets_.empty()&&packets_.back().start-packets_.front().start>24000)){
  if(packets_.front().expectedForOutput&&outputExpected.load(std::memory_order_relaxed))++outputOverruns;else ++unusedExpired;
  packets_.pop_front();++discarded;
 }
}
std::pair<float,float> AudioLane::takePeaks(){return {peakLeft_.exchange(0,std::memory_order_relaxed),peakRight_.exchange(0,std::memory_order_relaxed)};}
void AudioLane::clear(){std::lock_guard<std::mutex> lock(mutex_);packets_.clear();meterPackets_.clear();peakLeft_=0;peakRight_=0;}
void AudioLane::mix(int64_t start,size_t n,float gain,std::vector<float>& out,bool mono){sum(start,n,gain,out,true,mono);}
void AudioLane::sample(int64_t start,size_t n,float gain,std::vector<float>& out,bool mono){sum(start,n,gain,out,false,mono);}
void AudioLane::sum(int64_t start,size_t n,float gain,std::vector<float>& out,bool prune,bool mono){
 std::lock_guard<std::mutex> lock(mutex_);
 while(prune&&!packets_.empty()&&packets_.front().start+int64_t(packets_.front().samples.size()/2)<=start)packets_.pop_front();
 const auto& history=prune?packets_:meterPackets_;
 for(const auto& p:history){auto first=std::max(start,p.start),last=std::min(start+int64_t(n),p.start+int64_t(p.samples.size()/2));for(auto i=first;i<last;++i){const size_t src=size_t(i-p.start)*2,dst=size_t(i-start)*2;if(mono){const float value=(p.samples[src]*.5f+p.samples[src+1]*.5f)*gain;out[dst]+=value;out[dst+1]+=value;}else for(int c=0;c<2;c++)out[dst+c]+=p.samples[src+c]*gain;}}
}
static AudioLevels measureAudio(const std::vector<float>& samples){
 AudioLevels levels;double powerLeft=0,powerRight=0;const size_t frames=samples.size()/2;if(!frames)return levels;
 for(size_t i=0;i<frames;++i){const float l=std::clamp(samples[i*2],-1.f,1.f),r=std::clamp(samples[i*2+1],-1.f,1.f);powerLeft+=double(l)*l;powerRight+=double(r)*r;levels.peakLeft=std::max(levels.peakLeft,std::abs(l));levels.peakRight=std::max(levels.peakRight,std::abs(r));}
 levels.rmsLeft=float(std::sqrt(powerLeft/frames));levels.rmsRight=float(std::sqrt(powerRight/frames));return levels;
}
AudioLevels AudioLane::sourceLevels(int64_t start,size_t n,bool mono){if(n>48000)throw std::invalid_argument("Audio meter block exceeds one second");std::vector<float> samples(n*2,0);sample(start,n,1,samples,mono);return measureAudio(samples);}
void AudioMixer::inputs(std::vector<AudioInput> v){std::lock_guard<std::mutex> lock(mutex_);inputs_=std::move(v);}
std::vector<int32_t> AudioMixer::mix(int64_t start,size_t n){
 lastMixTimestamp=start;
 if(n>48000)throw std::invalid_argument("Audio block exceeds one second");
 std::vector<AudioInput> inputs;{std::lock_guard<std::mutex> lock(mutex_);inputs=inputs_;}
 std::vector<float> buffer(n*2,0);if(enabled&&!muted)for(auto& i:inputs)if(i.enabled&&i.lane)i.lane->mix(start,n,std::clamp(i.gain,0.f,2.f),buffer,i.mono);
 uint64_t limited=0,audible=0;std::vector<int32_t> pcm(n*2);for(size_t i=0;i<pcm.size();++i){double v=buffer[i];if(std::abs(v)>1)++limited;pcm[i]=int32_t(std::clamp(v,-1.,1.)*2147483647.);if(pcm[i])++audible;}clipped+=limited;nonSilent+=audible;return pcm;
}
std::pair<float,float> AudioMixer::sourcePeaks(int64_t start,size_t n){const auto levels=sourceLevels(start,n);return {levels.peakLeft,levels.peakRight};}
AudioLevels AudioMixer::sourceLevels(int64_t start,size_t n){
 if(n>48000)throw std::invalid_argument("Audio meter block exceeds one second");
 std::vector<AudioInput> inputs;{std::lock_guard<std::mutex> lock(mutex_);inputs=inputs_;}
 std::vector<float> samples(n*2,0);for(auto& input:inputs)if(input.enabled&&input.lane)input.lane->sample(start,n,std::clamp(input.gain,0.f,2.f),samples,input.mono);
 return measureAudio(samples);
}
std::shared_ptr<const Frame> composite(const std::vector<std::shared_ptr<const Frame>>& layers,Size size){
 auto out=std::make_shared<Frame>(size);
 for(auto it=layers.rbegin();it!=layers.rend();++it){if(!*it)continue;auto src=(*it)->size==size?*it:resize(**it,size);
  for(size_t i=0;i<out->pixels.size();i+=4){const int a=src->pixels[i+3];if(!a)continue;if(a==255){std::memcpy(out->pixels.data()+i,src->pixels.data()+i,4);continue;}for(int c=0;c<4;c++)out->pixels[i+c]=uint8_t(std::min(255,int(src->pixels[i+c])+(int(out->pixels[i+c])*(255-a)+127)/255));}
 }return out;
}
Scene::Scene(){worker_=std::thread([this]{run();});}
Scene::~Scene(){{std::lock_guard<std::mutex> lock(mutex_);quit_=true;}cv_.notify_one();worker_.join();}
void Scene::configure(std::vector<ImageInput> v,Size viewport,Size target){validate(viewport);validate(target);std::lock_guard<std::mutex> lock(mutex_);layers_=std::move(v);viewport_=viewport;target_=target;++revision_;cv_.notify_one();}
void Scene::run(){uint64_t previous=UINT64_MAX;std::vector<std::shared_ptr<const Frame>> held;
 for(;;){std::unique_lock<std::mutex> lock(mutex_);cv_.wait_for(lock,std::chrono::milliseconds(16));if(quit_)return;auto layers=layers_;auto viewport=viewport_,target=target_;auto revision=revision_;lock.unlock();
  std::vector<std::shared_ptr<const Frame>> next;for(auto& l:layers)next.push_back(l.visible?l.frames->get():nullptr);if(next==held&&revision==previous)continue;
  try{auto image=composite(next,viewport);frames.publish(viewport==target?image:resize(*image,target));++composed;}catch(...){frames.clear();}held=std::move(next);previous=revision;
 }
}
}
