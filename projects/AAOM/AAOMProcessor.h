#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include <BaseProcessor.h>
#include <ParametricEQ.h>

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

    // --- model selection ----------------------------------------------------
    // Swapping models is a message-thread operation: the bundle is parsed first
    // (so a bad file never disturbs the live model), then audio is suspended for
    // the engine rebuild. Corners are always reseeded from the new bundle's own
    // profiles — embeddings are dim- and run-locked, so they cannot carry over.

    // Load a bundle from disk. On failure returns false, fills `message`, and
    // leaves the currently loaded model completely untouched.
    bool loadModelFromFile(const juce::File& file, juce::String& message);

    // --- built-in models ----------------------------------------------------
    // Every model package under assets/<run>/ is compiled into the binary. Index
    // 0 is the default loaded on a fresh instance.
    static int numBuiltInModels();
    static juce::String builtInModelName(int index);
    void loadBuiltInModel(int index = 0);
    // Index of the live built-in, or -1 when the model came from disk.
    int builtInModelIndex() const { return usingBuiltInModel() ? builtInIndex_ : -1; }

    bool usingBuiltInModel() const { return modelFile_ == juce::File{}; }
    juce::File modelFile() const { return modelFile_; }
    juce::String modelDisplayName() const;
    // Non-fatal note about the last load (e.g. a session's model was missing).
    const juce::String& modelWarning() const { return modelWarning_; }
    int modelGeneration() const { return modelGen_.load(); } // bumps on any model change

    // Recently loaded bundles, most recent first (persisted across instances).
    static constexpr int kMaxRecentModels = 8;
    juce::StringArray recentModelFiles() const;
    void clearRecentModelFiles();

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

    // Parse `jsonText` and, only if it yields a valid model, commit it as the
    // live one (suspending audio for the engine rebuild). `source` is the file
    // it came from, or an empty File for a compiled-in bundle — in which case
    // `builtInIndex` says which one.
    bool applyBundle(const juce::String& jsonText, const juce::File& source, int builtInIndex,
                     juce::String& message);
    // Seed the engine from corners_ and rebuild its DSP at the current rate.
    void rebuildEngine();

    // Raw catalogue JSON belonging to a model package (compiled-in or on disk).
    juce::String catalogueTextFor(const juce::File& source, int builtInIndex) const;

    // Corner set to use after swapping to `model`. Assigned corners are carried
    // over by profile name (the runs share a device set, so the same amp exists
    // in both catalogues with a model-appropriate embedding); anything that was
    // never assigned falls back to the new bundle's own profiles. `numLost`
    // receives the count of assigned corners with no match in `newCatalog`.
    std::array<CornerInfo, kNumCorners> carryCornersTo(const MorphModel& model,
                                                       const std::vector<CornerInfo>& newCatalog,
                                                       int& numLost) const;

    juce::PropertiesFile* settings();
    void noteRecentModel(const juce::File& file);

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    MorphEngine engine_;
    juce::String bundleError_;

    // Which bundle is live. An empty File means a compiled-in one, identified
    // by builtInIndex_ into the built-in table.
    juce::File modelFile_;
    int builtInIndex_ = 0;
    juce::String modelWarning_;
    std::atomic<int> modelGen_{0};

    // Cached from prepare() so a runtime model swap can rebuild the engine at
    // the right rate/block size without waiting for the host to call prepare().
    double currentSampleRate_ = 0.0;
    int currentBlockSize_ = 0;

    std::unique_ptr<juce::ApplicationProperties> appProps_;

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

    // Post-model tone stack (Bass/Mid/Treble/Presence), applied to the mono
    // signal at host sample rate before the output gain stage.
    enum EqBand : unsigned int
    {
        EqBass = 0,
        EqMid,
        EqTreble,
        EqPresence,
        EqNumBands
    };
    DSP::ParametricEqualizer eq_{EqNumBands, 1};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AAOMProcessor)
};

} // namespace aaom
