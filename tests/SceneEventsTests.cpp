#include "core/Scene.hpp"
#include "core/MediaClock.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace u2k;
#define REQUIRE(x) do{if(!(x))throw std::runtime_error(#x);}while(0)
int main(){try{
 auto source=std::make_shared<LatestFrame>();
 auto first=std::make_shared<Frame>(Size{1,1});first->pixels={10,20,30,128};
 auto second=std::make_shared<Frame>(Size{1,1});second->pixels={20,30,40,128};
 REQUIRE(composite({nullptr,first},{1,1})==first);
 REQUIRE(composite({first},{2,2})!=first);
 auto signal=std::make_shared<FrameSignal>();
 {
  Scene scene;scene.frames.subscribe(signal);
  auto wait=[&](auto predicate){std::unique_lock<std::mutex> lock(signal->mutex);return signal->cv.wait_for(lock,std::chrono::seconds(2),predicate);};
  source->publish(first);scene.configure({{source,true}},{1,1},{1,1});
  REQUIRE(wait([&]{return scene.frames.get()==first;}));
  source->publish(second);REQUIRE(wait([&]{return scene.frames.get()==second;}));
  scene.configure({{source,false}},{1,1},{1,1});
  REQUIRE(wait([&]{auto f=scene.frames.get();return f&&f->pixels[3]==0;}));
  std::unique_lock<std::mutex> lock(signal->mutex);const auto observed=signal->revision;lock.unlock();
  scene.configure({{source,false}},{1,1},{1,1});source->publish(first);
  lock.lock();REQUIRE(!signal->cv.wait_for(lock,std::chrono::milliseconds(50),[&]{return signal->revision!=observed;}));
 }
 // A layer change on air keeps the delayed SDI timeline covered: the old composition stays
 // selectable before the change and the new one starts at a barrier. A size change clears it.
 {
  auto a=std::make_shared<LatestFrame>(),b=std::make_shared<LatestFrame>();Scene scene;scene.frames.subscribe(signal);
  auto wait=[&](auto predicate){std::unique_lock<std::mutex> lock(signal->mutex);return signal->cv.wait_for(lock,std::chrono::seconds(2),predicate);};
  auto published=[&](int64_t since,Size size){return wait([&]{auto f=scene.frames.history.at(mediaSampleTime());return f.frame&&f.time>=since&&f.frame->size==size;});};
  scene.configure({{a,true},{b,true}},{1,1},{1,1});
  auto shown=std::make_shared<Frame>(*first);shown->paintTime=mediaSampleTime();a->publish(shown);
  REQUIRE(wait([&]{return scene.frames.get()==shown;}));
  // Stamps are 48 kHz samples: keep the change strictly after the observed instant.
  const auto before=mediaSampleTime();std::this_thread::sleep_for(std::chrono::milliseconds(2));
  scene.configure({{a,true},{b,false}},{1,1},{1,1});REQUIRE(published(before,{1,1}));
  const auto old=scene.frames.history.at(before);REQUIRE(old.frame==shown);REQUIRE(old.time<=before);
  scene.configure({{a,true},{b,false}},{2,2},{2,2});REQUIRE(published(before,{2,2}));REQUIRE(!scene.frames.history.at(before).frame);
  scene.frames.unsubscribe(signal);
 }
 // History chooses by media time, never a newer frame; static content spans long gaps.
 FrameHistory history;history.configure(3,12,100);
 first->paintTime=100;second->paintTime=200;history.publish(first,100);history.publish(second,200);
 REQUIRE(!history.at(99).frame);REQUIRE(history.at(199).frame==first);REQUIRE(history.at(200).frame==second);
 REQUIRE(history.at(160,50).frame==second); // Nearest available future frame is bounded.
 REQUIRE(history.at(150,50).frame==first); // Exact ties prefer the past.
 REQUIRE(history.at(160,20).frame==first); // Never exceed permitted anticipation.
 first->contentTime=50;auto resized=resize(*first,{2,2});REQUIRE(resized->paintTime==100);REQUIRE(resized->contentTime==50);
 auto copied=Frame::copy(first->size,first->pixels.data(),first->stride,1,100,50);REQUIRE(copied->paintTime==100);REQUIRE(copied->contentTime==50);
 REQUIRE(composite({first},{1,1})->contentTime==50);
 auto mixed=composite({first,second},{1,1});REQUIRE(mixed->paintTime==200);REQUIRE(mixed->contentTime==INT64_MIN);
 history.publish(first,10000);REQUIRE(history.at(9999).frame==second);
 auto third=std::make_shared<Frame>(Size{1,1});third->paintTime=10020;history.publish(third,10020);
 REQUIRE(history.bytes()<=12);REQUIRE(history.at(10019).frame==first);
 history.publish(second,50);REQUIRE(history.at(10020).frame==third); // Late publication cannot rewind history.
 history.clear();REQUIRE(!history.at(20000).frame);
 history.configure(16,4);history.publish(first,1);history.publish(second,2);
 REQUIRE(history.bytes()==4);REQUIRE(!history.at(1).frame);REQUIRE(history.at(2).frame==second);
 history.publish(resized,3);REQUIRE(history.bytes()==0); // Oversize image cannot defeat the budget.
 // SDR preparation is immutable, format-specific, budgeted and removable at STOP.
 history.configure(3,12);history.publish(first,100);history.publish(second,200);
 history.prepareOutput(false,32);auto prepared=history.at(100);REQUIRE(prepared.prepared);REQUIRE(history.bytes()==16);
 std::vector<uint8_t> expected(4);toSDR(*first,expected.data(),4,true,false);REQUIRE(*prepared.prepared==expected);
 history.prepareOutput(true,32);toSDR(*first,expected.data(),4,true,true);REQUIRE(*history.at(100).prepared==expected);
 REQUIRE(*prepared.prepared!=expected); // Reader retains the prior immutable BGRA representation.
 history.prepareOutput(std::nullopt,12);REQUIRE(!history.at(100).prepared);REQUIRE(history.bytes()==8);
 // Changing the frame-count limit keeps existing content and the byte budget.
 FrameHistory retained;retained.configure(2,32);retained.publish(first,100);retained.publish(second,200);
 retained.retainFrames(4);retained.publish(first,300);retained.publish(second,400);REQUIRE(retained.at(100).frame==first);
 retained.retainFrames(2);REQUIRE(!retained.at(100).frame);REQUIRE(retained.at(300).frame==first);
 retained.prepareOutput(false,32);retained.retainFrames(20);for(int i=0;i<20;++i)retained.publish(first,500+i*100);REQUIRE(retained.bytes()<=32);
 bool invalidLimit=false;try{retained.retainFrames(0);}catch(const std::invalid_argument&){invalidLimit=true;}REQUIRE(invalidLimit);
 // Selection, bounded publication and clear are safe across independent threads.
 FrameHistory concurrentHistory;concurrentHistory.configure(8,32);concurrentHistory.prepareOutput(false,64);std::atomic<bool> done{false},bad{false};
 std::thread historyWriter([&]{for(int i=0;i<5000;++i)concurrentHistory.publish(i%2?first:second,i);done=true;});
 std::thread historyReader([&]{while(!done){auto selected=concurrentHistory.at(2500);if(selected.frame&&selected.time>2500)bad=true;if(concurrentHistory.bytes()>64)bad=true;}});
 for(int i=0;i<500;++i){concurrentHistory.retainFrames(8+size_t(i%13));concurrentHistory.clear();concurrentHistory.prepareOutput(i%3?std::optional<bool>(bool(i%2)):std::nullopt,64);}historyWriter.join();historyReader.join();REQUIRE(!bad);
 // Visual identifiers survive decoding thresholds and reject wrong geometry/checksums.
 auto coded=std::make_shared<Frame>(Size{1920,1080});for(size_t i=3;i<coded->pixels.size();i+=4)coded->pixels[i]=255;
 const int code=1234;const uint64_t word=(uint64_t(0xa65c)<<24)|(uint64_t(code)<<12)|((code^0xb6d)&0xfff);
 for(int b=0;b<40;++b)for(int row=0;row<2;++row){auto p=coded->pixels.data()+size_t(72+row*12)*coded->stride+(114+b*36)*4;for(int c=0;c<3;++c)p[c]=(word>>(39-b))&1?250:5;}
 REQUIRE(referenceFrameId(coded->pixels.data(),1920,1080,coded->stride)==code);REQUIRE(!referenceFrameId(coded->pixels.data(),1280,720,coded->stride));
 coded->pixels[size_t(72)*coded->stride+114*4]=128;coded->pixels[size_t(72)*coded->stride+114*4+1]=128;coded->pixels[size_t(72)*coded->stride+114*4+2]=128;REQUIRE(!referenceFrameId(coded->pixels.data(),1920,1080,coded->stride));
 // Publication after subscriber destruction must never access a dead Scene.
 source->publish(second);source->clear();
 // Race publication with reconfiguration and Scene destruction.
 std::atomic<bool> stop{false};std::thread publisher([&]{while(!stop.load()){source->publish(first);std::this_thread::yield();}});
 {
  Scene scene;
  for(int i=0;i<1000;++i)scene.configure({{source,bool(i%2)}},{1,1},{1,1});
 }
 stop=true;publisher.join();
 std::cout<<"PASS: event-driven composition, single-layer alias, hidden sources and observer lifetime\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
