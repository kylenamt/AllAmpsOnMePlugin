#pragma once

#include <array>
#include <atomic>
#include <vector>

#include <BaseProcessor.h>

#include "dsp/MorphEngine.h"
#include "dsp/Resampler.h"

namespace aaom
{

// Parameter IDs (shared with the editor).
namespace pid
{
static const juce::String inputGain{"InputGain"};
static const juce::String outputGain{"OutputGain"};
static const juce::String morphX{"MorphX"};
static const juce::String morphY{"MorphY"};
static const juce::String eqBass{"EqBass"};
static const juce::String eqMid{"EqMid"};
static const juce::String eqTreble{"EqTreble"};
static const juce::String eqPresence{"EqPresence"};
} // namespace pid

// One XY-pad corner as seen by the message thread / persistence. The audio
// thread keeps its own mirror inside MorphEngine (embeddings only).
struct CornerInfo
{
    bool assigned = false;
    juce::String name;
    juce::String runSha8;
    std::vector<float> embedding;
};

// Result of trying to paste a profile into a corner.
enum class PasteResult
{
    Ok,                 // loaded
    OkRunMismatch,      // loaded, but run sha8 differed (caller chose to override)
    RejectedParse,      // not valid aaom_profile JSON
    RejectedDim,        // embedding_dim mismatch (hard reject)
    RejectedRunMismatch // run sha8 differs and override not granted
};

class AAOMProcessor : public mrta::BaseProcessor
{
public:
    AAOMProcessor();
    ~AAOMProcessor() override;

    void prepare(double sampleRate, int maxBufferSize) override;
    void process(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;

    // --- model / bundle ----------------------------------------------------
    bool modelLoaded() const { return engine_.ready(); }
    const juce::String& bundleError() const { return bundleError_; }
    MorphEngine& engine() { return engine_; }
    int embeddingDim() const { return engine_.ready() ? engine_.model()->embeddingDim() : 0; }
    juce::String bundleRunSha8() const;

    // --- corners (message thread) ------------------------------------------
    static constexpr int kNumCorners = MorphEngine::kNumCorners;
    const CornerInfo& corner(int i) const { return corners_[static_cast<std::size_t>(i)]; }
    int cornerGeneration() const { return cornerGen_.load(); } // bumps on any corner change

    // Try to load a pasted profile JSON into a corner. `message` gets a
    // human-readable explanation for the UI.
    PasteResult tryPasteProfile(int corner, const juce::String& jsonText, bool allowRunMismatch, juce::String& message);
    void clearCorner(int corner);

    // --- default profile catalogue (bundled profiles.json) -----------------
    // Extra factory embeddings (same run as the bundle) the user can pick from
    // per corner, in addition to pasting an aaom_profile from the clipboard.
    int numCatalogProfiles() const { return static_cast<int>(catalog_.size()); }
    juce::String catalogProfileName(int i) const;
    // Load catalogue profile `profileIndex` into `corner`. Returns false on a
    // bad index (the bundled set already matches the model dim/run).
    bool loadCatalogProfile(int corner, int profileIndex);

private:
    void loadBundle();
    void loadCatalog();
    void seedCornersFromBundle();
    void pushCornerToEngine(int i);

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    MorphEngine engine_;
    juce::String bundleError_;

    std::array<CornerInfo, kNumCorners> corners_;
    std::atomic<int> cornerGen_{0};

    // Bundled default profiles (from profiles.json), pre-validated to the model.
    std::vector<CornerInfo> catalog_;

    // Audio-thread parameter mirrors (written by MRTA callbacks in process()).
    std::atomic<float> morphX_{0.5f};
    std::atomic<float> morphY_{0.5f};

    juce::LinearSmoothedValue<float> inputGain_;
    juce::LinearSmoothedValue<float> outputGain_;

    Resampler48k resampler_;   // host SR <-> 48 kHz around the model
    std::vector<float> mono_;  // preallocated mono work buffer (NAM is 1->1)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AAOMProcessor)
};

} // namespace aaom
