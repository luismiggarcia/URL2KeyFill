#include "DeckLinkAPI.h"
#include <CoreFoundation/CoreFoundation.h>
#include <cstdio>
int main(){auto it=CreateDeckLinkIteratorInstance();if(!it)return 1;IDeckLink* d=nullptr;int index=0;while(it->Next(&d)==S_OK){IDeckLinkStatus* s=nullptr;if(d->QueryInterface(IID_IDeckLinkStatus,reinterpret_cast<void**>(&s))==S_OK){for(auto key:{bmdDeckLinkStatusBusy,bmdDeckLinkStatusCurrentVideoInputMode,bmdDeckLinkStatusCurrentVideoOutputMode,bmdDeckLinkStatusLastVideoOutputPixelFormat}){int64_t value=0;auto result=s->GetInt(key,&value);std::printf("device=%d key=%08x result=%d value=%lld\n",index,unsigned(key),int(result),(long long)value);}s->Release();}d->Release();++index;}it->Release();}
