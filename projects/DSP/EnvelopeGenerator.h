#pragma once

namespace DSP
{

class EnvelopeGenerator
{
public:
    EnvelopeGenerator();
    ~EnvelopeGenerator();

    EnvelopeGenerator(const EnvelopeGenerator&) = delete;
    EnvelopeGenerator(EnvelopeGenerator&&) = delete;
    const EnvelopeGenerator& operator=(const EnvelopeGenerator&) = delete;
    const EnvelopeGenerator& operator=(EnvelopeGenerator&&) = delete;

    void prepare(double newSampleRate);
    void process(float* output, unsigned int numSamples);

    // trigger the beginning of the envelope - note on
    void start();

    // trigger the ending of the envelope - note off
    void end();

    bool isOff() const { return state == OFF; }

    void setAnalogStyle(bool isAnalogStyle);
    void setAttackTime(float attackTimeMs);
    void setDecayTime(float decayTimeMs);
    void setSustainLevel(float sustainLevelLinear);
    void setReleaseTime(float releaseTimeMs);

private:
    double sampleRate { 48000.0 };

    float attackTimeMs { 10.f };
    float decayTimeMs { 5.f };
    float releaseTimeMs { 50.f };

    float sustainLevel { 1.f };

    unsigned int attackTimeSamples { 0 };
    unsigned int decayTimeSamples { 0 };
    unsigned int releaseTimeSamples { 0 };

    unsigned int attackSamplesCounter { 0 };
    unsigned int decaySamplesCounter { 0 };
    unsigned int releaseSamplesCounter { 0 };

    float currentEnvelope { 0.f };

    float attackLeakyIntCoeff { 0.f };
    float decayLeakyIntCoeff { 0.f };
    float releaseLeakyIntCoeff { 0.f };

    bool isAnalogStyle { false };

    enum EnvelopeState : unsigned int
    {
        OFF = 0,
        ATTACK,
        DECAY,
        SUSTAIN,
        RELEASE
    };

    EnvelopeState state { OFF };

    static constexpr float delta { 1e-3f };

    void doDigital(float* output, unsigned int numSamples);
    void doAnalog(float* output, unsigned int numSamples);
};

}
