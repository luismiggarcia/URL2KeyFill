#pragma once
// Controlled diagnostic protocol only. Not a clock for arbitrary web pages.
#include <atomic>
#include <cstdint>
#include <optional>
#include <cstdlib>
namespace u2k {
inline std::optional<uint16_t> referenceFrameId(const uint8_t* pixels,int width,int height,size_t stride){
 if(!pixels||width!=1920||height!=1080||stride<size_t(width)*4)return {};
 uint64_t bits=0;
 for(int b=0;b<40;++b){
  int values[2];for(int row=0;row<2;++row){const auto p=pixels+size_t(72+row*12)*stride+(114+b*36)*4;if(p[3]<240)return {};const int v=(int(p[0])+p[1]+p[2])/3;if(v>192)values[row]=1;else if(v<64)values[row]=0;else return {};}
  if(values[0]!=values[1])return {};bits=(bits<<1)|values[0];
 }
 const auto id=uint16_t((bits>>12)&0xfff);
 if((bits>>24)!=0xa65c||id>=3000||(bits&0xfff)!=((id^0xb6d)&0xfff))return {};
 return id;
}
class ReferenceAudioClock {
 uint64_t bits_=0;int count_=0,consecutive_=0;int lastId_=-1;int64_t candidate_=INT64_MIN;
public:
 std::atomic<int64_t> origin{INT64_MIN},lastOrigin{INT64_MIN};
 std::atomic<uint64_t> matches{0},mismatches{0},skipped{0};
 void clear(){bits_=0;count_=0;consecutive_=0;lastId_=-1;candidate_=INT64_MIN;origin=INT64_MIN;lastOrigin=INT64_MIN;matches=0;mismatches=0;skipped=0;}
 void sample(int16_t pcm,int64_t pts){
  bits_=(bits_<<1)|(uint16_t(pcm)&1);if(count_<64)++count_;
  if(count_<64||(bits_>>32)!=0x55324b46)return;
  const auto id=int((bits_>>16)&0xffff);if(id>=3000||(bits_&0xffff)!=uint16_t(id^0xa55a))return;
  const auto observed=pts-263-int64_t(id)*960;lastOrigin=observed;++matches;
  if(lastId_>=0&&id>lastId_+1)skipped+=id-lastId_-1;
  if(lastId_>=0&&(id<=lastId_||std::llabs(observed-candidate_)>1)){++mismatches;consecutive_=0;origin=INT64_MIN;}
  if(!consecutive_)candidate_=observed;
  lastId_=id;if(++consecutive_>=3)origin=candidate_;
 }
};
}
