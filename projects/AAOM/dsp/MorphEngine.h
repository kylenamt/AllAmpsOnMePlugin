#pragma once

// MorphEngine — the real-time morph layer on top of MorphModel (JUCE-free).
//
// Holds up to 4 "corner" embeddings arranged on a 2D XY pad. A dot at (x,y) in
// [0,1]^2 bilinearly blends the assigned corners; the result is hot-swapped onto
// the live NAM WaveNet. All per-block work is preallocated and allocation-free.
//
// The blend happens in *weight* space, not embedding space. MorphModel's fold is
// affine in the embedding (FiLM: gamma(e) is affine and scales W; delta: W+dW(e)
// with dW affine), and affine maps commute with affine combinations, so for
// bilinear weights w_i summing to 1:
//
//     fold( sum_i w_i * corner_i )  ==  sum_i w_i * fold(corner_i)     exactly
//
// So each corner is folded *once*, when it is assigned, and every block does a
// 4-way weighted sum of the cached streams instead of a full fold. That is ~2x
// cheaper than folding a blended embedding under FiLM, ~3x under delta, and --
// the point -- costs the same for both, so the fold's per-architecture price is
// paid at corner-assignment time rather than once per block of every drag.
//
// The one-pole smoother likewise runs in weight space. It is LTI and the blend
// is linear, so smoothing the weights is identical to smoothing the embedding
// and re-folding; it additionally smooths a *corner reassignment*, which an
// embedding-space smoother would step through discontinuously.
//
// Threading: prepare() builds the DSP off the audio thread. setMorph() and
// setCorner*/clearCorner may be called from the audio thread (e.g. from MRTA
// parameter callbacks) — they only touch engine-owned buffers. processBlock()
// runs on the audio thread.

#include <array>
#include <memory>
#include <vector>

#include "CornerFifo.h"
#include "MorphModel.h"

namespace aaom
{

class MorphEngine
{
public:
    static constexpr int kNumCorners = 4;

    MorphEngine() = default;

    // Take ownership of a parsed model. Initialises corner 0 with the model's
    // first profile (typically "Table mean"); further profiles seed corners 1..3
    // if present. Must be called before prepare().
    void setModel(std::unique_ptr<MorphModel> model);

    MorphModel* model() { return model_.get(); }
    const MorphModel* model() const { return model_.get(); }
    bool ready() const { return model_ != nullptr; }

    // Build the DSP at the current morph position. Off the audio thread.
    void prepare(double sampleRate, int maxBlockSize);

    // Assign / clear a corner embedding directly (NOT real-time safe). Use only
    // before the audio thread is running (construction / prepare). Length must
    // equal the model's embedding_dim. Corners with nothing assigned contribute
    // nothing and the bilinear weights renormalise over the assigned corners.
    void setCorner(int index, const std::vector<float>& embedding);
    void clearCorner(int index);
    bool cornerAssigned(int index) const;

    // Real-time-safe corner updates: call from the message thread while audio is
    // running. The change is applied on the audio thread at the next block.
    void queueCorner(int index, const std::vector<float>& embedding);
    void queueClear(int index);

    // Set the dot position (audio thread safe). Clamped to [0,1].
    void setMorph(float x, float y);

    // One-pole smoothing time constant for the embedding morph.
    void setSmoothingTimeMs(float ms);

    // Process num_frames mono samples. Re-blends/hot-swaps weights first if the
    // dot or a corner has moved. in may equal out.
    void processBlock(NAM_SAMPLE* in, NAM_SAMPLE* out, int numFrames);

    // Reset DSP state (e.g. transport reset). Off the audio thread ideally.
    void reset();

private:
    // Normalised bilinear weights over the *assigned* corners. False when nothing
    // is assigned near the dot, in which case the caller holds its current weights.
    bool computeBlendWeights(float (&w)[kNumCorners]) const;

    // blendTarget_ = sum_i w[i] * foldedCorners_[i]; caches w into lastBlendW_.
    void rebuildBlendTarget(const float (&w)[kNumCorners]);

    void computeTarget();          // fills target_ from corners + (x_,y_); prepare() only

    std::unique_ptr<MorphModel> model_;

    std::array<std::vector<float>, kNumCorners> corners_;
    std::array<bool, kNumCorners> assigned_{{false, false, false, false}};
    // Corner assigned/cleared but not yet re-folded. Drained one per block so a
    // preset load (all four at once) cannot pile four folds into one callback.
    std::array<bool, kNumCorners> cornerDirty_{{false, false, false, false}};

    CornerFifo cornerFifo_;

    std::vector<float> target_;    // E; only prepare() needs the embedding itself

    // Weight-space state, all namWeightCount() long and preallocated by setModel().
    std::array<std::vector<float>, kNumCorners> foldedCorners_;
    std::vector<float> blendTarget_;    // where the smoother is heading
    std::vector<float> currentWeights_; // what the live WaveNet currently holds
    float lastBlendW_[kNumCorners] = {0.0f, 0.0f, 0.0f, 0.0f};
    bool blendValid_ = false; // lastBlendW_/blendTarget_ reflect the current corners
    bool settled_ = true;     // currentWeights_ == blendTarget_, nothing to push

    float x_ = 0.5f;
    float y_ = 0.5f;
    float smoothingMs_ = 30.0f;
    double sampleRate_ = 48000.0;
    int maxBlockSize_ = 0;
    float smoothCoeffPerBlock_ = 1.0f;
    bool prepared_ = false;
};

} // namespace aaom
