#pragma once

#include <array>
#include <algorithm>
#include <cmath>
#include <juce_core/juce_core.h>

#include "AddSynthConstants.h"
#include "Ramp.h"
#include "SineWavetable.h"

namespace DSP
{
// simplified oscillator with only sine wave
class Partial
{
public:
    void prepare(double newSampleRate)
    {
        sampleRate = newSampleRate;
        amplitudeRamp.prepare(newSampleRate);
        updatePhaseDelta();
    }

    float processSample()
    {
        const float sample = SineWavetable::lookup(phase) * amplitudeRamp.getNext();
        phase += phaseDelta;
        if (phase >= 2*M_PI){phase -= 2*M_PI;}
        return sample;
    }

    void setFrequency(float freqHz)
    {
        frequency = std::clamp(freqHz, 0.0f, 20000.0f);
        updatePhaseDelta();
    }

    void setAmplitude(float amp)
    {
        amplitudeRamp.setTarget(amp);
    }

    void setPhase(float newPhase)
    {
        phase = std::fmod(newPhase, 2*M_PI);
        if (phase < 0.0f){phase += 2*M_PI;};
    }


private:
    void updatePhaseDelta()
    {
        if (sampleRate > 0.0)
            phaseDelta = 2*M_PI * frequency / static_cast<float>(sampleRate);
        else
            phaseDelta = 0.0f;
    }

    double sampleRate { 48000.0 };
    float frequency { 0.0f };
    DSP::Ramp<float> amplitudeRamp { 0.005f };
    float phase { 0.0f };
    float phaseDelta { 0.0f };
};

class PartialBank
{
public:
    PartialBank();
    ~PartialBank();

    PartialBank(const PartialBank&) = delete;
    PartialBank(PartialBank&&) = delete;
    const PartialBank& operator=(const PartialBank&) = delete;
    const PartialBank& operator=(PartialBank&&) = delete;

    void prepare(double newSampleRate);
    void process(float* output, unsigned int numSamples);
    float processSample();

    // Sum partials into per-band accumulators (each scaled by normFactor).
    void processSampleBands(std::array<float, kNumBands>& out);

    void setBaseFrequency(float freqHz);
    void setNumPartials(int numPartials);
    void setRatios(const std::array<float, kMaxPartials>& newRatios);
    void setInharmonicity(float value);
    void setOffset(float value);          // shifts all partials by a constant ratio
    void setStretch(float value);         // compresses/stretches the harmonic spacing
    void setOddEvenBalance(float value);  // shifts odd vs even partials in opposite directions
    void setCoefficients(const std::array<float, kMaxPartials>& amps);
    void setBandCrossovers(const std::array<float, kNumCrossovers>& crossoverHz);
    void randomizePhases(juce::Random& rng);

    int getNumActivePartials() const { return numActive; }

private:
    void updateActiveCount();
    void updateFrequencies();
    void recalculateNormalization();

    double sampleRate { 48000.0 };
    float baseFrequency { 440.0f };
    float inharmonicity { 0.0f }; // deviation of higher harmonics
    float offset { 0.0f };        // constant ratio added to every partial
    float stretch { 1.0f };       // scales harmonic spacing around the fundamental
    float oddEvenBalance { 0.0f };// opposite ratio shift for odd vs even partials
    
    int requestedPartials { 32 };
    int numActive { 0 };
    float normFactor { 1.0f };

    std::array<Partial, kMaxPartials> partials {};
    alignas(32) std::array<float, kMaxPartials> amplitudes {};
    alignas(32) std::array<float, kMaxPartials> ratios {};

    std::array<float, kNumCrossovers> crossovers { 150.0f, 600.0f, 2000.0f, 6000.0f };
    std::array<int, kMaxPartials> bandOf {};      // band index (0..kNumBands-1) per partial
};
}
