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
