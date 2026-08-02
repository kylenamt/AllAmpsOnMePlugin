#include "AAOMProcessor.h"
#include "AAOMEditor.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <iterator>
#include <map>
#include <utility>

#include <BinaryData.h>

namespace aaom
{

namespace
{
// Detent size of the EQ faders, in dB. The parameter's NormalisableRange
// interval is what actually quantises the value, and the slider attachment
// carries it through to the UI, so this alone makes the faders stepped.
constexpr float kEqStepDb = 1.0f;

// Compiled-in model packages, one per assets/<run>/ folder. Each bundle is
// run-locked to the catalogue beside it, so the two always travel together.
// Index 0 is the default a fresh instance loads. `id` is written into session
// state, so it must stay stable even if the display name or order changes.
struct BuiltInModel
{
    const char* id;
    const char* displayName;
    const char* bundleData;
    int bundleSize;
    const char* profilesData;
    int profilesSize;
};

const BuiltInModel kBuiltInModels[] = {
    // Newer A2 run: 256-dim embeddings, lower val_esr than wavenet_a2.
    {"nam_a2_256", "NAM A2 256", BinaryData::nam_a2_256_bundle_json, BinaryData::nam_a2_256_bundle_jsonSize,
     BinaryData::nam_a2_256_profiles_json, BinaryData::nam_a2_256_profiles_jsonSize},
    // Original 128-dim run, kept so existing sessions can still resolve it.
    {"wavenet_a2", "WaveNet A2 128", BinaryData::bundle_json, BinaryData::bundle_jsonSize,
     BinaryData::profiles_json, BinaryData::profiles_jsonSize},
};

constexpr int kNumBuiltInModels = static_cast<int>(std::size(kBuiltInModels));

int builtInIndexForId(const juce::String& id)
{
    for (int i = 0; i < kNumBuiltInModels; ++i)
        if (id == kBuiltInModels[i].id)
            return i;
    return -1;
}

// Locate the catalogue that belongs to a bundle on disk. Exported packages name
// the pair after the run (nam_a2_256_bundle.json / nam_a2_256_profiles.json),
// but a plain profiles.json beside the bundle is also accepted.
juce::File siblingCatalogueFor(const juce::File& bundle)
{
    const juce::File dir = bundle.getParentDirectory();
    const juce::String stem = bundle.getFileNameWithoutExtension();

    juce::StringArray candidates;
    if (stem.contains("bundle"))
        candidates.add(stem.replace("bundle", "profiles") + ".json");
    candidates.add(stem + "_profiles.json");
    candidates.add("profiles.json");

    for (const auto& name : candidates)
    {
        const juce::File f = dir.getChildFile(name);
        if (f.existsAsFile())
            return f;
    }
    return {};
}

// Parse a profile catalogue, keeping only entries that fit the given model.
// Pure so it can run against a model that is parsed but not yet live, which is
// what lets a swap prepare everything before suspending audio.
std::vector<CornerInfo> parseCatalogue(const juce::String& text, int modelDim, const juce::String& modelRun)
{
    std::vector<CornerInfo> out;
    if (text.isEmpty() || modelDim <= 0)
        return out;

    const juce::var root = juce::JSON::parse(text);
    if (!root.isArray())
        return out;

    for (const juce::var& entry : *root.getArray())
    {
        if (!entry.isObject() || !entry.hasProperty("aaom_profile"))
            continue;

        const juce::var embVar = entry.getProperty("embedding", juce::var());
        if (!embVar.isArray() || embVar.getArray()->size() != modelDim)
            continue; // skip anything that doesn't fit this model

        CornerInfo info;
        info.assigned = true;
        info.name = entry.getProperty("name", juce::var("profile")).toString();
        info.runSha8 = entry.getProperty("run", juce::var()).toString();
        info.embedding.resize(static_cast<std::size_t>(modelDim));
        for (int i = 0; i < modelDim; ++i)
            info.embedding[static_cast<std::size_t>(i)] = static_cast<float>(double(embVar[i]));

        // Catalogue should match the model's run; skip strays defensively.
        if (info.runSha8.isNotEmpty() && modelRun.isNotEmpty() && info.runSha8 != modelRun)
            continue;

        out.push_back(std::move(info));
    }

    // Sort by name so the Load menu can present alphabetical groups (amp variants
    // that share a name prefix end up adjacent). Indices stay self-consistent:
    // the menu and loadCatalogProfile() both address this same sorted vector.
    std::sort(out.begin(), out.end(),
              [](const CornerInfo& a, const CornerInfo& b) { return a.name.compareIgnoreCase(b.name) < 0; });
    return out;
}
} // namespace

int AAOMProcessor::numBuiltInModels()
{
    return kNumBuiltInModels;
}

juce::String AAOMProcessor::builtInModelName(int index)
{
    if (index < 0 || index >= kNumBuiltInModels)
        return {};
    return kBuiltInModels[index].displayName;
}

static std::vector<mrta::ParameterInfo> makeParameters()
{
    return {
        {pid::inputGain, "Input", "dB", 0.0f, -24.0f, 24.0f, 0.1f, 1.0f},
        {pid::outputGain, "Output", "dB", 0.0f, -24.0f, 24.0f, 0.1f, 1.0f},
        {pid::morphX, "Morph X", "", 0.5f, 0.0f, 1.0f, 0.001f, 1.0f},
        {pid::morphY, "Morph Y", "", 0.5f, 0.0f, 1.0f, 0.001f, 1.0f},
        // EQ: post-model tone stack, see ParametricEqualizer eq_ in AAOMProcessor.
        // Detented in 1 dB steps (25 positions over the +/-12 dB range) so the
        // faders click into place like a hardware tone stack rather than sweeping
        // continuously; the look-and-feel draws matching tick marks.
        {pid::eqBass, "Bass", "dB", 0.0f, -12.0f, 12.0f, kEqStepDb, 1.0f},
        {pid::eqMid, "Mid", "dB", 0.0f, -12.0f, 12.0f, kEqStepDb, 1.0f},
        {pid::eqTreble, "Treble", "dB", 0.0f, -12.0f, 12.0f, kEqStepDb, 1.0f},
        {pid::eqPresence, "Presence", "dB", 0.0f, -12.0f, 12.0f, kEqStepDb, 1.0f},
    };
}

AAOMProcessor::AAOMProcessor()
: mrta::BaseProcessor(makeParameters())
{
    registerParameterCallback(pid::inputGain, [this](float v, bool force) {
        const float g = juce::Decibels::decibelsToGain(v);
        if (force)
            inputGain_.setCurrentAndTargetValue(g);
        else
            inputGain_.setTargetValue(g);
    });

    registerParameterCallback(pid::outputGain, [this](float v, bool force) {
        const float g = juce::Decibels::decibelsToGain(v);
        if (force)
            outputGain_.setCurrentAndTargetValue(g);
        else
            outputGain_.setTargetValue(g);
    });

    registerParameterCallback(pid::morphX, [this](float v, bool) { morphX_.store(v); });
    registerParameterCallback(pid::morphY, [this](float v, bool) { morphY_.store(v); });

    // Fixed tone-stack shape (classic amp-style bands); only gain is
    // automatable per band, matching the EqBass/Mid/Treble/Presence knobs.
    eq_.setBandType(EqBass, DSP::ParametricEqualizer::LowShelf);
    eq_.setBandFrequency(EqBass, 100.0f);
    eq_.setBandType(EqMid, DSP::ParametricEqualizer::Peak);
    eq_.setBandFrequency(EqMid, 800.0f);
    eq_.setBandResonance(EqMid, 0.7f);
    eq_.setBandType(EqTreble, DSP::ParametricEqualizer::HighShelf);
    eq_.setBandFrequency(EqTreble, 3000.0f);
    eq_.setBandType(EqPresence, DSP::ParametricEqualizer::HighShelf);
    eq_.setBandFrequency(EqPresence, 6500.0f);

    registerParameterCallback(pid::eqBass, [this](float v, bool) { eq_.setBandGain(EqBass, v); });
    registerParameterCallback(pid::eqMid, [this](float v, bool) { eq_.setBandGain(EqMid, v); });
    registerParameterCallback(pid::eqTreble, [this](float v, bool) { eq_.setBandGain(EqTreble, v); });
    registerParameterCallback(pid::eqPresence, [this](float v, bool) { eq_.setBandGain(EqPresence, v); });

    // Loads the model and, on success, seeds the catalogue and corners from it.
    loadBundle();
}

AAOMProcessor::~AAOMProcessor() = default;

juce::String AAOMProcessor::bundleRunSha8() const
{
    return engine_.ready() ? juce::String(engine_.model()->runSha8()) : juce::String{};
}

void AAOMProcessor::loadBundle()
{
#ifdef AAOM_DEV_BUNDLE_PATH
    // Dev iteration path: load a bundle from disk without rebuilding the binary.
    const juce::File devFile{AAOM_DEV_BUNDLE_PATH};
    if (devFile.existsAsFile())
    {
        juce::String devMessage;
        if (loadModelFromFile(devFile, devMessage))
            return;
        bundleError_ = devMessage;
    }
#endif

    loadBuiltInModel(0);
}

void AAOMProcessor::loadBuiltInModel(int index)
{
    if (index < 0 || index >= kNumBuiltInModels)
        index = 0;

    const BuiltInModel& m = kBuiltInModels[index];
    const juce::String text = juce::String::fromUTF8(m.bundleData, m.bundleSize);

    juce::String message;
    if (!applyBundle(text, juce::File{}, index, message)) // applyBundle owns modelWarning_
        bundleError_ = message;
}

bool AAOMProcessor::loadModelFromFile(const juce::File& file, juce::String& message)
{
    if (!file.existsAsFile())
    {
        message = "File not found: " + file.getFullPathName();
        return false;
    }

    const juce::String text = file.loadFileAsString();
    if (text.trim().isEmpty())
    {
        message = "File is empty: " + file.getFullPathName();
        return false;
    }

    if (!applyBundle(text, file, -1, message))
        return false;

    noteRecentModel(file); // applyBundle owns modelWarning_
    return true;
}

bool AAOMProcessor::applyBundle(const juce::String& jsonText, const juce::File& source, int builtInIndex,
                                juce::String& message)
{
    // Parse before committing anything. A malformed or mismatched bundle must
    // leave the live model completely untouched, so nothing below is reached —
    // and audio is never suspended — unless the parse fully succeeds.
    std::unique_ptr<MorphModel> parsed;
    try
    {
        parsed = MorphModel::fromBundleJson(jsonText.toStdString());
    }
    catch (const std::exception& e)
    {
        message = juce::String("Bundle load failed: ") + e.what();
        return false;
    }

    if (parsed == nullptr)
    {
        message = "Bundle load failed: parser returned no model.";
        return false;
    }

    // Everything that can be computed against the parsed-but-not-yet-live model
    // is done here, outside the suspended window: parsing the catalogue is the
    // most expensive step of a swap, and resolving the corners needs it. This
    // keeps the audio gap down to the engine rebuild alone.
    const int resolvedIndex = (source == juce::File{} && builtInIndex >= 0) ? builtInIndex : 0;
    std::vector<CornerInfo> newCatalog = parseCatalogue(catalogueTextFor(source, resolvedIndex),
                                                        parsed->embeddingDim(),
                                                        juce::String(parsed->runSha8()));

    // Corner embeddings are dim- and run-locked to the model that produced them,
    // so the raw vectors cannot carry across a swap — but the *tones* can: each
    // assigned corner is re-resolved by profile name against the incoming
    // catalogue. The morph X/Y position is a plain parameter and stays put.
    int lostCorners = 0;
    std::array<CornerInfo, kNumCorners> newCorners = carryCornersTo(*parsed, newCatalog, lostCorners);

    // Commit. Rebuilding the engine allocates every weight tensor and constructs
    // a fresh WaveNet, which is far too slow for the audio thread, so processing
    // is suspended across the swap (the host gets silence rather than a blocked
    // thread). The callback lock additionally guarantees no block is mid-flight
    // when the old model is freed.
    suspendProcessing(true);
    {
        const juce::ScopedLock sl(getCallbackLock());
        engine_.setModel(std::move(parsed));
        modelFile_ = source;
        // Only meaningful when source is empty; keeps a stale index from making
        // a disk-loaded model look like a built-in.
        builtInIndex_ = resolvedIndex;
        bundleError_.clear();
        catalog_ = std::move(newCatalog);
        corners_ = std::move(newCorners);
        cornerGen_.fetch_add(1);
        rebuildEngine();
    }
    suspendProcessing(false);

    modelWarning_ = lostCorners > 0
                        ? juce::String(lostCorners) + " corner profile(s) have no equivalent in this model."
                        : juce::String{};

    modelGen_.fetch_add(1);
    message = "Loaded.";
    return true;
}

void AAOMProcessor::rebuildEngine()
{
    if (!engine_.ready() || currentSampleRate_ <= 0.0 || currentBlockSize_ <= 0)
        return; // not prepared yet — prepare() will build the engine instead

    // Audio thread is not running here (constructor, or suspended swap), so the
    // corners can be pushed directly rather than through the FIFO.
    for (int i = 0; i < kNumCorners; ++i)
    {
        const auto& c = corners_[static_cast<std::size_t>(i)];
        if (c.assigned)
            engine_.setCorner(i, c.embedding);
        else
            engine_.clearCorner(i);
    }

    const double innerRate = resampler_.active() ? Resampler48k::kModelRate : currentSampleRate_;
    const int innerBlock = resampler_.active() ? resampler_.maxInnerBlock() : currentBlockSize_;
    engine_.prepare(innerRate, innerBlock);
}

juce::String AAOMProcessor::modelDisplayName() const
{
    return usingBuiltInModel() ? builtInModelName(builtInIndex_) : modelFile_.getFileNameWithoutExtension();
}

juce::PropertiesFile* AAOMProcessor::settings()
{
    if (appProps_ == nullptr)
    {
        juce::PropertiesFile::Options opts;
        opts.applicationName = "AllAmpsOnMe";
        opts.filenameSuffix = "settings";
        opts.folderName = "AllAmpsOnMe";
        opts.osxLibrarySubFolder = "Application Support";

        appProps_ = std::make_unique<juce::ApplicationProperties>();
        appProps_->setStorageParameters(opts);
    }
    return appProps_->getUserSettings();
}

juce::StringArray AAOMProcessor::recentModelFiles() const
{
    // The properties file is lazily created; reading recents is logically const.
    auto* props = const_cast<AAOMProcessor*>(this)->settings();
    if (props == nullptr)
        return {};

    juce::StringArray recents;
    recents.addLines(props->getValue("recentModels"));
    recents.removeEmptyStrings();
    return recents;
}

void AAOMProcessor::noteRecentModel(const juce::File& file)
{
    auto* props = settings();
    if (props == nullptr)
        return;

    juce::StringArray recents;
    recents.addLines(props->getValue("recentModels"));
    recents.removeEmptyStrings();

    // Most recent first, no duplicates, bounded length.
    recents.removeString(file.getFullPathName());
    recents.insert(0, file.getFullPathName());
    while (recents.size() > kMaxRecentModels)
        recents.remove(recents.size() - 1);

    props->setValue("recentModels", recents.joinIntoString("\n"));
    props->saveIfNeeded();
}

void AAOMProcessor::clearRecentModelFiles()
{
    if (auto* props = settings())
    {
        props->removeValue("recentModels");
        props->saveIfNeeded();
    }
}

juce::String AAOMProcessor::catalogueTextFor(const juce::File& source, int builtInIndex) const
{
    // A built-in model uses the catalogue compiled in beside it; a bundle loaded
    // from disk uses the catalogue shipped in its own folder.
    if (source == juce::File{})
    {
        const int i = (builtInIndex >= 0 && builtInIndex < kNumBuiltInModels) ? builtInIndex : 0;
        return juce::String::fromUTF8(kBuiltInModels[i].profilesData, kBuiltInModels[i].profilesSize);
    }

    const juce::File sibling = siblingCatalogueFor(source);
    return sibling != juce::File{} ? sibling.loadFileAsString() : juce::String{};
}

void AAOMProcessor::loadCatalog()
{
    catalog_.clear();
    if (!engine_.ready())
        return;

    catalog_ = parseCatalogue(catalogueTextFor(modelFile_, builtInIndex_), engine_.model()->embeddingDim(),
                              juce::String(engine_.model()->runSha8()));
}

std::array<CornerInfo, AAOMProcessor::kNumCorners>
AAOMProcessor::carryCornersTo(const MorphModel& model, const std::vector<CornerInfo>& newCatalog,
                              int& numLost) const
{
    std::array<CornerInfo, kNumCorners> result;
    numLost = 0;

    std::map<juce::String, const CornerInfo*> byName;
    for (const CornerInfo& c : newCatalog)
        byName.emplace(c.name, &c); // first wins on duplicate names

    for (int i = 0; i < kNumCorners; ++i)
    {
        const CornerInfo& old = corners_[static_cast<std::size_t>(i)];
        if (!old.assigned || old.name.isEmpty())
            continue;

        // Same amp, new model: take the embedding from the incoming catalogue
        // rather than making the user pick the same four tones over again.
        const auto it = byName.find(old.name);
        if (it != byName.end())
            result[static_cast<std::size_t>(i)] = *it->second;
        else
            ++numLost; // no equivalent in this model; left empty rather than guessed
    }

    // Corners that were never assigned fall back to the new bundle's own
    // profiles — what a fresh instance starts from.
    const auto& profiles = model.profiles();
    const juce::String run = juce::String(model.runSha8());
    for (int i = 0; i < kNumCorners; ++i)
    {
        const auto idx = static_cast<std::size_t>(i);
        if (result[idx].assigned || corners_[idx].assigned)
            continue;
        if (i < static_cast<int>(profiles.size()))
            result[idx] = CornerInfo{true, juce::String(profiles[static_cast<std::size_t>(i)].name), run,
                                     profiles[static_cast<std::size_t>(i)].embedding};
    }

    return result;
}

juce::String AAOMProcessor::catalogProfileName(int i) const
{
    if (i < 0 || i >= static_cast<int>(catalog_.size()))
        return {};
    return catalog_[static_cast<std::size_t>(i)].name;
}

bool AAOMProcessor::loadCatalogProfile(int corner, int profileIndex)
{
    if (corner < 0 || corner >= kNumCorners)
        return false;
    if (profileIndex < 0 || profileIndex >= static_cast<int>(catalog_.size()))
        return false;

    corners_[static_cast<std::size_t>(corner)] = catalog_[static_cast<std::size_t>(profileIndex)];
    cornerGen_.fetch_add(1);
    pushCornerToEngine(corner);
    return true;
}

void AAOMProcessor::seedCornersFromBundle()
{
    for (auto& c : corners_)
        c = CornerInfo{};

    if (!engine_.ready())
        return;

    const auto* model = engine_.model();
    const auto& profiles = model->profiles();
    const juce::String sha = juce::String(model->runSha8());
    for (int i = 0; i < kNumCorners && i < static_cast<int>(profiles.size()); ++i)
    {
        corners_[static_cast<std::size_t>(i)] = CornerInfo{true, juce::String(profiles[i].name), sha,
                                                           profiles[i].embedding};
    }
    cornerGen_.fetch_add(1);
}

void AAOMProcessor::pushCornerToEngine(int i)
{
    if (i < 0 || i >= kNumCorners)
        return;
    const auto& c = corners_[static_cast<std::size_t>(i)];
    if (c.assigned)
        engine_.queueCorner(i, c.embedding);
    else
        engine_.queueClear(i);
}

PasteResult AAOMProcessor::tryPasteProfile(int corner, const juce::String& jsonText, bool allowRunMismatch,
                                           juce::String& message)
{
    if (corner < 0 || corner >= kNumCorners)
    {
        message = "Invalid corner.";
        return PasteResult::RejectedParse;
    }
    if (!engine_.ready())
    {
        message = "No model loaded.";
        return PasteResult::RejectedParse;
    }

    juce::var v = juce::JSON::parse(jsonText);
    if (!v.isObject() || !v.hasProperty("aaom_profile"))
    {
        message = "Not a valid aaom_profile JSON.";
        return PasteResult::RejectedParse;
    }

    const int modelDim = engine_.model()->embeddingDim();
    const int dim = static_cast<int>(v.getProperty("dim", -1));
    const juce::var embVar = v.getProperty("embedding", juce::var());
    if (!embVar.isArray())
    {
        message = "Profile has no 'embedding' array.";
        return PasteResult::RejectedParse;
    }
    const int embLen = embVar.getArray()->size();
    if (dim != modelDim || embLen != modelDim)
    {
        message = "Embedding dimension mismatch: profile dim=" + juce::String(dim) + ", embedding="
                  + juce::String(embLen) + ", model expects " + juce::String(modelDim) + ". Rejected.";
        return PasteResult::RejectedDim;
    }

    const juce::String profileRun = v.getProperty("run", juce::var()).toString();
    const juce::String modelRun = juce::String(engine_.model()->runSha8());
    const bool runMismatch = profileRun.isNotEmpty() && modelRun.isNotEmpty() && profileRun != modelRun;
    if (runMismatch && !allowRunMismatch)
    {
        message = "Profile is from run '" + profileRun + "' but this model is run '" + modelRun
                  + "'. Load anyway?";
        return PasteResult::RejectedRunMismatch;
    }

    std::vector<float> emb(static_cast<std::size_t>(modelDim));
    for (int i = 0; i < modelDim; ++i)
        emb[static_cast<std::size_t>(i)] = static_cast<float>(double(embVar[i]));

    CornerInfo info;
    info.assigned = true;
    info.name = v.getProperty("name", juce::var("profile")).toString();
    info.runSha8 = profileRun;
    info.embedding = std::move(emb);
    corners_[static_cast<std::size_t>(corner)] = std::move(info);
    cornerGen_.fetch_add(1);
    pushCornerToEngine(corner);

    message = runMismatch ? "Loaded (run mismatch overridden)." : "Loaded.";
    return runMismatch ? PasteResult::OkRunMismatch : PasteResult::Ok;
}

void AAOMProcessor::clearCorner(int corner)
{
    if (corner < 0 || corner >= kNumCorners)
        return;
    corners_[static_cast<std::size_t>(corner)] = CornerInfo{};
    cornerGen_.fetch_add(1);
    pushCornerToEngine(corner);
}

void AAOMProcessor::prepare(double sampleRate, int maxBufferSize)
{
    mono_.assign(static_cast<std::size_t>(juce::jmax(1, maxBufferSize)), 0.0f);

    inputGain_.reset(sampleRate, 0.02);
    outputGain_.reset(sampleRate, 0.02);

    // Tone stack runs on the mono signal at host rate, post-resampling.
    eq_.prepare(sampleRate, 1);

    // Set up host <-> 48 kHz resampling; the model always runs at 48 kHz.
    resampler_.prepare(sampleRate, maxBufferSize);
    setLatencySamples(resampler_.latencySamples());

    // Cached so a runtime model swap can rebuild the engine at this same
    // rate/block without waiting for the host to call prepare() again.
    currentSampleRate_ = sampleRate;
    currentBlockSize_ = maxBufferSize;

    // Seed the engine's corners directly (audio thread not running yet), then
    // build the DSP at the current morph position.
    rebuildEngine();
}

void AAOMProcessor::process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int numCh = buffer.getNumChannels();
    const int n = buffer.getNumSamples();
    if (n <= 0 || numCh <= 0)
        return;

