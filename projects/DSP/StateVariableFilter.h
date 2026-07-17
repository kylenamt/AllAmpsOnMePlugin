#pragma once

namespace DSP
{

class StateVariableFilter
{
public:
    StateVariableFilter();
    ~StateVariableFilter();

    StateVariableFilter(const StateVariableFilter&) = delete;
    StateVariableFilter(StateVariableFilter&&) = delete;
    const StateVariableFilter& operator=(const StateVariableFilter&) = delete;
    const StateVariableFilter& operator=(StateVariableFilter&&) = delete;

    void prepare(double sampleRate);

    void process(float* lpfOut, float* bpfOut, float* hpfOut,
                 const float* audioIn, const float* freqIn, const float* resoIn,
                 unsigned int numSamples);

private:
    double sampleRate { 48000.0 };

    float state0 { 0.f };
    float state1 { 0.f };
};

}
