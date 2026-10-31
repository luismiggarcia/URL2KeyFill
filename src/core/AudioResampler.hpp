#pragma once
#include <vector>
#include <cstddef>
namespace u2k {
// Band-limited interpolation for the narrow +/-200 ppm clock envelope.
// Symmetric lookahead is supplied from retained history, inside the source-age budget.
class AudioResampler {
 std::vector<float> table_;
public:
 static constexpr int taps=160,left=79,right=80,phases=1024;
 AudioResampler();
 void render(const float* stereo,size_t inputFrames,double offset,double ratio,float* output,size_t frames)const;
 double coefficient(int tap,double phase)const;
};
}