    engine_.setMorph(morphX_.load(), morphY_.load());

    if (!engine_.ready())
    {
        buffer.applyGain(juce::Decibels::decibelsToGain(inputGain_.getTargetValue()
                                                        + outputGain_.getTargetValue()));
        return;
    }

    if (static_cast<int>(mono_.size()) < n)
        mono_.resize(static_cast<std::size_t>(n));

    const float invCh = 1.0f / static_cast<float>(numCh);
    for (int i = 0; i < n; ++i)
    {
        float s = 0.0f;
        for (int c = 0; c < numCh; ++c)
            s += buffer.getReadPointer(c)[i];
        mono_[static_cast<std::size_t>(i)] = s * invCh * inputGain_.getNextValue();
    }

    // Run the 48 kHz model, resampling around it when the host rate differs.
    resampler_.process(mono_.data(), mono_.data(), n,
                       [this](float* buf, int m) { engine_.processBlock(buf, buf, m); });

    // Post-model tone stack (Bass/Mid/Treble/Presence), at host sample rate.
    float* eqBuf = mono_.data();
    eq_.process(&eqBuf, &eqBuf, 1, static_cast<unsigned int>(n));

    for (int i = 0; i < n; ++i)
    {
        const float v = mono_[static_cast<std::size_t>(i)] * outputGain_.getNextValue();
        for (int c = 0; c < numCh; ++c)
            buffer.getWritePointer(c)[i] = v;
    }
}

