#include "MorphEngine.h"

#include <algorithm>
#include <cmath>

namespace aaom
{
namespace
{
// Below this the one-pole has effectively arrived: snap, push once more, and stop
// touching the weights until the dot or a corner moves again.
constexpr float kSettleEps = 1.0e-9f;
} // namespace

void MorphEngine::setModel(std::unique_ptr<MorphModel> model)
{
    model_ = std::move(model);
    assigned_.fill(false);
    cornerDirty_.fill(false);
    blendValid_ = false;
    settled_ = true;

    if (model_ == nullptr)
    {
        for (auto& c : corners_)
            c.clear();
        for (auto& f : foldedCorners_)
            f.clear();
        return;
    }

    const int E = model_->embeddingDim();
    // Preallocate corner storage to E so the RT drain path never reallocates.
    for (auto& c : corners_)
        c.assign(E, 0.0f);
    target_.assign(E, 0.0f);
    cornerFifo_.resize(16, E);

    // Weight-space buffers. assign() gives them their full length up front, so the
    // foldEmbedding() calls on the audio thread only ever clear() and push_back()
    // into existing capacity.
    const std::size_t nw = model_->namWeightCount();
    for (auto& f : foldedCorners_)
        f.assign(nw, 0.0f);
    blendTarget_.assign(nw, 0.0f);
    currentWeights_.assign(nw, 0.0f);

    // Seed corners from available profiles: corner 0 gets the first profile
    // (usually "Table mean"), corners 1..3 the next few if present.
    const auto& profiles = model_->profiles();
    for (int i = 0; i < kNumCorners && i < static_cast<int>(profiles.size()); ++i)
        setCorner(i, profiles[i].embedding);
}

void MorphEngine::setCorner(int index, const std::vector<float>& embedding)
{
    if (model_ == nullptr || index < 0 || index >= kNumCorners)
        return;
    if (static_cast<int>(embedding.size()) != model_->embeddingDim())
        return; // silently ignore mismatched dim; the UI validates before calling
    corners_[index] = embedding;
    assigned_[index] = true;
    cornerDirty_[index] = true;
}

void MorphEngine::clearCorner(int index)
{
    if (index < 0 || index >= kNumCorners)
        return;
    corners_[index].clear();
    assigned_[index] = false;
    cornerDirty_[index] = true;
}

bool MorphEngine::cornerAssigned(int index) const
{
    return index >= 0 && index < kNumCorners && assigned_[index];
}

void MorphEngine::queueCorner(int index, const std::vector<float>& embedding)
{
    if (model_ == nullptr || index < 0 || index >= kNumCorners)
        return;
    if (static_cast<int>(embedding.size()) != model_->embeddingDim())
        return;
    cornerFifo_.push(index, embedding.data(), static_cast<int>(embedding.size()), false);
}

void MorphEngine::queueClear(int index)
{
    if (index < 0 || index >= kNumCorners)
        return;
    cornerFifo_.push(index, nullptr, 0, true);
}

void MorphEngine::setMorph(float x, float y)
{
    x_ = std::clamp(x, -1.0f, 2.0f);
    y_ = std::clamp(y, -1.0f, 2.0f);
}

void MorphEngine::setSmoothingTimeMs(float ms)
{
    smoothingMs_ = std::max(0.0f, ms);
    // Per-block one-pole coefficient. Approx: coeff = 1 - exp(-blockDur / tau).
    if (smoothingMs_ <= 0.0f || maxBlockSize_ <= 0 || sampleRate_ <= 0.0)
    {
        smoothCoeffPerBlock_ = 1.0f;
        return;
    }
    const double blockDurMs = 1000.0 * static_cast<double>(maxBlockSize_) / sampleRate_;
    smoothCoeffPerBlock_ = static_cast<float>(1.0 - std::exp(-blockDurMs / smoothingMs_));
    smoothCoeffPerBlock_ = std::clamp(smoothCoeffPerBlock_, 0.0f, 1.0f);
}

bool MorphEngine::computeBlendWeights(float (&w)[kNumCorners]) const
{
    // Bilinear weights over the 4 corners: bottom-left, bottom-right, top-left,
    // top-right. These four polynomials sum to 1 for any x,y (not just inside
    // [0,1]), so a dot outside the corner square extrapolates -- some weights
    // go negative -- rather than clamping to the nearest corner. Unassigned
    // corners drop out and the rest renormalise, so the returned weights always
    // sum to 1 -- which is what makes blending folded corners equal to folding
    // the blended embedding.
    w[0] = (1.0f - x_) * (1.0f - y_);
    w[1] = x_ * (1.0f - y_);
    w[2] = (1.0f - x_) * y_;
    w[3] = x_ * y_;

    float sum = 0.0f;
    for (int i = 0; i < kNumCorners; ++i)
    {
        if (!assigned_[i])
            w[i] = 0.0f;
        sum += w[i];
    }

    if (sum <= 1.0e-12f)
    {
        for (int i = 0; i < kNumCorners; ++i)
            w[i] = 0.0f;
        return false; // nothing assigned near the dot
    }

    const float inv = 1.0f / sum;
    for (int i = 0; i < kNumCorners; ++i)
        w[i] *= inv;
    return true;
}

void MorphEngine::rebuildBlendTarget(const float (&w)[kNumCorners])
{
    const std::size_t n = blendTarget_.size();
    std::fill(blendTarget_.begin(), blendTarget_.end(), 0.0f);
    for (int i = 0; i < kNumCorners; ++i)
    {
        if (w[i] == 0.0f)
            continue;
        const float wi = w[i];
        const float* src = foldedCorners_[i].data();
        for (std::size_t t = 0; t < n; ++t)
            blendTarget_[t] += wi * src[t];
    }
    std::copy(w, w + kNumCorners, lastBlendW_);
    blendValid_ = true;
}

void MorphEngine::computeTarget()
{
    const int E = model_->embeddingDim();
    if (static_cast<int>(target_.size()) != E)
        target_.assign(E, 0.0f);

    float w[kNumCorners];
    const bool any = computeBlendWeights(w);

    std::fill(target_.begin(), target_.end(), 0.0f);
    if (!any)
        return;

    for (int i = 0; i < kNumCorners; ++i)
    {
        if (w[i] == 0.0f)
            continue;
        const float* c = corners_[i].data();
        for (int e = 0; e < E; ++e)
            target_[e] += w[i] * c[e];
    }
}

void MorphEngine::prepare(double sampleRate, int maxBlockSize)
{
    if (model_ == nullptr)
        return;

    sampleRate_ = sampleRate;
    maxBlockSize_ = std::max(1, maxBlockSize);
    setSmoothingTimeMs(smoothingMs_);

    computeTarget();
    model_->buildDsp(target_, sampleRate_, maxBlockSize_);

    // Fold every assigned corner once. From here the audio thread only ever
    // blends these streams; it re-folds solely when a corner is reassigned.
    float w[kNumCorners];
    const bool any = computeBlendWeights(w);
    for (int i = 0; i < kNumCorners; ++i)
    {
        cornerDirty_[i] = false;
        if (assigned_[i])
            model_->foldEmbedding(corners_[i], foldedCorners_[i]);
    }

    if (any)
        rebuildBlendTarget(w);
    else
        model_->foldEmbedding(target_, blendTarget_); // degenerate: no corners assigned

    // buildDsp loaded fold(blended embedding); the blend of folded corners is the
    // same number up to float rounding. Push the blend so "currentWeights_ is what
    // the WaveNet holds" is exact rather than nearly true.
    currentWeights_ = blendTarget_;
    model_->setWeights(currentWeights_);
    settled_ = true;
    prepared_ = true;
}

void MorphEngine::reset()
{
    if (model_)
        model_->reset(sampleRate_, maxBlockSize_);
}

void MorphEngine::processBlock(NAM_SAMPLE* in, NAM_SAMPLE* out, int numFrames)
{
    if (model_ == nullptr || !prepared_)
    {
        if (in != out && numFrames > 0)
            std::copy(in, in + numFrames, out);
        return;
    }

    // Apply any pending corner updates from the message thread (RT-safe).
    cornerFifo_.drain([this](int index, bool clear, const std::vector<float>& emb) {
        if (index < 0 || index >= kNumCorners)
            return;
        if (clear)
        {
            assigned_[index] = false;
        }
        else
        {
            corners_[index] = emb;
            assigned_[index] = true;
        }
        cornerDirty_[index] = true;
    });

    // Re-fold at most one dirty corner per block. A preset load dirties all four
    // at once and a fold costs several times a blend, so spreading them over a
    // few blocks keeps the worst-case callback bounded.
    for (int i = 0; i < kNumCorners; ++i)
    {
        if (!cornerDirty_[i])
            continue;
        if (assigned_[i])
            model_->foldEmbedding(corners_[i], foldedCorners_[i]);
        cornerDirty_[i] = false;
        blendValid_ = false; // a cached stream changed; the blend must be redone
        break;
    }

    float w[kNumCorners];
    if (computeBlendWeights(w))
    {
        bool moved = !blendValid_;
        for (int i = 0; i < kNumCorners && !moved; ++i)
            moved = (w[i] != lastBlendW_[i]);

        if (moved)
        {
            rebuildBlendTarget(w);
            settled_ = false;
        }
    }
    // else: nothing assigned near the dot -- hold whatever the WaveNet has.

    if (!settled_)
    {
        // One-pole toward the target, in weight space. Identical to smoothing the
        // embedding and re-folding (the fold is affine, the smoother is LTI), and
        // it also glides a corner reassignment instead of stepping to it.
        const float a = smoothCoeffPerBlock_;
        const std::size_t n = currentWeights_.size();
        float maxDelta = 0.0f;
        for (std::size_t t = 0; t < n; ++t)
        {
            const float d = blendTarget_[t] - currentWeights_[t];
            maxDelta = std::max(maxDelta, std::fabs(d));
            currentWeights_[t] += a * d;
        }
        if (maxDelta <= kSettleEps)
        {
            currentWeights_ = blendTarget_; // same length: no reallocation
            settled_ = true;
        }
        model_->setWeights(currentWeights_);
    }

    model_->process(in, out, numFrames);
}

} // namespace aaom
