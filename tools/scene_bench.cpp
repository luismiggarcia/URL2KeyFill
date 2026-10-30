// Isolated single-layer composition benchmark (100 semi-transparent frames/size).
// Build with C++20, -O2, -pthread and the matching Core.cpp/Scene.cpp sources.
// For before/after comparison, snapshot the core headers and sources from bc8ae57
// into a temporary core/ directory, then compile this same file against each tree.
// This measures composition only, not CEF/SDI latency or total application CPU.
#include "core/Scene.hpp"
#include <chrono>
#include <iostream>
int main(){for(u2k::Size size: {u2k::Size{1920,1080},u2k::Size{3840,2160}}){auto f=std::make_shared<u2k::Frame>(size);for(size_t i=0;i<f->pixels.size();i+=4){f->pixels[i]=64;f->pixels[i+1]=32;f->pixels[i+2]=16;f->pixels[i+3]=128;}constexpr int count=100;auto start=std::chrono::steady_clock::now();std::shared_ptr<const u2k::Frame> out;unsigned sum=0;for(int i=0;i<count;++i){out=u2k::composite({f},size);sum+=out->pixels[0];}auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/count;if(!out||out->pixels!=f->pixels)return 1;std::cout<<size.width<<"x"<<size.height<<" ms/frame="<<ms<<" alias="<<(out==f)<<" checksum="<<sum<<'\n';}}
