#include "core/Scene.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
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
 // History chooses by media time, never a newer frame; static content spans long gaps.
 FrameHistory history;history.configure(3,12,100);
 first->paintTime=100;second->paintTime=200;history.publish(first,100);history.publish(second,200);
 REQUIRE(!history.at(99).frame);REQUIRE(history.at(199).frame==first);REQUIRE(history.at(200).frame==second);
 REQUIRE(history.at(160,50).frame==second); // Nearest available future frame is bounded.
 REQUIRE(history.at(150,50).frame==first); // Exact ties prefer the past.
 REQUIRE(history.at(160,20).frame==first); // Never exceed permitted anticipation.
 auto resized=resize(*first,{2,2});REQUIRE(resized->paintTime==100);
 auto mixed=composite({first,second},{1,1});REQUIRE(mixed->paintTime==200);
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
 // Selection, bounded publication and clear are safe across independent threads.
 FrameHistory concurrentHistory;concurrentHistory.configure(8,32);concurrentHistory.prepareOutput(false,64);std::atomic<bool> done{false},bad{false};
 std::thread historyWriter([&]{for(int i=0;i<5000;++i)concurrentHistory.publish(i%2?first:second,i);done=true;});
 std::thread historyReader([&]{while(!done){auto selected=concurrentHistory.at(2500);if(selected.frame&&selected.time>2500)bad=true;if(concurrentHistory.bytes()>64)bad=true;}});
 for(int i=0;i<500;++i){concurrentHistory.clear();concurrentHistory.prepareOutput(i%3?std::optional<bool>(bool(i%2)):std::nullopt,64);}historyWriter.join();historyReader.join();REQUIRE(!bad);
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
