#include "MorphEngine.h"

#include <algorithm>
#include <cmath>

namespace aaom
{

void MorphEngine::setModel(std::unique_ptr<MorphModel> model)
{
    model_ = std::move(model);
    assigned_.fill(false);

    if (model_ == nullptr)
    {
        for (auto& c : corners_)
            c.clear();
        return;
    }

    const int E = model_->embeddingDim();
    // Preallocate corner storage to E so the RT drain path never reallocates.
    for (auto& c : corners_)
        c.assign(E, 0.0f);
    target_.assign(E, 0.0f);
    current_.assign(E, 0.0f);
    lastFolded_.assign(E, 0.0f);
    cornerFifo_.resize(16, E);

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
}

void MorphEngine::clearCorner(int index)
{
    if (index < 0 || index >= kNumCorners)
        return;
    corners_[index].clear();
    assigned_[index] = false;
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
    x_ = std::clamp(x, 0.0f, 1.0f);
    y_ = std::clamp(y, 0.0f, 1.0f);
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

void MorphEngine::computeTarget()
{
    const int E = model_->embeddingDim();
    if (static_cast<int>(target_.size()) != E)
        target_.assign(E, 0.0f);

    // Bilinear weights over the 4 corners: bottom-left, bottom-right, top-left,
    // top-right. Unassigned corners drop out and the rest renormalise.
    float w[kNumCorners] = {(1.0f - x_) * (1.0f - y_), x_ * (1.0f - y_), (1.0f - x_) * y_, x_ * y_};
    float sum = 0.0f;
    for (int i = 0; i < kNumCorners; ++i)
    {
        if (!assigned_[i])
            w[i] = 0.0f;
        sum += w[i];
    }

    std::fill(target_.begin(), target_.end(), 0.0f);
    if (sum <= 1.0e-12f)
        return; // nothing assigned near the dot: hold zero (keeps current via smoothing)

    const float inv = 1.0f / sum;
    for (int i = 0; i < kNumCorners; ++i)
    {
        if (w[i] == 0.0f)
            continue;
        const float wi = w[i] * inv;
        const float* c = corners_[i].data();
        for (int e = 0; e < E; ++e)
            target_[e] += wi * c[e];
    }
}

void MorphEngine::snapCurrentToTarget()
{
    current_ = target_;
    currentValid_ = true;
}

void MorphEngine::prepare(double sampleRate, int maxBlockSize)
{
    if (model_ == nullptr)
        return;

    sampleRate_ = sampleRate;
    maxBlockSize_ = std::max(1, maxBlockSize);
    setSmoothingTimeMs(smoothingMs_);

    computeTarget();
    snapCurrentToTarget();
    lastFolded_ = current_;

    model_->buildDsp(current_, sampleRate_, maxBlockSize_);
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

    const int E = model_->embeddingDim();

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
    });

    computeTarget();

    if (!currentValid_)
        snapCurrentToTarget();

    // One-pole smooth current -> target and track how far it has moved since the
    // weights were last folded.
    const float a = smoothCoeffPerBlock_;
    float maxDelta = 0.0f;
    for (int e = 0; e < E; ++e)
    {
        current_[e] += a * (target_[e] - current_[e]);
        maxDelta = std::max(maxDelta, std::fabs(current_[e] - lastFolded_[e]));
    }

    if (maxDelta > 1.0e-7f)
    {
        model_->setEmbedding(current_);
        lastFolded_ = current_;
    }

    model_->process(in, out, numFrames);
}

} // namespace aaom
