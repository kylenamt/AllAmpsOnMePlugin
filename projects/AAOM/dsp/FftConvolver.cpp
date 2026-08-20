#include "FftConvolver.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace aaom
{

namespace
{
// Partition size bounds. The head costs P multiply-adds per sample and the tail
// costs roughly one complex MAC per bin per partition, so P trades one against
// the other: a small P keeps the head cheap but multiplies the spectral MACs.
// Growing P until the partition count is bounded stops a long IR from turning
// into a wall of accumulation.
constexpr int kMinPartition = 128;
constexpr int kMaxPartition = 1024;
constexpr int kMaxPartitions = 24;

int tailPartitions(int irLength, int partition)
{
    const int tail = irLength - partition;
    return tail > 0 ? (tail + partition - 1) / partition : 0;
}

int choosePartition(int irLength)
{
    int p = kMinPartition;
    while (p < kMaxPartition && tailPartitions(irLength, p) > kMaxPartitions)
        p *= 2;
    return p;
}
} // namespace

//==============================================================================

void Fft::setSize(int n)
{
    if (n_ == n)
        return;

    n_ = n;

    int bits = 0;
    while ((1 << bits) < n)
        ++bits;

    rev_.assign(static_cast<std::size_t>(n), 0);
    for (int i = 0; i < n; ++i)
    {
        int r = 0;
        for (int b = 0; b < bits; ++b)
            if ((i & (1 << b)) != 0)
                r |= 1 << (bits - 1 - b);
        rev_[static_cast<std::size_t>(i)] = r;
    }

    cos_.assign(static_cast<std::size_t>(n / 2), 0.0f);
    sin_.assign(static_cast<std::size_t>(n / 2), 0.0f);
    for (int k = 0; k < n / 2; ++k)
    {
        const double a = 2.0 * M_PI * k / n;
        cos_[static_cast<std::size_t>(k)] = static_cast<float>(std::cos(a));
        sin_[static_cast<std::size_t>(k)] = static_cast<float>(std::sin(a));
    }
}

void Fft::forward(float* re, float* im) const
{
    for (int i = 0; i < n_; ++i)
    {
        const int j = rev_[static_cast<std::size_t>(i)];
        if (j > i)
        {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }

    for (int len = 2; len <= n_; len <<= 1)
    {
        const int half = len >> 1;
        const int step = n_ / len;
        for (int base = 0; base < n_; base += len)
        {
            for (int j = 0; j < half; ++j)
            {
                // Twiddle e^(-2*pi*i*j/len).
                const float wr = cos_[static_cast<std::size_t>(j * step)];
                const float wi = -sin_[static_cast<std::size_t>(j * step)];
                const int a = base + j;
                const int b = a + half;
                const float xr = re[b] * wr - im[b] * wi;
                const float xi = re[b] * wi + im[b] * wr;
                re[b] = re[a] - xr;
                im[b] = im[a] - xi;
                re[a] += xr;
                im[a] += xi;
            }
        }
    }
}

void Fft::inverse(float* re, float* im) const
{
    // IDFT(X) = conj(DFT(conj(X))) / N.
    for (int i = 0; i < n_; ++i)
        im[i] = -im[i];

    forward(re, im);

    const float scale = 1.0f / static_cast<float>(n_);
    for (int i = 0; i < n_; ++i)
    {
        re[i] *= scale;
        im[i] *= -scale;
    }
}

//==============================================================================

void FftConvolver::clear()
{
    irLength_ = 0;
    partition_ = 0;
    fftSize_ = 0;
    numBins_ = 0;
    numPartitions_ = 0;
    fdlPos_ = 0;
    fill_ = 0;

    head_.clear();
    filterRe_.clear();
    filterIm_.clear();
    fdlRe_.clear();
    fdlIm_.clear();
    hist_.clear();
    tail_.clear();
    scratchRe_.clear();
    scratchIm_.clear();
    accRe_.clear();
    accIm_.clear();
}

void FftConvolver::resetState()
{
    std::fill(hist_.begin(), hist_.end(), 0.0f);
    std::fill(tail_.begin(), tail_.end(), 0.0f);
    std::fill(fdlRe_.begin(), fdlRe_.end(), 0.0f);
    std::fill(fdlIm_.begin(), fdlIm_.end(), 0.0f);
    fdlPos_ = 0;
    fill_ = 0;
}

void FftConvolver::setImpulseResponse(const float* ir, int numSamples)
{
    if (ir == nullptr || numSamples <= 0)
    {
        clear();
        return;
    }

    irLength_ = numSamples;
    partition_ = choosePartition(numSamples);
    fftSize_ = partition_ * 2;
    numBins_ = fftSize_ / 2 + 1;
    numPartitions_ = tailPartitions(numSamples, partition_);

    // Head: the first P taps, run as a plain FIR so the output for sample n is
    // available the moment n arrives.
    head_.assign(static_cast<std::size_t>(partition_), 0.0f);
    std::copy(ir, ir + std::min(partition_, numSamples), head_.begin());

    fft_.setSize(fftSize_);

    scratchRe_.assign(static_cast<std::size_t>(fftSize_), 0.0f);
    scratchIm_.assign(static_cast<std::size_t>(fftSize_), 0.0f);
    accRe_.assign(static_cast<std::size_t>(numBins_), 0.0f);
    accIm_.assign(static_cast<std::size_t>(numBins_), 0.0f);

    // Tail: h[P..L) split into P-sized partitions, each zero-padded to N and
    // transformed once. Only the first N/2+1 bins are kept — the input is real,
    // so the rest is the conjugate mirror and is rebuilt before the inverse.
    filterRe_.assign(static_cast<std::size_t>(numPartitions_ * numBins_), 0.0f);
    filterIm_.assign(static_cast<std::size_t>(numPartitions_ * numBins_), 0.0f);
    for (int k = 0; k < numPartitions_; ++k)
    {
        const int offset = partition_ + k * partition_;
        const int len = std::min(partition_, numSamples - offset);

        std::fill(scratchRe_.begin(), scratchRe_.end(), 0.0f);
        std::fill(scratchIm_.begin(), scratchIm_.end(), 0.0f);
        std::copy(ir + offset, ir + offset + len, scratchRe_.begin());
        fft_.forward(scratchRe_.data(), scratchIm_.data());

        const std::size_t base = static_cast<std::size_t>(k * numBins_);
        std::copy(scratchRe_.begin(), scratchRe_.begin() + numBins_, filterRe_.begin() + base);
        std::copy(scratchIm_.begin(), scratchIm_.begin() + numBins_, filterIm_.begin() + base);
    }

    fdlRe_.assign(static_cast<std::size_t>(std::max(1, numPartitions_) * numBins_), 0.0f);
    fdlIm_.assign(static_cast<std::size_t>(std::max(1, numPartitions_) * numBins_), 0.0f);
    hist_.assign(static_cast<std::size_t>(fftSize_), 0.0f);
    tail_.assign(static_cast<std::size_t>(partition_), 0.0f);

    resetState();
}

void FftConvolver::process(const float* in, float* out, int numSamples)
{
    if (!ready())
    {
        if (in != out)
            std::memcpy(out, in, static_cast<std::size_t>(numSamples) * sizeof(float));
        return;
    }

    int done = 0;
    while (done < numSamples)
    {
        const int chunk = std::min(numSamples - done, partition_ - fill_);

        // Input is copied in first so `in` may alias `out`: the head FIR reads
        // its history out of hist_, never out of the caller's buffer.
        float* window = hist_.data() + partition_ + fill_;
        std::memcpy(window, in + done, static_cast<std::size_t>(chunk) * sizeof(float));

        for (int i = 0; i < chunk; ++i)
        {
            const float* x = window + i; // x[0] is the newest sample, x[-j] older
            float acc = 0.0f;
            for (int j = 0; j < partition_; ++j)
                acc += head_[static_cast<std::size_t>(j)] * x[-j];
            out[done + i] = acc + tail_[static_cast<std::size_t>(fill_ + i)];
        }

        fill_ += chunk;
        done += chunk;

        if (fill_ == partition_)
        {
            runFftStep();
            fill_ = 0;
        }
    }
}

void FftConvolver::runFftStep()
{
    if (numPartitions_ > 0)
    {
        // hist_ holds the 2P window ending at this block boundary. Because the
        // tail starts at tap P, that window is exactly what the *next* P output
        // samples need — the transform is always a full block ahead of use.
        std::copy(hist_.begin(), hist_.end(), scratchRe_.begin());
        std::fill(scratchIm_.begin(), scratchIm_.end(), 0.0f);
        fft_.forward(scratchRe_.data(), scratchIm_.data());

        const std::size_t slot = static_cast<std::size_t>(fdlPos_ * numBins_);
        std::copy(scratchRe_.begin(), scratchRe_.begin() + numBins_, fdlRe_.begin() + slot);
        std::copy(scratchIm_.begin(), scratchIm_.begin() + numBins_, fdlIm_.begin() + slot);

        std::fill(accRe_.begin(), accRe_.end(), 0.0f);
        std::fill(accIm_.begin(), accIm_.end(), 0.0f);
        for (int k = 0; k < numPartitions_; ++k)
        {
            const int index = (fdlPos_ - k + numPartitions_) % numPartitions_;
            const float* ur = fdlRe_.data() + index * numBins_;
            const float* ui = fdlIm_.data() + index * numBins_;
            const float* hr = filterRe_.data() + k * numBins_;
            const float* hi = filterIm_.data() + k * numBins_;
            for (int b = 0; b < numBins_; ++b)
            {
                accRe_[static_cast<std::size_t>(b)] += ur[b] * hr[b] - ui[b] * hi[b];
                accIm_[static_cast<std::size_t>(b)] += ur[b] * hi[b] + ui[b] * hr[b];
            }
        }

        // Mirror the half spectrum back to full length, then transform back and
        // keep the second half — the part of the block free of wrap-around.
        for (int b = 0; b < numBins_; ++b)
        {
            scratchRe_[static_cast<std::size_t>(b)] = accRe_[static_cast<std::size_t>(b)];
            scratchIm_[static_cast<std::size_t>(b)] = accIm_[static_cast<std::size_t>(b)];
        }
        for (int b = numBins_; b < fftSize_; ++b)
        {
            scratchRe_[static_cast<std::size_t>(b)] = accRe_[static_cast<std::size_t>(fftSize_ - b)];
            scratchIm_[static_cast<std::size_t>(b)] = -accIm_[static_cast<std::size_t>(fftSize_ - b)];
        }
        fft_.inverse(scratchRe_.data(), scratchIm_.data());
        std::copy(scratchRe_.begin() + partition_, scratchRe_.end(), tail_.begin());

        fdlPos_ = (fdlPos_ + 1) % numPartitions_;
    }

    // The block just filled becomes the older half of the next window.
    std::copy(hist_.begin() + partition_, hist_.end(), hist_.begin());
}

} // namespace aaom
