#pragma once

#include <array>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "AddSynthConstants.h"
#include "EnvelopeGenerator.h"
#include "PartialBank.h"
#include "Ramp.h"
#include "SpectralCoef.h"
#include "StateVariableFilter.h"

namespace DSP
{
float convertMidiNoteToFreq(int midiNote);

struct SynthParams
{
    std::array<float, kMaxPartials> amplitudes {};
    std::array<float, kMaxPartials> ratios {};
    int numPartials { 32 };

    float attackMs { 10.0f };
    float decayMs { 80.0f };
    float sustain { 0.7f };
    float releaseMs { 120.0f };

    // per-band amplitude envelopes (used when perBandEnv == true)
    bool perBandEnv { false };
    std::array<float, kNumCrossovers> bandCrossoverHz { 150.0f, 600.0f, 2000.0f, 6000.0f };
    std::array<float, kNumBands> bandAttackMs  { 10.0f, 10.0f, 10.0f, 10.0f, 10.0f };
    std::array<float, kNumBands> bandDecayMs   { 80.0f, 80.0f, 80.0f, 80.0f, 80.0f };
    std::array<float, kNumBands> bandSustain   { 0.7f, 0.7f, 0.7f, 0.7f, 0.7f };
    std::array<float, kNumBands> bandReleaseMs { 120.0f, 120.0f, 120.0f, 120.0f, 120.0f };

    float filterCutoffHz { 2000.0f };
    float filterResonance { 0.7f };
    float filterEnvAmount { 0.0f };

    // separate filter envelope
    float filterAttackMs { 10.0f };
    float filterDecayMs { 80.0f };
    float filterSustain { 0.7f };
    float filterReleaseMs { 120.0f };

    float filterLfoRate { 0.0f };
    float filterLfoDepth { 0.0f };
    float inharmonicity { 0.0f };
    float offset { 0.0f };
    float stretch { 1.0f };
    float oddEvenBalance { 0.0f };

    float masterGain { 0.8f };
};

class AdditiveSound : public juce::SynthesiserSound
{
public:
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }
};

class AdditiveVoice : public juce::SynthesiserVoice
{
public:
    AdditiveVoice();
    ~AdditiveVoice() override;

    AdditiveVoice(const AdditiveVoice&) = delete;
    AdditiveVoice(AdditiveVoice&&) = delete;
    const AdditiveVoice& operator=(const AdditiveVoice&) = delete;
    const AdditiveVoice& operator=(AdditiveVoice&&) = delete;

    void prepare(double newSampleRate, int samplesPerBlock);
    void syncParams(const SynthParams& params);

    bool canPlaySound(juce::SynthesiserSound* sound) override;
    void startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*, int) override;
    void stopNote(float velocity, bool allowTailOff) override;
    void pitchWheelMoved(int) override;
    void controllerMoved(int, int) override;
    void renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) override;

private:
    double sampleRate { 48000.0 };
    int blockSize { 0 };

    PartialBank partialBank;
    DSP::EnvelopeGenerator envGen;
    DSP::EnvelopeGenerator filterGen;
    std::array<DSP::EnvelopeGenerator, kNumBands> bandEnv;
    bool perBandEnv { false };
    DSP::StateVariableFilter filter;

    float filterCutoffHz { 2000.0f };
    float filterResonance { 0.7f };
    float filterEnvAmount { 0.0f };
    float filterLfoRate { 0.0f };
    float filterLfoDepth { 0.0f };
    float filterLfoPhase { 0.0f };
    float filterLfoPhaseDelta { 0.0f };
    float voiceGain { 1.0f };
    float noteVelocity { 1.0f };
    DSP::Ramp<float> filterCutoffRamp;
    DSP::Ramp<float> filterResonanceRamp;
    DSP::Ramp<float> filterEnvAmountRamp;
    DSP::Ramp<float> filterLfoRateRamp;
    DSP::Ramp<float> filterLfoDepthRamp;
    DSP::Ramp<float> voiceGainRamp;
    DSP::Ramp<float> noteVelocityRamp;

    juce::Random random;
};

class AdditiveSynthesiser : public juce::Synthesiser
{
public:
    void prepare(double newSampleRate, int samplesPerBlock);
    void updateParams(const SynthParams& params);
};
}
