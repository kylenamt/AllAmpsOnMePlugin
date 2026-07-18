#pragma once

// Resampler — JUCE-free sample-rate conversion so the 48 kHz model can run in a
// host at any sample rate. `Resampler48k` wraps a mono processing callback:
// host block -> upsample to 48 kHz -> process -> downsample back to host, always
// returning exactly the host block length. Catmull-Rom (cubic) interpolation;
// good enough for M5 polish, upgradeable to windowed-sinc later.
//
// Kept dependency-free so it is exercised by tools/selftest.cpp.

#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

namespace aaom
{

// Single-direction streaming cubic resampler with an internal reservoir.
class StreamResampler
{
public:
    // ratioInPerOut = inputRate / outputRate (input samples advanced per output).
    void reset(double ratioInPerOut, int capacity)
    {
        inPerOut_ = ratioInPerOut > 0.0 ? ratioInPerOut : 1.0;
        cap_ = capacity > 8 ? capacity : 8;
        ring_.assign(static_cast<std::size_t>(cap_), 0.0f);
        head_ = 0;
        count_ = 0;
        phase_ = 0.0;
        // Prime with 3 zeros so the 4-tap kernel has history from the first output.
        push(nullptr, 3);
    }

    void push(const float* in, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            if (count_ >= cap_)
            {
                // Reservoir overflow (shouldn't happen when sized correctly): drop oldest.
                head_ = (head_ + 1) % cap_;
                --count_;
            }
            const int w = (head_ + count_) % cap_;
            ring_[static_cast<std::size_t>(w)] = in ? in[i] : 0.0f;
            ++count_;
        }
    }

    // Produce up to maxOut samples; returns the number actually produced (limited
    // by available input). Consumes input from the reservoir as it advances.
    int produce(float* out, int maxOut)
    {
        int produced = 0;
        while (produced < maxOut && count_ >= 4)
        {
            const float p0 = at(0), p1 = at(1), p2 = at(2), p3 = at(3);
            out[produced++] = catmull(p0, p1, p2, p3, static_cast<float>(phase_));
            phase_ += inPerOut_;
            while (phase_ >= 1.0 && count_ >= 4)
            {
                phase_ -= 1.0;
                head_ = (head_ + 1) % cap_;
                --count_;
            }
            if (phase_ >= 1.0)
                break; // need more input to advance further
        }
        return produced;
    }

private:
    float at(int k) const { return ring_[static_cast<std::size_t>((head_ + k) % cap_)]; }

    static float catmull(float p0, float p1, float p2, float p3, float t)
    {
        // Catmull-Rom spline evaluated at t in [0,1] between p1 and p2.
        const float a = -0.5f * p0 + 1.5f * p1 - 1.5f * p2 + 0.5f * p3;
        const float b = p0 - 2.5f * p1 + 2.0f * p2 - 0.5f * p3;
        const float c = -0.5f * p0 + 0.5f * p2;
        return ((a * t + b) * t + c) * t + p1;
    }

    double inPerOut_ = 1.0;
    double phase_ = 0.0;
    std::vector<float> ring_;
    int cap_ = 0;
    int head_ = 0;
    int count_ = 0;
};

// Wraps a mono process callback with host<->48k conversion.
class Resampler48k
{
public:
    static constexpr double kModelRate = 48000.0;

    // Returns true if resampling is engaged (host rate differs from 48 kHz).
    bool active() const { return active_; }

    // Max number of 48 kHz frames a host block can expand to (to size buffers /
    // prepare the inner model).
    int maxInnerBlock() const { return maxInner_; }

    // Approximate added latency in host samples (small priming delay).
    int latencySamples() const { return active_ ? 4 : 0; }

    void prepare(double hostRate, int maxHostBlock)
    {
        hostRate_ = hostRate > 0.0 ? hostRate : kModelRate;
        maxHostBlock_ = maxHostBlock > 0 ? maxHostBlock : 1;
        active_ = std::fabs(hostRate_ - kModelRate) > 1.0;

        if (!active_)
        {
            maxInner_ = maxHostBlock_;
            inner_.clear();
            return;
        }

        // Upsample host -> 48k: inPerOut = host/48k.
        const double upRatio = hostRate_ / kModelRate;
        // Downsample 48k -> host: inPerOut = 48k/host.
        const double downRatio = kModelRate / hostRate_;

        maxInner_ = static_cast<int>(std::ceil(maxHostBlock_ / upRatio)) + 4;
        inner_.assign(static_cast<std::size_t>(maxInner_), 0.0f);

        const int upCap = maxInner_ + maxHostBlock_ + 16;
        const int downCap = maxInner_ + maxHostBlock_ + 16;
        up_.reset(upRatio, upCap);
        down_.reset(downRatio, downCap);
    }

    // Process n host samples in `in`, write n host samples to `out`. `proc48`
    // is called as proc48(float* buf, int m) to process m 48 kHz mono frames
    // in place. in may equal out.
    template <typename Proc48>
    void process(const float* in, float* out, int n, Proc48&& proc48)
    {
        if (!active_)
        {
            proc48Copy(in, out, n);
            std::forward<Proc48>(proc48)(out, n);
            return;
        }

        up_.push(in, n);
        const int m = up_.produce(inner_.data(), maxInner_);

        std::forward<Proc48>(proc48)(inner_.data(), m);

        down_.push(inner_.data(), m);
        const int got = down_.produce(out, n);
        // Pad any startup shortfall with the last produced sample (rare, <=latency).
        for (int i = got; i < n; ++i)
            out[i] = got > 0 ? out[got - 1] : 0.0f;
    }

private:
    static void proc48Copy(const float* in, float* out, int n)
    {
        if (in != out)
            for (int i = 0; i < n; ++i)
                out[i] = in[i];
    }

    bool active_ = false;
    double hostRate_ = kModelRate;
    int maxHostBlock_ = 0;
    int maxInner_ = 0;
    std::vector<float> inner_;
    StreamResampler up_;
    StreamResampler down_;
};

} // namespace aaom