//==============================================================================
// State: parameter tree (via APVTS) + corner profiles as a child node.

void AAOMProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ValueTree state = getParameterManager().getAPVTS().copyState();
    state.removeChild(state.getChildWithName("AAOM_CORNERS"), nullptr);
    state.removeChild(state.getChildWithName("AAOM_MODEL"), nullptr);

    // Model identity: either the id of a compiled-in model or the path it was
    // loaded from, plus the run sha8 it had when saved so a reload can tell
    // "file is gone" apart from "file changed underneath us".
    juce::ValueTree modelNode{"AAOM_MODEL"};
    modelNode.setProperty("path", usingBuiltInModel() ? juce::String{} : modelFile_.getFullPathName(), nullptr);
    modelNode.setProperty("builtin",
                          usingBuiltInModel() ? juce::String(kBuiltInModels[builtInIndex_].id) : juce::String{},
                          nullptr);
    modelNode.setProperty("sha8", bundleRunSha8(), nullptr);
    state.addChild(modelNode, -1, nullptr);

    juce::ValueTree corners{"AAOM_CORNERS"};
    for (int i = 0; i < kNumCorners; ++i)
    {
        const auto& c = corners_[static_cast<std::size_t>(i)];
        if (!c.assigned)
            continue;
        juce::ValueTree node{"CORNER"};
        node.setProperty("index", i, nullptr);
        node.setProperty("name", c.name, nullptr);
        node.setProperty("run", c.runSha8, nullptr);
        node.setProperty("embedding",
                         juce::var(juce::MemoryBlock(c.embedding.data(), c.embedding.size() * sizeof(float))),
                         nullptr);
        corners.addChild(node, -1, nullptr);
    }
    state.addChild(corners, -1, nullptr);

    juce::MemoryOutputStream mos(destData, false);
    state.writeToStream(mos);
}

void AAOMProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    juce::ValueTree state = juce::ValueTree::readFromData(data, sizeInBytes);
    if (!state.isValid())
        return;

    getParameterManager().getAPVTS().replaceState(state);

    // Model first: corner embeddings are validated against the live model's
    // embedding_dim, so the right model has to be in place before they load.
    modelWarning_.clear();
    const juce::ValueTree modelNode = state.getChildWithName("AAOM_MODEL");
    const juce::String savedPath =
        modelNode.isValid() ? modelNode.getProperty("path", juce::var()).toString() : juce::String{};
    const juce::String savedSha =
        modelNode.isValid() ? modelNode.getProperty("sha8", juce::var()).toString() : juce::String{};
    const juce::String savedBuiltIn =
        modelNode.isValid() ? modelNode.getProperty("builtin", juce::var()).toString() : juce::String{};

    if (savedPath.isNotEmpty())
    {
        const juce::File saved{savedPath};
        juce::String message;
        if (!saved.existsAsFile())
        {
            loadBuiltInModel();
            modelWarning_ = "Saved model not found, using built-in: " + savedPath;
        }
        else if (!loadModelFromFile(saved, message))
        {
            loadBuiltInModel();
            modelWarning_ = "Saved model failed to load, using built-in: " + message;
        }
        else if (savedSha.isNotEmpty() && bundleRunSha8() != savedSha)
        {
            // Loaded fine, but it is not the same export the session was saved
            // against — corners below may be dropped by the dim check.
            modelWarning_ = "Model file changed since the session was saved (run " + savedSha + " -> "
                            + bundleRunSha8() + ").";
        }
    }
    else
    {
        // Session was saved on a compiled-in model. An unknown or absent id
        // (older sessions, or a model dropped from the build) falls back to the
        // default rather than refusing to restore.
        const int wanted = builtInIndexForId(savedBuiltIn);
        if (savedBuiltIn.isNotEmpty() && wanted < 0)
            modelWarning_ = "Saved model '" + savedBuiltIn + "' is not in this build, using "
                            + builtInModelName(0) + ".";

        const int index = wanted >= 0 ? wanted : 0;
        if (!usingBuiltInModel() || builtInIndex_ != index)
        {
            const juce::String carried = modelWarning_;
            loadBuiltInModel(index);
            modelWarning_ = carried; // loadBuiltInModel clears it on success
        }
    }

    // Restore corners (fall back to bundle defaults if the node is absent).
    juce::ValueTree corners = state.getChildWithName("AAOM_CORNERS");
    if (!corners.isValid())
    {
        seedCornersFromBundle();
        for (int i = 0; i < kNumCorners; ++i)
            pushCornerToEngine(i);
        return;
    }

    for (auto& c : corners_)
        c = CornerInfo{};

    const int modelDim = embeddingDim();
    for (int ci = 0; ci < corners.getNumChildren(); ++ci)
    {
        juce::ValueTree node = corners.getChild(ci);
        const int i = static_cast<int>(node.getProperty("index", -1));
        if (i < 0 || i >= kNumCorners)
            continue;
        const auto* mb = node.getProperty("embedding", juce::var()).getBinaryData();
        if (mb == nullptr)
            continue;
        const int count = static_cast<int>(mb->getSize() / sizeof(float));
        if (modelDim > 0 && count != modelDim)
            continue; // dimension changed since save — skip stale corner

        CornerInfo info;
        info.assigned = true;
        info.name = node.getProperty("name", juce::var("profile")).toString();
        info.runSha8 = node.getProperty("run", juce::var()).toString();
        info.embedding.resize(static_cast<std::size_t>(count));
        std::memcpy(info.embedding.data(), mb->getData(), mb->getSize());
        corners_[static_cast<std::size_t>(i)] = std::move(info);
    }

    cornerGen_.fetch_add(1);
    for (int i = 0; i < kNumCorners; ++i)
        pushCornerToEngine(i);
}

juce::AudioProcessorEditor* AAOMProcessor::createEditor()
{
    return new AAOMEditor(*this);
}

} // namespace aaom

CREATE_PLUGIN(aaom::AAOMProcessor)
