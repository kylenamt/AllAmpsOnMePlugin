#pragma once

// MorphModel — the AllAmpsOnMe embedding-morph engine, JUCE-free.
//
// Responsibilities:
//   * Parse an "aaom bundle" (JSON, see .claude/PLAN.md "File formats") into the
//     fixed A2 architecture description, the per-layer weight tensors, and the
//     list of embedding profiles.
//   * Given an embedding vector `e`, fold the per-layer FiLM (gamma/beta = film(e))
//     into the plain A2 conv/mixin weights, producing a weight stream in exactly
//     the order NeuralAmpModelerCore's WaveNet::set_weights_ consumes.
//   * Instantiate that as a stock NAM `WaveNet` (generic path) and process audio.
//   * For morphing, re-fold a new embedding and hot-swap the weights on the live
//     WaveNet via set_weights_ (no reallocation, no reconstruction).
//
// The fold identity (per layer, per output channel c):
//     z = conv(x) + mixin(clean);  z = gamma*z + beta;  z = leaky_relu(z)
//   =>  conv.weight[c]  *= gamma[c]
//       conv.bias[c]     = gamma[c]*conv.bias[c] + beta[c]
//       mixin.weight[c] *= gamma[c]
// which is bit-for-bit equivalent to the unfolded FiLM forward for a fixed e.

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

// Only NAM's public DSP interface is needed in the header. NAM_SAMPLE is defined
// there (float when NAM_SAMPLE_FLOAT is set for the build, as the plugin does).
#include "NAM/dsp.h"

namespace nam { namespace wavenet { class WaveNet; } }

namespace aaom
{

struct Profile
{
    std::string name;
    std::vector<float> embedding; // length == MorphModel::embeddingDim()
};

class MorphModel
{
public:
    MorphModel() = default;
    ~MorphModel();

    MorphModel(const MorphModel&) = delete;
    MorphModel& operator=(const MorphModel&) = delete;

    // Parse a bundle from its JSON text. Throws std::runtime_error on malformed
    // input or a shape/count mismatch against the declared architecture.
    static std::unique_ptr<MorphModel> fromBundleJson(const std::string& jsonText);

    // --- Architecture / metadata -------------------------------------------
    int channels() const { return channels_; }
    int embeddingDim() const { return embeddingDim_; }
    int numLayers() const { return static_cast<int>(layers_.size()); }
    int headKernel() const { return headKernel_; }
    double bundleSampleRate() const { return sampleRate_; }
    const std::string& runSha8() const { return runSha8_; }
    const std::string& runName() const { return runName_; }
    const std::vector<Profile>& profiles() const { return profiles_; }

    // Number of floats a folded weight stream contains (what NAM will consume).
    std::size_t namWeightCount() const;

    // --- Folding ------------------------------------------------------------
    // Fold `e` (length embeddingDim()) into a NAM-order weight stream in `out`.
    // `out` is resized as needed; safe to reuse across calls (no allocation once
    // it has reached namWeightCount()).
    void foldEmbedding(const std::vector<float>& e, std::vector<float>& out) const;

    // --- DSP lifecycle ------------------------------------------------------
    // Build the stock NAM WaveNet for embedding `e` (generic path, so the live
    // instance can be re-weighted for morphing). Call once before processing.
    void buildDsp(const std::vector<float>& e, double sampleRate, int maxBufferSize);

    // Reset internal DSP state for a new sample rate / block size.
    void reset(double sampleRate, int maxBufferSize);

    // Re-fold `e` and hot-swap the weights on the live WaveNet. Real-time safe
    // once buildDsp() has run at the current architecture. No-op if no DSP.
    void setEmbedding(const std::vector<float>& e);

    // Process `numFrames` mono samples in place-safe fashion (in may == out).
    void process(NAM_SAMPLE* in, NAM_SAMPLE* out, int numFrames);

    bool hasDsp() const { return dsp_ != nullptr; }

private:
    struct Layer
    {
        std::vector<float> convW;  // [C, C, k]  (out, in, kernel), C-order
        std::vector<float> convB;  // [C]
        std::vector<float> mixinW; // [C, 1, 1]
        std::vector<float> filmW;  // [2C, E]    (gamma rows 0..C-1, beta rows C..2C-1)
        std::vector<float> filmB;  // [2C]
        std::vector<float> x1W;    // [C, C, 1]
        std::vector<float> x1B;    // [C]
        int kernel = 0;
    };

    // Build the nlohmann config+weights document (as text) for get_dsp.
    std::string buildNamJson(const std::vector<float>& namWeights) const;

    int channels_ = 0;
    int embeddingDim_ = 0;
    int headKernel_ = 0;
    float headScale_ = 0.0f;
    double sampleRate_ = 48000.0;
    std::string runSha8_;
    std::string runName_;

    std::vector<int> kernelSizes_;
    std::vector<int> dilations_;

    std::vector<float> rechannelW_; // [C, 1, 1]
    std::vector<Layer> layers_;
    std::vector<float> headW_;      // [1, C, headKernel]
    std::vector<float> headB_;      // [1]

    std::vector<Profile> profiles_;

    std::unique_ptr<nam::DSP> dsp_;
    nam::wavenet::WaveNet* wavenet_ = nullptr; // non-owning view of dsp_
    std::vector<float> scratchWeights_;        // reused by setEmbedding (RT path)

    // Per-fold gamma/beta scratch, sized to `channels_` on first use. mutable so
    // foldEmbedding() stays const while remaining allocation-free after warm-up.
    mutable std::vector<float> gammaScratch_;
    mutable std::vector<float> betaScratch_;
};

} // namespace aaom
