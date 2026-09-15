#pragma once

// MorphEngine — the real-time morph layer on top of MorphModel (JUCE-free).
//
// Holds up to 4 "corner" embeddings arranged on a 2D XY pad. A dot at (x,y)
// bilinearly blends the assigned corners; the result is hot-swapped onto the
// live NAM WaveNet. All per-block work is preallocated and allocation-free.
//
// (x,y) range over [-1,2], not just [0,1]: the corners live at the unit square
// as before, but the dot may leave it to linearly *extrapolate* past a corner
// (weights go negative), the 2D generalisation of x*a + (1-x)*b for x outside
// [0,1].
//
// The blend happens in *weight* space, not embedding space. MorphModel's fold is
// affine in the embedding (FiLM: gamma(e) is affine and scales W; delta: W+dW(e)
// with dW affine), and affine maps commute with affine combinations, so for
// bilinear weights w_i summing to 1 (true for *any* real x,y -- the four corner
// polynomials sum to 1 identically, not just inside the unit square):
//
//     fold( sum_i w_i * corner_i )  ==  sum_i w_i * fold(corner_i)     exactly
//
// so the fold-once/blend-per-block optimisation below stays exact under
// extrapolation too.
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
// Spherical (SLERP) mode -- setUseSlerp(), gated by the model itself via
// MorphModel::embeddingsNormalized() -- blends corners along the great-circle
// geodesic instead of the chord, which matters for a run whose embeddings were
// trained/exported to live on a fixed-radius hypersphere: linear blending cuts
// through the interior of that sphere and its extrapolation runs off it
// immediately, whereas SLERP's extrapolation continues along the same arc.
// SLERP is *not* affine, so it breaks the fold/blend commutativity above --
// blending folded corners is no longer equal to folding the blended embedding.
// So this mode cannot reuse the cheap weight-space blend: it blends corner
// *embeddings* (nested pairwise SLERP, matching the bilinear weight structure)
// and re-folds the result on every dot/corner change instead of every corner
// assignment. Corners are still folded into foldedCorners_ unconditionally so
// toggling back to linear is instant, without re-folding.
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

    // Set the dot position (audio thread safe). Clamped to [-1,2] -- [0,1] is
    // the corner square, the rest is linear extrapolation past it.
    void setMorph(float x, float y);

    // One-pole smoothing time constant for the embedding morph.
    void setSmoothingTimeMs(float ms);

    // Enable spherical (SLERP) blending in place of the default linear blend.
    // Audio-thread safe (see Threading above); only takes effect once the live
    // model reports embeddingsNormalized() -- see MorphModel -- so setting it
    // for a model that doesn't support it is a harmless no-op.
    void setUseSlerp(bool enabled) { useSlerp_ = enabled; }
    bool useSlerp() const { return useSlerp_; }

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

    // Spherical counterpart of rebuildBlendTarget(): nested pairwise SLERP of
    // the raw corner embeddings into target_, then a full re-fold into
    // blendTarget_ (no cached-stream shortcut available here -- see the class
    // doc). Also caches w into lastBlendW_ so the two modes share one "did
    // the blend move" trigger in processBlock()/prepare().
    void rebuildBlendTargetSlerp(const float (&w)[kNumCorners]);

    // useSlerp_ gated by the model's own capability, so a stale/automated
    // setting for a model that doesn't support it is silently inert.
    bool slerpActive() const { return useSlerp_ && model_ != nullptr && model_->embeddingsNormalized(); }

    void computeTarget();          // fills target_ from corners + (x_,y_); prepare() only

    std::unique_ptr<MorphModel> model_;

    std::array<std::vector<float>, kNumCorners> corners_;
    std::array<bool, kNumCorners> assigned_{{false, false, false, false}};
    // Corner assigned/cleared but not yet re-folded. Drained one per block so a
    // preset load (all four at once) cannot pile four folds into one callback.
    std::array<bool, kNumCorners> cornerDirty_{{false, false, false, false}};

    CornerFifo cornerFifo_;

    std::vector<float> target_;    // E; only prepare() needs the embedding itself
    // Scratch for rebuildBlendTargetSlerp()'s nested SLERP: bottom row
    // (corners 0,1) and top row (corners 2,3) each collapse to one E-vector
    // before the final SLERP between them. E-sized, preallocated by setModel().
    std::vector<float> slerpBottomScratch_;
    std::vector<float> slerpTopScratch_;

    // Weight-space state, all namWeightCount() long and preallocated by setModel().
    std::array<std::vector<float>, kNumCorners> foldedCorners_;
    std::vector<float> blendTarget_;    // where the smoother is heading
    std::vector<float> currentWeights_; // what the live WaveNet currently holds
    float lastBlendW_[kNumCorners] = {0.0f, 0.0f, 0.0f, 0.0f};
    bool blendValid_ = false; // lastBlendW_/blendTarget_ reflect the current corners
    bool settled_ = true;     // currentWeights_ == blendTarget_, nothing to push
    bool useSlerp_ = false;   // see setUseSlerp()/slerpActive()
    // slerpActive() at the last rebuild. A toggle flip alone doesn't change
    // (x_,y_) or the corners, so lastBlendW_ can't detect it; this catches it.
    bool lastUsedSlerp_ = false;

    float x_ = 0.5f;
    float y_ = 0.5f;
    float smoothingMs_ = 30.0f;
    double sampleRate_ = 48000.0;
    int maxBlockSize_ = 0;
    float smoothCoeffPerBlock_ = 1.0f;
    bool prepared_ = false;
};

} // namespace aaom
