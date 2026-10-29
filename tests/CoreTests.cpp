#include "core/Core.hpp"
#include "core/Pipeline.hpp"
#include <chrono>
#include <iostream>
#include <fstream>
#include <thread>
#include <stdexcept>
using namespace u2k;
#define REQUIRE(x) do{if(!(x))throw std::runtime_error(#x);}while(0)
template<class F>void rejects(F f){bool threw=false;try{f();}catch(...){threw=true;}REQUIRE(threw);}
int main(){try{
 Rate r{1001,30000};REQUIRE(r.timestamp(30000)==30030000);REQUIRE((Rate{1001,60000}.timestamp(60000)==60060000));rejects([]{Rate{0,0}.validate();});rejects([&]{r.timestamp(UINT64_MAX);});
 auto rect=fit({100,100},{1920,1080});REQUIRE(rect.x==420&&rect.width==1080&&rect.y==0);rejects([]{validate({-1,1080});});
 uint8_t data[]={0,0,128,128,0,255,0,255,99,99,99,99};auto f=Frame::copy({2,1},data,12,1);data[2]=0;REQUIRE(f->pixels[2]==128);REQUIRE(f->stride==8);
 LatestFrame latest;latest.publish(f);auto held=latest.get();latest.clear();REQUIRE(held->pixels[2]==128&&!latest.get());
 auto scaled=resize(*f,{4,4});REQUIRE(scaled->pixels[3]==0);REQUIRE(scaled->pixels[scaled->stride+3]==128);REQUIRE(scaled->pixels[3*scaled->stride+3]==0);
 std::vector<uint8_t> out(12,77);toSDR(*f,out.data(),12,true);REQUIRE(out[2]==255&&out[3]==128&&out[7]==255&&out[11]==0);toSDR(*f,out.data(),12,false);for(auto p:out)REQUIRE(p==0);
 toSDR(*f,out.data(),12,true,true);REQUIRE(out[0]==128&&out[1]==255);rejects([&]{Frame::copy({2,1},data,3,0);});
 Settings s;s.url="https://example.com/?a=\"x\"&b=2";s.device="device 123";s.viewport={3840,2160};s.mode=42;
 auto path=std::filesystem::temp_directory_path()/"url2keyfill-test-settings.txt";s.save(path);auto restored=Settings::load(path);REQUIRE(restored.url==s.url&&restored.device==s.device&&restored.viewport==s.viewport&&restored.mode==42&&restored.rate.duration==1001);
 {std::ofstream file(path);file<<"invalid";}rejects([&]{Settings::load(path);});std::filesystem::remove(path);
 REQUIRE(!allowedUrl("file:///etc/passwd"));REQUIRE(!allowedUrl("javascript:alert(1)"));REQUIRE(allowedUrl("u2k://test/index.html"));
 {Pipeline p;p.target({4,4});p.submit(f);for(int i=0;i<100&&!p.frames().get();i++)std::this_thread::sleep_for(std::chrono::milliseconds(2));REQUIRE(p.frames().get());REQUIRE((p.frames().get()->size==Size{4,4}));p.clear();REQUIRE(!p.frames().get());}
 std::cout<<"PASS: settings, rational time, aspect fit, alpha, stride, ownership, pipeline, URL policy\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
