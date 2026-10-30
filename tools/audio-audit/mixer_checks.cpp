// Observations behind audit findings A4, A6 and A8, using the real AudioLane/AudioMixer.
// Each line prints the current behaviour; "OBSERVED" marks behaviour the audit questions.
// Exit status is always 0: this documents behaviour, it is not a regression test.
#include "core/Scene.hpp"
#include <cmath>
#include <cstdio>
using namespace u2k;
static void line(bool questioned,const char* text){std::printf("%-9s %s\n",questioned?"OBSERVED":"OK",text);}
int main(){
    char text[256];
    {   // A6: layer MONO with one input channel switched off.
        auto lane=std::make_shared<AudioLane>();std::vector<float> l(480,.5f),r(480,0.f);const float* d[]={l.data(),r.data()};lane->push(d,480,2,1000);
        AudioMixer m;m.inputs({{lane,true,1,true,true,false}});auto s=m.sample(48000,480);
        std::snprintf(text,sizeof text,"A6 layer MONO, R off: L=0.500 in -> L=%.3f R=%.3f (%.2f dB)",s[0],s[1],20*std::log10(s[0]/.5));line(std::abs(s[0]-.5f)>1e-6f,text);
        m.inputs({{lane,true,1,false,true,false}});s=m.sample(48000,480);
        std::snprintf(text,sizeof text,"A6 layer stereo, R off: L=%.3f R=%.3f",s[0],s[1]);line(false,text);
        m.inputs({{lane,true,1}});m.mono=true;m.right=false;s=m.sample(48000,480);
        std::snprintf(text,sizeof text,"A7 OUTPUT MONO then R off (output-channel mute): L=%.3f R=%.3f",s[0],s[1]);line(false,text);
    }
    {   // A4: over-unity mix is hard-clipped on PCM while meters clamp before measuring.
        auto lane=std::make_shared<AudioLane>();std::vector<float> v(480,.8f);const float* d[]={v.data(),v.data()};lane->push(d,480,2,1000);
        AudioMixer m;m.inputs({{lane,true,2}});auto levels=m.sourceLevels(48000,480);auto pcm=m.mix(48000,480);
        std::snprintf(text,sizeof text,"A4 0.8 x gain 2 (1.6 = +4.1 dBFS): composite meter peak %.3f, PCM %d, clipped samples %llu",levels.peakLeft,pcm[0],(unsigned long long)m.clipped.load());
        line(levels.peakLeft<=1.f,text);
    }
    {   // A8: overlapping packets are summed.
        auto lane=std::make_shared<AudioLane>();std::vector<float> v(480,.25f);const float* d[]={v.data(),v.data()};
        lane->push(d,480,2,1000);lane->push(d,480,2,1009);
        AudioMixer m;m.inputs({{lane,true,1}});auto s=m.sample(48000+9*48,4);
        std::snprintf(text,sizeof text,"A8 packets overlapping by 1 ms: sample %.3f where source is 0.250",s[0]);line(std::abs(s[0]-.25f)>1e-6f,text);
    }
    {   // A1 context: a disabled mixer silences sample() (Monitor OUTPUT) but not the composite meter.
        auto lane=std::make_shared<AudioLane>();std::vector<float> v(480,.5f);const float* d[]={v.data(),v.data()};lane->push(d,480,2,1000);
        AudioMixer m;m.inputs({{lane,true,1}});m.enabled=false;
        auto monitor=m.sample(48000,480);auto meter=m.sourceLevels(48000,480);
        std::snprintf(text,sizeof text,"A1 mixer.enabled=false (left by PAUSE then STOP): monitor %.3f, composite meter peak %.3f",monitor[0],meter.peakLeft);
        line(monitor[0]==0&&meter.peakLeft>0,text);
    }
    return 0;
}
