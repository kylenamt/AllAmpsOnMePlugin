#include "PartialBank.h"

namespace DSP
{
PartialBank::PartialBank()
{
    for (int i = 0; i < kMaxPartials; ++i)
        ratios[i] = static_cast<float>(i + 1);

    amplitudes.fill(0.0f);
    amplitudes[0] = 1.0f;
}

PartialBank::~PartialBank() = default;

void PartialBank::prepare(double newSampleRate)
{
    sampleRate = newSampleRate;
    for (auto& partial : partials)
        partial.prepare(sampleRate);

    updateActiveCount();
    updateFrequencies();
    recalculateNormalization();
}

void PartialBank::process(float* output, unsigned int numSamples)
{
    for (unsigned int n = 0; n < numSamples; ++n)
        output[n] = processSample();
}

float PartialBank::processSample()
{
    float sum = 0.0f;
    for (int i = 0; i < numActive; ++i)
        sum += partials[i].processSample();

    return sum * normFactor;
}

void PartialBank::processSampleBands(std::array<float, kNumBands>& out)
{
    out.fill(0.0f);
    for (int i = 0; i < numActive; ++i)
        out[static_cast<size_t>(bandOf[i])] += partials[i].processSample();

    for (auto& v : out)
        v *= normFactor;
}

void PartialBank::setBaseFrequency(float freqHz)
{
    baseFrequency = std::clamp(freqHz, 1.0f, 20000.0f);
    updateActiveCount();
    updateFrequencies();
    recalculateNormalization();
}

void PartialBank::setNumPartials(int numPartials)
{
    requestedPartials = std::clamp(numPartials, 1, kMaxPartials);
    updateActiveCount();
    recalculateNormalization();
}

void PartialBank::setRatios(const std::array<float, kMaxPartials>& newRatios)
{
    ratios = newRatios;
    updateFrequencies();
}

void PartialBank::setInharmonicity(float value)
{
    inharmonicity = std::max(0.0f, value);
    updateFrequencies();
}

void PartialBank::setOffset(float value)
{
    offset = std::clamp(value, -1.0f, 1.0f);
    updateFrequencies();
}

void PartialBank::setStretch(float value)
{
    stretch = std::clamp(value, 0.5f, 1.5f);
    updateFrequencies();
}

void PartialBank::setOddEvenBalance(float value)
{
    oddEvenBalance = std::clamp(value, -1.0f, 1.0f);
    updateFrequencies();
}

void PartialBank::setCoefficients(const std::array<float, kMaxPartials>& amps)
{
    amplitudes = amps;
    for (int i = 0; i < kMaxPartials; ++i)
        partials[i].setAmplitude(amplitudes[i]);
    recalculateNormalization();
}

void PartialBank::setBandCrossovers(const std::array<float, kNumCrossovers>& crossoverHz)
{
    crossovers = crossoverHz;
    updateFrequencies(); // reassign partials to bands
}

// randomly init phase of partials, avoids sudden spike in amplitude at start
void PartialBank::randomizePhases(juce::Random& rng)
{
    for (int i = 0; i < numActive; ++i)
    {
        const float phase = rng.nextFloat() * 2*M_PI;
        partials[i].setPhase(phase);
    }
}

// limit the actual number of partials based on kMaxPartials and Nyquist rate
void PartialBank::updateActiveCount()
{
    if (baseFrequency <= 0.0f || sampleRate <= 0.0)
    {
        numActive = 0;
        return;
    }

    const float nyquist = static_cast<float>(sampleRate) * 0.5f;
    const int maxByNyquist = static_cast<int>(std::floor(nyquist / baseFrequency));
    const int maxAllowed = std::max(0, std::min(kMaxPartials, maxByNyquist));
    numActive = std::clamp(requestedPartials, 0, maxAllowed);
}

// set frequency for each partial
void PartialBank::updateFrequencies()
{
    // adds pitch-dependent inharmonicity
    const float effectiveB = (baseFrequency > 0.0f)
                                 ? (inharmonicity / baseFrequency)
                                 : 0.0f;

    for (int i = 0; i < kMaxPartials; ++i)
    {
        // reshape the harmonic series before applying inharmonicity:
        //   stretch around the fundamental, add a global offset, then split odd/even
        float ratio = 1.0f + (ratios[i] - 1.0f) * stretch; // stretch keeps partial 1 anchored
        ratio += offset;                                    // constant shift for all partials
        const float oddEvenSign = ((i + 1) % 2 != 0) ? 1.0f : -1.0f; // partial 1,3,5 = odd
        ratio += oddEvenBalance * oddEvenSign;

        const float stretchedRatio = ratio * std::sqrt(1.0f + effectiveB * ratio * ratio);// shift from perfect harmonic k*f0
        const float freq = baseFrequency * std::max(0.0f, stretchedRatio);
        partials[i].setFrequency(freq);

        // assign to a frequency band (0..kNumBands-1) by comparing against crossovers
        int band = 0;
        while (band < kNumCrossovers && freq >= crossovers[static_cast<size_t>(band)])
            ++band;
        bandOf[i] = band;
    }
}

void PartialBank::recalculateNormalization()
{
    if (numActive <= 0) { normFactor = 0.0f; return; }
    float sumAmps = 0.0f;
    for (int i = 0; i < numActive; ++i)
        sumAmps += amplitudes[static_cast<size_t>(i)];
    normFactor = 1.0f / std::max(1.0f, sumAmps);
}
}
