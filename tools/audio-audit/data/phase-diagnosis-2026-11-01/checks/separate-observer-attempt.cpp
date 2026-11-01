// Read-only observation of an already active output. Never enables/disables
// streams, registers callbacks, schedules media or changes device configuration.
#include "DeckLinkAPI.h"
#include <mach/mach_time.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
static int64_t hostNs(){static const auto tb=[] {mach_timebase_info_data_t t;mach_timebase_info(&t);return t;}();return int64_t((__uint128_t(mach_absolute_time())*tb.numer)/tb.denom);}
int main(int argc,char** argv){
 if(argc!=3){std::fprintf(stderr,"Use: decklink_observe_clock seconds csv\n");return 2;}
 char* end=nullptr;const long seconds=std::strtol(argv[1],&end,10);
 if(*end||seconds<10||seconds>600)return 2;
 auto iterator=CreateDeckLinkIteratorInstance();if(!iterator)return 1;
 IDeckLink* device=nullptr;IDeckLinkOutput* output=nullptr;
 while(iterator->Next(&device)==S_OK){if(device->QueryInterface(IID_IDeckLinkOutput,reinterpret_cast<void**>(&output))==S_OK)break;device->Release();device=nullptr;}iterator->Release();
 if(!output)return 1;
 auto file=std::fopen(argv[2],"wx");if(!file){output->Release();device->Release();return 1;}
 std::fprintf(file,"host_before_ns,host_after_ns,stream_us,hardware_us,in_frame_us,ticks_per_frame_us,speed,stream_ok,hardware_ok\n");
 const auto began=std::chrono::steady_clock::now();int result=0;
 do{
  int64_t stream=0,hardware=0,inFrame=0,ticks=0;double speed=0;
  const auto before=hostNs();const auto a=output->GetScheduledStreamTime(1000000,&stream,&speed);const auto b=output->GetHardwareReferenceClock(1000000,&hardware,&inFrame,&ticks);const auto after=hostNs();
  std::fprintf(file,"%lld,%lld,%lld,%lld,%lld,%lld,%.9f,%d,%d\n",(long long)before,(long long)after,(long long)stream,(long long)hardware,(long long)inFrame,(long long)ticks,speed,a==S_OK,b==S_OK);
  if(a!=S_OK||b!=S_OK||speed<=0){result=1;break;}
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
 }while(std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count()<seconds);
 if(std::ferror(file))result=1;if(std::fclose(file))result=1;
 output->Release();device->Release();return result;
}
