#include "AudioResampler.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
namespace u2k {
namespace {double bessel(double x){double sum=1,term=1;for(int k=1;k<60;++k){term*=x*x/(4*k*k);sum+=term;if(term<sum*1e-16)break;}return sum;}}
AudioResampler::AudioResampler():table_((phases+1)*taps){
 constexpr double cutoff=.45,beta=10.5;const double norm=bessel(beta);
 for(int p=0;p<=phases;++p){double sum=0;for(int j=0;j<taps;++j){const double x=j-left-double(p)/phases,u=x/80.;const double window=std::abs(u)>=1?0:bessel(beta*std::sqrt(std::max(0.,1-u*u)))/norm;const double kernel=std::abs(x)<1e-12?2*cutoff:std::sin(2*std::numbers::pi*cutoff*x)/(std::numbers::pi*x);const double v=kernel*window;table_[p*taps+j]=float(v);sum+=v;}for(int j=0;j<taps;++j)table_[p*taps+j]=float(table_[p*taps+j]/sum);}
}
double AudioResampler::coefficient(int tap,double phase)const{const double t=std::clamp(phase,0.,1.)*phases;const int p=std::min(phases-1,int(t));const double f=t-p;return table_[p*taps+tap]*(1-f)+table_[(p+1)*taps+tap]*f;}
void AudioResampler::render(const float* input,size_t inputFrames,double offset,double ratio,float* out,size_t n)const{
 if(!input||!out||!std::isfinite(offset)||!std::isfinite(ratio)||ratio<.9998||ratio>1.0002||n>48000)throw std::invalid_argument("Invalid adaptive resampling block");
 for(size_t i=0;i<n;++i){const double position=offset+double(i)*ratio;const auto base=int64_t(std::floor(position));if(base-left<0||base+right>=int64_t(inputFrames))throw std::out_of_range("Missing resampler history/lookahead");const double t=(position-base)*phases;const int p=std::min(phases-1,int(t));const float f=float(t-p);const float* a=table_.data()+p*taps;const float* b=a+taps;const float* samples=input+(base-left)*2;double l=0,r=0;for(int k=0;k<taps;++k){const float w=a[k]+(b[k]-a[k])*f;l+=samples[k*2]*w;r+=samples[k*2+1]*w;}out[i*2]=float(l);out[i*2+1]=float(r);}
}
}
