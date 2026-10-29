#include "output/Output.hpp"
#include <iostream>
int main(){try{auto output=u2k::makeOutput();auto devices=output->enumerate();std::cout<<output->status().message<<'\n';for(auto& d:devices){std::cout<<"DEVICE "<<d.id<<" "<<d.name<<'\n';for(auto& m:d.modes)std::cout<<"  "<<m.id<<" "<<m.size.width<<"x"<<m.size.height<<" "<<m.name<<'\n';}return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
