#pragma once

// FftConvolver — JUCE-free FFT convolution for the cabinet IR stage.
//
// Uniformly partitioned overlap-save, split into a direct-form *head* and an
// FFT *tail* so the whole thing is zero-latency:
//
//   y[n] = SUM_{j<P} h[j] x[n-j]        <- head, time domain, needs x[n] now
//        + SUM_{j>=P} h[j] x[n-j]       <- tail, only needs x up to n-P
//
// The tail therefore has a full partition of slack: at every P-sample boundary
// one forward FFT + K spectral multiply-accumulates + one inverse FFT produce
// the tail contribution for the *next* P samples, using input that is already
// complete. Nothing has to be delayed to make the maths work, so the plugin
// keeps reporting the resampler's latency alone and a cab IR can be loaded or
// dropped without the host re-syncing.
//
// Cost per sample is P multiply-adds for the head plus ~K*N/P complex MACs for
// the tail, versus L for brute force; P is picked from the IR length so K stays
// bounded (see kMaxPartitions in the .cpp).
//
// Kept dependency-free so it is exercised by tools/selftest.cpp.

#include <vector>

namespace aaom
{

// Minimal iterative radix-2 complex FFT over split real/imaginary arrays.
// Real input costs 2x what a dedicated real transform would, which is not worth
// optimising here: the spectral MACs dominate as soon as an IR spans more than
// a couple of partitions.
class Fft
{
public:
    void setSize(int n); // n must be a power of two
    int size() const { return n_; }

    void forward(float* re, float* im) const; // in place
    void inverse(float* re, float* im) const; // in place, includes the 1/N scale

private:
    int n_ = 0;
    std::vector<int> rev_;         // bit-reversal permutation
    std::vector<float> cos_, sin_; // twiddles, N/2 entries
};

class FftConvolver
{
public:
    // Install an impulse response. Allocates and resets the running state, so
    // this is a message-thread operation (the processor suspends audio around
    // it). A null or empty IR clears the convolver.
    void setImpulseResponse(const float* ir, int numSamples);

    void clear();         // drop the IR entirely
    void resetState();    // zero the delay lines, keep the IR

    bool ready() const { return irLength_ > 0; }
    int irLength() const { return irLength_; }
    int partitionSize() const { return partition_; }
    int numPartitions() const { return numPartitions_; } // FFT partitions, head excluded

    // Mono, arbitrary block length, `in` may alias `out`. No allocation.
    void process(const float* in, float* out, int numSamples);

private:
    void runFftStep();

    Fft fft_;

    int irLength_ = 0;
    int partition_ = 0; // P
    int fftSize_ = 0;   // N = 2P
    int numBins_ = 0;   // N/2 + 1 (the rest is the conjugate mirror)
    int numPartitions_ = 0;

    std::vector<float> head_; // first P taps, applied directly

    // Partition spectra, numPartitions_ * numBins_ bins.
    std::vector<float> filterRe_, filterIm_;
    // Frequency delay line of past input windows, same layout.
    std::vector<float> fdlRe_, fdlIm_;
    int fdlPos_ = 0;

    std::vector<float> hist_; // 2P input window: [previous block | block being filled]
    int fill_ = 0;            // samples written into the second half
    std::vector<float> tail_; // tail contribution for the P samples being emitted

    std::vector<float> scratchRe_, scratchIm_; // N
    std::vector<float> accRe_, accIm_;         // numBins_
};

} // namespace aaom
