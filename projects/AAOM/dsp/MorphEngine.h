#pragma once

// MorphEngine — the real-time morph layer on top of MorphModel (JUCE-free).
//
// Holds up to 4 "corner" embeddings arranged on a 2D XY pad. A dot at (x,y) in
// [0,1]^2 bilinearly blends the assigned corners into a target embedding; the
// current embedding one-pole smooths toward the target so drags stay click-free
// (gamma/beta are continuous in the embedding). Whenever the smoothed embedding
// moves, the folded weights are re-derived and hot-swapped on the live NAM
// WaveNet. All per-block work is preallocated and allocation-free.
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

    // Process num_frames mono samples. Re-folds/hot-swaps weights first if the
    // smoothed embedding has moved. in may equal out.
    void processBlock(NAM_SAMPLE* in, NAM_SAMPLE* out, int numFrames);

    // Reset DSP state (e.g. transport reset). Off the audio thread ideally.
    void reset();

private:
    void computeTarget();          // fills target_ from corners + (x_,y_)
    void snapCurrentToTarget();     // current_ = target_

    std::unique_ptr<MorphModel> model_;

    std::array<std::vector<float>, kNumCorners> corners_;
    std::array<bool, kNumCorners> assigned_{{false, false, false, false}};

    CornerFifo cornerFifo_;

    std::vector<float> target_;    // E
    std::vector<float> current_;   // E (smoothed, what the weights reflect)
    std::vector<float> lastFolded_;// E (embedding the live weights were folded from)

    float x_ = 0.5f;
    float y_ = 0.5f;
    float smoothingMs_ = 30.0f;
    double sampleRate_ = 48000.0;
    int maxBlockSize_ = 0;
    float smoothCoeffPerBlock_ = 1.0f;
    bool currentValid_ = false;
    bool prepared_ = false;
};

} // namespace aaom
