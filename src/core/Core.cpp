#include "Core.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
namespace u2k {
void validate(Size s) {
    if(s.width<1||s.height<1||s.width>7680||s.height>4320) throw std::invalid_argument("Dimensions must be within 1–7680 × 1–4320.");
}
Rect fit(Size s,Size d) {
    validate(s);validate(d);
    const double scale=std::min(double(d.width)/s.width,double(d.height)/s.height);
    int w=std::max(1,int(std::lround(s.width*scale))),h=std::max(1,int(std::lround(s.height*scale)));
    return {(d.width-w)/2,(d.height-h)/2,w,h};
}
void Rate::validate() const {if(duration<=0||scale<=0||duration>1000000||scale>1000000||double(scale)/duration>240||scale<duration) throw std::invalid_argument("Invalid rational frame rate.");}
int64_t Rate::timestamp(uint64_t n) const {validate(); if(n>uint64_t(INT64_MAX/duration))throw std::overflow_error("Timestamp overflow.");return int64_t(n)*duration;}
std::string Rate::label() const {std::ostringstream s;s<<std::fixed<<std::setprecision(3)<<double(scale)/duration<<" fps ("<<scale<<"/"<<duration<<")";return s.str();}
bool allowedUrl(const std::string& s) {
    return s.size()<=8192 && (s.rfind("https://",0)==0||s.rfind("http://",0)==0||s=="u2k://test/index.html") && s.find_first_of("\r\n\0",0,3)==std::string::npos;
}
void Settings::validate() const {u2k::validate(viewport);rate.validate();if(!allowedUrl(url))throw std::invalid_argument("Use HTTP/HTTPS or the built-in test URL.");if(device.size()>1024)throw std::invalid_argument("Invalid device identifier.");}
void Settings::save(const std::filesystem::path& p) const {
    validate();if(!p.parent_path().empty())std::filesystem::create_directories(p.parent_path());
    auto temp=p;temp+=".tmp";
    {std::ofstream f(temp,std::ios::trunc);f.exceptions(std::ios::failbit|std::ios::badbit);
    f<<"URL2KeyFill 2\n"<<std::quoted(url)<<'\n'<<viewport.width<<' '<<viewport.height<<'\n'<<std::quoted(device)<<'\n'<<mode<<' '<<rate.duration<<' '<<rate.scale<<' '<<matchOutput<<'\n';f.flush();}
    std::filesystem::rename(temp,p);
}
Settings Settings::load(const std::filesystem::path& p) {
    if(!std::filesystem::exists(p))return {};
    std::ifstream f(p);Settings s;std::string header;std::getline(f,header);
    if((header!="URL2KeyFill 1"&&header!="URL2KeyFill 2")||!(f>>std::quoted(s.url)>>s.viewport.width>>s.viewport.height>>std::quoted(s.device)>>s.mode>>s.rate.duration>>s.rate.scale))throw std::runtime_error("Invalid settings file; the original was preserved.");
    if(header=="URL2KeyFill 2"){int match=0;if(!(f>>match)||match<0||match>1)throw std::runtime_error("Invalid viewport option.");s.matchOutput=match;}else s.matchOutput=false;
    f>>std::ws;if(!f.eof())throw std::runtime_error("Unexpected settings data.");s.validate();return s;
}
Frame::Frame(Size s,uint64_t seq):size(s),stride(size_t(s.width)*4),sequence(seq){validate(s);pixels.resize(stride*s.height,0);}
std::shared_ptr<const Frame> Frame::copy(Size s,const void* p,size_t row,uint64_t seq) {
    validate(s);if(!p||row<size_t(s.width)*4)throw std::invalid_argument("Invalid BGRA buffer.");
    auto f=std::make_shared<Frame>(s,seq);for(int y=0;y<s.height;y++)std::memcpy(f->pixels.data()+y*f->stride,static_cast<const uint8_t*>(p)+y*row,f->stride);return f;
}
std::shared_ptr<const Frame> resize(const Frame& src,Size dest) {
    auto out=std::make_shared<Frame>(dest,src.sequence);auto r=fit(src.size,dest);
    if(src.size==dest){out->pixels=src.pixels;return out;}
    for(int y=0;y<r.height;y++) {
        double sy=std::clamp((y+0.5)*src.size.height/r.height-0.5,0.,double(src.size.height-1));
        int y0=int(sy),y1=std::min(y0+1,src.size.height-1);double fy=sy-y0;
        for(int x=0;x<r.width;x++) {
            double sx=std::clamp((x+0.5)*src.size.width/r.width-0.5,0.,double(src.size.width-1));
            int x0=int(sx),x1=std::min(x0+1,src.size.width-1);double fx=sx-x0;
            for(int c=0;c<4;c++) {
                auto at=[&](int xx,int yy){return src.pixels[yy*src.stride+xx*4+c];};
                double a=at(x0,y0)*(1-fx)+at(x1,y0)*fx,b=at(x0,y1)*(1-fx)+at(x1,y1)*fx;
                out->pixels[(y+r.y)*out->stride+(x+r.x)*4+c]=uint8_t(std::lround(a*(1-fy)+b*fy));
            }
        }
    }return out;
}
void toSDR(const Frame& f,uint8_t* dst,size_t stride,bool air,bool argb) {
    if(!dst||stride<f.stride)throw std::invalid_argument("Invalid output stride.");
    // Lookup eliminates expensive pow() from the frame loop; indexed by alpha and component.
    static const auto lut=[] {std::vector<uint8_t> v(65536);for(int a=1;a<256;a++)for(int c=0;c<256;c++) {
        double s=std::min(1.,double(c)/a);double linear=s<=.04045?s/12.92:std::pow((s+.055)/1.055,2.4);
        double r=linear<.018?4.5*linear:1.099*std::pow(linear,.45)-.099;
        v[a*256+c]=uint8_t(std::clamp(std::lround(r*255),0L,255L));}return v;}();
    for(int y=0;y<f.size.height;y++) {
        auto d=dst+y*stride;std::memset(d,0,stride);if(!air)continue;
        auto s=f.pixels.data()+y*f.stride;
        for(int x=0;x<f.size.width;x++,s+=4,d+=4){int a=s[3];if(argb){d[0]=a;d[1]=lut[a*256+s[2]];d[2]=lut[a*256+s[1]];d[3]=lut[a*256+s[0]];}else{for(int c=0;c<3;c++)d[c]=lut[a*256+s[c]];d[3]=a;}}
    }
}
}
