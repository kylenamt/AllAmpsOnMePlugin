#include "EnvelopeGenerator.h"

#include <cmath>
#include <algorithm>

namespace DSP
{

EnvelopeGenerator::EnvelopeGenerator()
{
}

EnvelopeGenerator::~EnvelopeGenerator()
{
}

void EnvelopeGenerator::prepare(double newSampleRate)
{
    sampleRate = newSampleRate;

    // update time in samples with new sample rate
    attackTimeSamples = std::rint(attackTimeMs * static_cast<float>(sampleRate * 0.001));
    decayTimeSamples = std::rint(decayTimeMs * static_cast<float>(sampleRate * 0.001));
    releaseTimeSamples = std::rint(releaseTimeMs * static_cast<float>(sampleRate * 0.001));

    // update leaky integrator coeffs with new sample rate
    attackLeakyIntCoeff = std::exp(-1.f / static_cast<float>(attackTimeSamples));
    decayLeakyIntCoeff = std::exp(-1.f / static_cast<float>(decayTimeSamples));
    releaseLeakyIntCoeff = std::exp(-1.f / static_cast<float>(releaseTimeSamples));

    // reset counters and state
    state = OFF;
    attackSamplesCounter = 0;
    decaySamplesCounter = 0;
    releaseSamplesCounter = 0;
}

void EnvelopeGenerator::process(float* output, unsigned int numSamples)
{
    if (isAnalogStyle)
        doAnalog(output, numSamples);
    else
        doDigital(output, numSamples);
}

void EnvelopeGenerator::start()
{
    state = ATTACK;
    attackSamplesCounter = 0;
    decaySamplesCounter = 0;
    releaseSamplesCounter = 0;
}

void EnvelopeGenerator::end()
{
    state = RELEASE;
    attackSamplesCounter = 0;
    decaySamplesCounter = 0;
    releaseSamplesCounter = 0;
}

void EnvelopeGenerator::setAnalogStyle(bool newAnalogStyle)
{
    isAnalogStyle = newAnalogStyle;

    attackTimeSamples = std::rint(attackTimeMs * static_cast<float>(sampleRate * 0.001));
    decayTimeSamples = std::rint(decayTimeMs * static_cast<float>(sampleRate * 0.001));
    releaseTimeSamples = std::rint(releaseTimeMs * static_cast<float>(sampleRate * 0.001));

    attackSamplesCounter = 0;
    decaySamplesCounter = 0;
    releaseSamplesCounter = 0;

    attackLeakyIntCoeff = std::exp(-1.f / static_cast<float>(attackTimeSamples));
    decayLeakyIntCoeff = std::exp(-1.f / static_cast<float>(decayTimeSamples));
    releaseLeakyIntCoeff = std::exp(-1.f / static_cast<float>(releaseTimeSamples));
}

void EnvelopeGenerator::setAttackTime(float newAttackTimeMs)
{
    attackTimeMs = std::fmax(newAttackTimeMs, 0.1f);
    attackTimeSamples = std::rint(attackTimeMs * static_cast<float>(sampleRate * 0.001));
    attackLeakyIntCoeff = std::exp(-1.f / static_cast<float>(attackTimeSamples));

    if (state == ATTACK)
        attackSamplesCounter = std::min(attackTimeSamples - 1, attackSamplesCounter);
}

void EnvelopeGenerator::setDecayTime(float newDecayTimeMs)
{
    decayTimeMs = std::fmax(newDecayTimeMs, 0.1f);
    decayTimeSamples = std::rint(decayTimeMs * static_cast<float>(sampleRate * 0.001));
    decayLeakyIntCoeff = std::exp(-1.f / static_cast<float>(decayTimeSamples));

    if (state == DECAY)
        decaySamplesCounter = std::min(decayTimeSamples - 1, decaySamplesCounter);
}

void EnvelopeGenerator::setSustainLevel(float newSustainLevelLinear)
{
    sustainLevel = std::clamp(newSustainLevelLinear, 0.f, 1.f);
}

void EnvelopeGenerator::setReleaseTime(float newReleaseTimeMs)
{
    releaseTimeMs = std::fmax(newReleaseTimeMs, 0.1f);
    releaseTimeSamples = std::rint(releaseTimeMs * static_cast<float>(sampleRate * 0.001));
    releaseLeakyIntCoeff = std::exp(-1.f / static_cast<float>(releaseTimeSamples));

    if (state == RELEASE)
        releaseSamplesCounter = std::min(releaseTimeSamples - 1, releaseSamplesCounter);
}

void EnvelopeGenerator::doDigital(float* output, unsigned int numSamples)
{
    for (unsigned int n = 0; n < numSamples; ++n)
    {
        switch (state)
        {
        case OFF:
            {
                currentEnvelope = 0.f;
            }
            break;

        case ATTACK:
            if (attackSamplesCounter < attackTimeSamples)
            {
                currentEnvelope += (1.f - currentEnvelope) / std::fmax(static_cast<float>(attackTimeSamples - attackSamplesCounter), 1.f);
                currentEnvelope = std::fmin(currentEnvelope, 1.f);
                ++attackSamplesCounter;
                break;
            }
            else
            {
                attackSamplesCounter = 0;
                state = DECAY;
            }

        case DECAY:
            if (decaySamplesCounter < decayTimeSamples)
            {
                currentEnvelope += (sustainLevel - currentEnvelope) / std::fmax(static_cast<float>(decayTimeSamples - decaySamplesCounter), 1.f);
                ++decaySamplesCounter;
                break;
            }
            else
            {
                decaySamplesCounter = 0;
                state = SUSTAIN;
            }

        case SUSTAIN:
            {
                currentEnvelope = sustainLevel;
            }
            break;

        case RELEASE:
            if (releaseSamplesCounter < releaseTimeSamples)
            {
                currentEnvelope += (0.f - currentEnvelope) / std::fmax(static_cast<float>(releaseTimeSamples - releaseSamplesCounter), 1.f);
                currentEnvelope = std::fmax(currentEnvelope, 0.f);
                ++releaseSamplesCounter;
            }
            else
            {
                releaseSamplesCounter = 0;
                currentEnvelope = 0.f;
                state = OFF;
            }
            break;

        default: break;
        }

        output[n] = currentEnvelope;
    }
}

void EnvelopeGenerator::doAnalog(float* output, unsigned int numSamples)
{
    for (unsigned int n = 0; n < numSamples; ++n)
    {
        switch (state)
        {
        case OFF:
            {
                currentEnvelope = 0.f;
            }
            break;

        case ATTACK:
            if (std::fabs(currentEnvelope - 1.f) > delta)
            {
                currentEnvelope = (currentEnvelope - 1.1f) * attackLeakyIntCoeff + 1.1f;
                currentEnvelope = std::fmin(currentEnvelope, 1.f);
                break;
            }
            else
            {
                currentEnvelope = 1.f;
                state = DECAY;
            }

        case DECAY:
            if (std::fabs(currentEnvelope - sustainLevel) > delta)
            {
                currentEnvelope = (currentEnvelope - sustainLevel) * decayLeakyIntCoeff + sustainLevel;
                break;
            }
            else
            {
                currentEnvelope = sustainLevel;
                state = SUSTAIN;
            }

        case SUSTAIN:
            {
                currentEnvelope = sustainLevel;
            }
            break;

        case RELEASE:
            if (std::fabs(currentEnvelope - 0.f) > delta)
            {
                currentEnvelope = (currentEnvelope - 0.f) * releaseLeakyIntCoeff + 0.f;
            }
            else
            {
                currentEnvelope = 0.f;
                state = OFF;
            }
            break;

        default: break;
        }

        output[n] = currentEnvelope;
    }
}

}
