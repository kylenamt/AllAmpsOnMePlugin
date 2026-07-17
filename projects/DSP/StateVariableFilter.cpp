#include "StateVariableFilter.h"

#include <cmath>
#include <algorithm>

namespace DSP
{

StateVariableFilter::StateVariableFilter()
{
}

StateVariableFilter::~StateVariableFilter()
{
}

void StateVariableFilter::prepare(double newSampleRate)
{
    sampleRate = newSampleRate;

    state0 = 0.f;
    state1 = 0.f;
}

void StateVariableFilter::process(float* lpfOut, float* bpfOut, float* hpfOut, const float* audioIn, const float* freqIn, const float* resoIn, unsigned int numSamples)
{
    for (unsigned int n = 0; n < numSamples; ++n)
    {
        // 2R = 1 / Q
        float twoR = 1.f / std::clamp(resoIn[n], 0.1f, 10.f);

        // g = tan(pi * Fc / Fs)
        float g = std::tan(static_cast<float>(M_PI / sampleRate) * std::clamp(freqIn[n], 20.f, 20000.f));

        // g0 = 2R + g
        float g0 = twoR + g;

        // d = 1 / (1 + 2Rg + g^2)
        float d = 1.f / (1.f + twoR * g + g * g);

        // hp = (x - s1 - g0 * s0) * d
        float hp = (audioIn[n] - state1 - g0 * state0) * d;

        // v0 = g * hp
        float v0 = g * hp;

        // bp = v0 + s0
        float bp = v0 + state0;

        // s0 = bp + v0
        state0 = bp + v0;

        // v1 = g * bp
        float v1 = g * bp;

        // lp = v1 + s1
        float lp = v1 + state1;

        // s1 = lp + v1
        state1 = lp + v1;

        // write to output vectors
        lpfOut[n] = lp;
        bpfOut[n] = bp;
        hpfOut[n] = hp;
    }
}

}
