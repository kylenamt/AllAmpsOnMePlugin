#include "AAOMProcessor.h"
#include "AAOMEditor.h"

#include <cstring>
#include <exception>
#include <utility>

#include <BinaryData.h>

namespace aaom
{

static std::vector<mrta::ParameterInfo> makeParameters()
{
    return {
        {pid::inputGain, "Input", "dB", 0.0f, -24.0f, 24.0f, 0.1f, 1.0f},
        {pid::outputGain, "Output", "dB", 0.0f, -24.0f, 24.0f, 0.1f, 1.0f},
        {pid::morphX, "Morph X", "", 0.5f, 0.0f, 1.0f, 0.001f, 1.0f},
        {pid::morphY, "Morph Y", "", 0.5f, 0.0f, 1.0f, 0.001f, 1.0f},
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

    loadBundle();
    seedCornersFromBundle();
}

AAOMProcessor::~AAOMProcessor() = default;

juce::String AAOMProcessor::bundleRunSha8() const
{
    return engine_.ready() ? juce::String(engine_.model()->runSha8()) : juce::String{};
}

void AAOMProcessor::loadBundle()
{
    juce::String text;

#ifdef AAOM_DEV_BUNDLE_PATH
    // Dev iteration path: load a bundle from disk without rebuilding the binary.
    const juce::File devFile{AAOM_DEV_BUNDLE_PATH};
    if (devFile.existsAsFile())
        text = devFile.loadFileAsString();
#endif

    if (text.isEmpty())
        text = juce::String::fromUTF8(BinaryData::bundle_json, BinaryData::bundle_jsonSize);

    try
    {
        engine_.setModel(MorphModel::fromBundleJson(text.toStdString()));
        bundleError_.clear();
    }
    catch (const std::exception& e)
    {
        bundleError_ = juce::String("Bundle load failed: ") + e.what();
    }
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

    // Set up host <-> 48 kHz resampling; the model always runs at 48 kHz.
    resampler_.prepare(sampleRate, maxBufferSize);
    const double innerRate = resampler_.active() ? Resampler48k::kModelRate : sampleRate;
    const int innerBlock = resampler_.active() ? resampler_.maxInnerBlock() : maxBufferSize;
    setLatencySamples(resampler_.latencySamples());

    // Seed the engine's corners directly (audio thread not running yet), then
    // build the DSP at the current morph position.
    if (engine_.ready())
    {
        for (int i = 0; i < kNumCorners; ++i)
        {
            const auto& c = corners_[static_cast<std::size_t>(i)];
            if (c.assigned)
                engine_.setCorner(i, c.embedding);
            else
                engine_.clearCorner(i);
        }
    }

    engine_.prepare(innerRate, innerBlock);
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
