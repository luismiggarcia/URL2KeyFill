#include "Scene.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace u2k {
void AudioLane::push(const float* const* data,int n,int channels,int64_t pts){
 if(pts>INT64_MAX/48||pts<INT64_MIN/48)return;
 if(!data||n<1||n>48000||channels<1||channels>8)return;
 AudioPacket p;p.start=pts*48;latestTimestamp=p.start;p.samples.resize(size_t(n)*2);uint64_t signal=0;
 for(int i=0;i<n;i++)for(int c=0;c<2;c++){float v=data[std::min(c,channels-1)][i];p.samples[i*2+c]=std::isfinite(v)?v:0;if(std::abs(v)>.00001f)++signal;}
 nonZero+=signal;
 std::lock_guard<std::mutex> lock(mutex_);if(!packets_.empty()&&p.start<packets_.back().start)packets_.clear();
 packets_.push_back(std::move(p));received+=n;
 while(packets_.size()>64||(!packets_.empty()&&packets_.back().start-packets_.front().start>24000)){packets_.pop_front();++discarded;}
}
void AudioLane::clear(){std::lock_guard<std::mutex> lock(mutex_);packets_.clear();}
void AudioLane::mix(int64_t start,size_t n,float gain,std::vector<float>& out){
 std::lock_guard<std::mutex> lock(mutex_);
 while(!packets_.empty()&&packets_.front().start+int64_t(packets_.front().samples.size()/2)<=start)packets_.pop_front();
 for(const auto& p:packets_){auto first=std::max(start,p.start),last=std::min(start+int64_t(n),p.start+int64_t(p.samples.size()/2));for(auto i=first;i<last;++i)for(int c=0;c<2;c++)out[size_t(i-start)*2+c]+=p.samples[size_t(i-p.start)*2+c]*gain;}
}
void AudioMixer::inputs(std::vector<AudioInput> v){std::lock_guard<std::mutex> lock(mutex_);inputs_=std::move(v);}
std::vector<int32_t> AudioMixer::mix(int64_t start,size_t n){
 lastMixTimestamp=start;
 if(n>48000)throw std::invalid_argument("Audio block exceeds one second");
 std::vector<AudioInput> inputs;{std::lock_guard<std::mutex> lock(mutex_);inputs=inputs_;}
 std::vector<float> buffer(n*2,0);if(enabled)for(auto& i:inputs)if(i.enabled&&i.lane)i.lane->mix(start,n,std::clamp(i.gain,0.f,2.f),buffer);
 uint64_t limited=0,audible=0;std::vector<int32_t> pcm(n*2);for(size_t i=0;i<pcm.size();++i){double v=buffer[i];if(std::abs(v)>1)++limited;pcm[i]=int32_t(std::clamp(v,-1.,1.)*2147483647.);if(pcm[i])++audible;}clipped+=limited;nonSilent+=audible;return pcm;
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
