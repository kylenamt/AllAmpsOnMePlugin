// Benchmark: SineWavetable::lookup vs std::sin
//
// Mirrors the exact wavetable used in SineWavetable.h (2048-point table,
// linear interpolation, phase in [0, 2pi)) and simulates the Partial::processSample
// access pattern (sequential phase accumulation).
//
// Compile & run:
//   g++ -O2 -std=c++17 -o benchmark_sine benchmark_sine.cpp && ./benchmark_sine

#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>

// ---- Wavetable (mirrors SineWavetable.h) ------------------------------------

static constexpr int kTableSize = 2048;
static const float kPhaseStep = static_cast<float>(kTableSize) / (2.0f * static_cast<float>(M_PI));

static std::array<float, kTableSize + 1> buildTable()
{
    std::array<float, kTableSize + 1> t {};
    for (int i = 0; i <= kTableSize; ++i)
        t[i] = std::sin(2.0f * static_cast<float>(M_PI) * i / static_cast<float>(kTableSize));
    return t;
}

static const std::array<float, kTableSize + 1>& getTable()
{
    static const auto table = buildTable();
    return table;
}

inline float wavetableLookup(float phase)
{
    const auto& table = getTable();
    const float index = phase * kPhaseStep;
    const int i0      = std::clamp(static_cast<int>(index), 0, kTableSize - 1);
    const float frac  = index - static_cast<float>(i0);
    return table[i0] + frac * (table[i0 + 1] - table[i0]);
}

// ---- Benchmark helpers -------------------------------------------------------

using Clock = std::chrono::high_resolution_clock;

struct Result
{
    double ms;
    float checksum; // prevent dead-code elimination
};

// Simulates Partial::processSample for `numPartials` partials over `numSamples` samples.
// Each partial has a unique phaseDelta based on a harmonic series at 440 Hz / 48 kHz.
// voices x partials phase state
using PhaseArray = std::array<std::array<float, 512>, 8>;

template <typename SinFn>
Result run(SinFn sinFn, int numVoices, int numPartials, int numSamples, int repetitions)
{
    constexpr double sampleRate = 48000.0;
    constexpr float  twoPi      = 2.0f * static_cast<float>(M_PI);

    // Each voice plays a different MIDI note (a C-major chord spread across octaves)
    const std::array<float, 8> baseFreqs { 130.81f, 164.81f, 196.00f, 261.63f,
                                           329.63f, 392.00f, 523.25f, 659.25f };

    PhaseArray phases {};
    PhaseArray deltas {};
    for (int v = 0; v < numVoices; ++v)
        for (int p = 0; p < numPartials; ++p)
        {
            phases[v][p] = 0.0f;
            deltas[v][p] = twoPi * baseFreqs[v] * static_cast<float>(p + 1) / static_cast<float>(sampleRate);
        }

    float acc = 0.0f;
    auto t0 = Clock::now();

    for (int r = 0; r < repetitions; ++r)
    {
        for (int s = 0; s < numSamples; ++s)
        {
            for (int v = 0; v < numVoices; ++v)
            {
                for (int p = 0; p < numPartials; ++p)
                {
                    acc += sinFn(phases[v][p]);
                    phases[v][p] += deltas[v][p];
                    if (phases[v][p] >= twoPi)
                        phases[v][p] -= twoPi;
                }
            }
        }
    }

    auto t1 = Clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return { ms, acc };
}

// ---- Main -------------------------------------------------------------------

int main()
{
    // Warm up the wavetable cache
    (void)getTable();

    constexpr int numPartials  = 256;
    constexpr int numVoices    = 4;
    constexpr int numSamples   = 512;   // typical audio block size
    constexpr int repetitions  = 500;

    const long long totalCalls = static_cast<long long>(numVoices) * numPartials * numSamples * repetitions;

    std::cout << "\n=== Sine Benchmark ===\n";
    std::cout << "  Voices             : " << numVoices    << "\n";
    std::cout << "  Partials per voice : " << numPartials  << "\n";
    std::cout << "  Block size (samples): " << numSamples  << "\n";
    std::cout << "  Repetitions        : " << repetitions  << "\n";
    std::cout << "  Total sin() calls  : " << totalCalls   << "\n\n";

    auto wtResult  = run(wavetableLookup, numVoices, numPartials, numSamples, repetitions);
    auto stdResult = run([](float p){ return std::sin(p); }, numVoices, numPartials, numSamples, repetitions);

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  Wavetable : " << std::setw(8) << wtResult.ms  << " ms"
              << "  (checksum " << wtResult.checksum  << ")\n";
    std::cout << "  std::sin  : " << std::setw(8) << stdResult.ms << " ms"
              << "  (checksum " << stdResult.checksum << ")\n\n";

    const double speedup = stdResult.ms / wtResult.ms;
    std::cout << "  Speedup   : " << std::setprecision(2) << speedup << "x";
    if (speedup > 1.0)
        std::cout << "  (wavetable is faster)\n";
    else
        std::cout << "  (std::sin is faster at this optimisation level)\n";

    std::cout << "\nNote: run with -O0 to see worst-case, -O3 for best-case comparison.\n\n";
    return 0;
}
