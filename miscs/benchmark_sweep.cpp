// Benchmark sweep: SineWavetable::lookup vs std::sin across total oscillator load.
//
// Mirrors the 2048-point linearly-interpolated wavetable from SineWavetable.h and
// the Partial::processSample access pattern (sequential phase accumulation).
//
// Sweeps voices (1..8) x partials-per-voice (2,4,...,1024) and measures the time
// each method takes. The reported x quantity is the TOTAL number of oscillators
// (voices * partials), and y is the wall-clock time.
//
// Emits CSV to stdout: voices,partials,total,method,ms
//
// Compile & run (O3):
//   g++ -O3 -std=c++17 -o benchmark_sweep benchmark_sweep.cpp && ./benchmark_sweep > sweep.csv

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

// ---- Wavetable (mirrors SineWavetable.h) ------------------------------------

static constexpr float kPi = 3.14159265358979323846f;

static constexpr int kTableSize = 2048;
static const float kPhaseStep = static_cast<float>(kTableSize) / (2.0f * kPi);

static std::array<float, kTableSize + 1> buildTable()
{
    std::array<float, kTableSize + 1> t {};
    for (int i = 0; i <= kTableSize; ++i)
        t[i] = std::sin(2.0f * kPi * i / static_cast<float>(kTableSize));
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
    const int   i0    = std::clamp(static_cast<int>(index), 0, kTableSize - 1);
    const float frac  = index - static_cast<float>(i0);
    return table[i0] + frac * (table[i0 + 1] - table[i0]);
}

// ---- Benchmark --------------------------------------------------------------

using Clock = std::chrono::high_resolution_clock;

struct Result
{
    double ms;
    float  checksum; // prevents dead-code elimination
};

template <typename SinFn>
Result run(SinFn sinFn, int numVoices, int numPartials, int numSamples, int repetitions)
{
    constexpr double sampleRate = 48000.0;
    constexpr float  twoPi      = 2.0f * kPi;

    // Each voice plays a different MIDI note (a chord spread across octaves).
    const std::array<float, 8> baseFreqs { 130.81f, 164.81f, 196.00f, 261.63f,
                                           329.63f, 392.00f, 523.25f, 659.25f };

    const int total = numVoices * numPartials;
    std::vector<float> phases(static_cast<std::size_t>(total), 0.0f);
    std::vector<float> deltas(static_cast<std::size_t>(total), 0.0f);

    for (int v = 0; v < numVoices; ++v)
        for (int p = 0; p < numPartials; ++p)
            deltas[static_cast<std::size_t>(v) * numPartials + p]
                = twoPi * baseFreqs[v] * static_cast<float>(p + 1) / static_cast<float>(sampleRate);

    float acc = 0.0f;
    auto  t0  = Clock::now();

    for (int r = 0; r < repetitions; ++r)
        for (int s = 0; s < numSamples; ++s)
            for (int i = 0; i < total; ++i)
            {
                acc += sinFn(phases[i]);
                phases[i] += deltas[i];
                if (phases[i] >= twoPi)
                    phases[i] -= twoPi;
            }

    auto   t1 = Clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return { ms, acc };
}

// Volatile sink: forces the compiler to keep the sin results observable, so the
// sin calls are NOT eliminated as dead code (which would time an empty loop).
volatile float g_sink = 0.0f;

// Run a config a few times and keep the fastest (most stable) result.
template <typename SinFn>
double bestMs(SinFn sinFn, int numVoices, int numPartials, int numSamples, int repetitions, int trials)
{
    double best = 1e300;
    for (int t = 0; t < trials; ++t)
    {
        Result res = run(sinFn, numVoices, numPartials, numSamples, repetitions);
        g_sink += res.checksum; // consume the result so DCE can't drop the sin calls
        best = std::min(best, res.ms);
    }
    return best;
}

int main()
{
    (void) getTable(); // warm the table cache

    constexpr int numSamples  = 512; // audio block
    constexpr int repetitions = 100; // blocks per measurement
    constexpr int trials      = 3;   // keep fastest of N

    const std::array<int, 10> partialCounts { 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024 };

    std::printf("voices,partials,total,method,ms\n");

    for (int voices = 1; voices <= 8; ++voices)
        for (int partials : partialCounts)
        {
            const int total = voices * partials;

            const double wtMs  = bestMs(wavetableLookup,            voices, partials, numSamples, repetitions, trials);
            const double stdMs = bestMs([](float p){ return std::sin(p); }, voices, partials, numSamples, repetitions, trials);

            std::printf("%d,%d,%d,%s,%.4f\n", voices, partials, total, "wavetable", wtMs);
            std::printf("%d,%d,%d,%s,%.4f\n", voices, partials, total, "std_sin",   stdMs);
            std::fflush(stdout);
        }

    return 0;
}
