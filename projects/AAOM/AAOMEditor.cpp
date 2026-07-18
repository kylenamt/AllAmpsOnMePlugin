#include "AAOMEditor.h"
#include "AAOMProcessor.h"

namespace aaom
{

AAOMEditor::AAOMEditor(AAOMProcessor& processor)
: juce::AudioProcessorEditor(processor)
, proc_(processor)
, gains_(processor.getParameterManager(), 84, {pid::inputGain, pid::outputGain})
{
    title_.setText("AllAmpsOnMe — Morph", juce::dontSendNotification);
    title_.setFont(juce::Font(20.0f, juce::Font::bold));
    addAndMakeVisible(title_);

    juce::String status;
    if (proc_.modelLoaded())
    {
        const auto* m = proc_.engine().model();
        status = juce::String("Model: C=") + juce::String(m->channels()) + ", E="
                 + juce::String(m->embeddingDim()) + ", " + juce::String(m->numLayers())
                 + " layers  •  run " + juce::String(m->runSha8());
    }
    else
    {
        status = proc_.bundleError().isNotEmpty() ? proc_.bundleError() : juce::String("No model loaded");
    }
    status_.setText(status, juce::dontSendNotification);
    status_.setFont(juce::Font(12.0f));
    status_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(status_);

    pad_.onDrag = [this](float x, float y) {
        auto& apvts = proc_.getParameterManager().getAPVTS();
        if (auto* px = apvts.getParameter(pid::morphX))
            px->setValueNotifyingHost(x);
        if (auto* py = apvts.getParameter(pid::morphY))
            py->setValueNotifyingHost(y);
    };
    addAndMakeVisible(pad_);

    for (int i = 0; i < 4; ++i)
    {
        slots_[static_cast<std::size_t>(i)] = std::make_unique<CornerSlot>(i);
        slots_[static_cast<std::size_t>(i)]->onPaste = [this](int c) { handlePaste(c); };
        slots_[static_cast<std::size_t>(i)]->onClear = [this](int c) { handleClear(c); };
        addAndMakeVisible(*slots_[static_cast<std::size_t>(i)]);
    }

    addAndMakeVisible(gains_);

    refreshCorners();
    setSize(560, 540);
    startTimerHz(30);
}

AAOMEditor::~AAOMEditor()
{
    stopTimer();
}

void AAOMEditor::timerCallback()
{
    auto& apvts = proc_.getParameterManager().getAPVTS();
    float x = 0.5f, y = 0.5f;
    if (auto* rx = apvts.getRawParameterValue(pid::morphX))
        x = rx->load();
    if (auto* ry = apvts.getRawParameterValue(pid::morphY))
        y = ry->load();
    pad_.setDotPosition(x, y);

    if (proc_.cornerGeneration() != lastCornerGen_)
        refreshCorners();
}

void AAOMEditor::refreshCorners()
{
    lastCornerGen_ = proc_.cornerGeneration();
    std::array<bool, 4> assigned{{false, false, false, false}};
    for (int i = 0; i < 4; ++i)
    {
        const auto& c = proc_.corner(i);
        assigned[static_cast<std::size_t>(i)] = c.assigned;
        slots_[static_cast<std::size_t>(i)]->setContents(c.assigned, c.name);
    }
    pad_.setCornerAssigned(assigned);
}

void AAOMEditor::handlePaste(int corner)
{
    const juce::String json = juce::SystemClipboard::getTextFromClipboard();
    if (json.trim().isEmpty())
    {
        juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Paste profile",
                                                    "Copy an aaom_profile JSON to the clipboard, then click Paste.",
                                                    this);
        return;
    }
    doPaste(corner, json, false);
}

void AAOMEditor::doPaste(int corner, const juce::String& json, bool allowRunMismatch)
{
    juce::String message;
    const PasteResult result = proc_.tryPasteProfile(corner, json, allowRunMismatch, message);

    switch (result)
    {
        case PasteResult::Ok:
        case PasteResult::OkRunMismatch:
            refreshCorners();
            break;

        case PasteResult::RejectedRunMismatch:
            juce::AlertWindow::showOkCancelBox(
                juce::MessageBoxIconType::WarningIcon, "Run mismatch", message, "Load anyway", "Cancel", this,
                juce::ModalCallbackFunction::create([this, corner, json](int r) {
                    if (r == 1)
                        doPaste(corner, json, true);
                }));
            break;

        case PasteResult::RejectedDim:
        case PasteResult::RejectedParse:
        default:
            juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Paste rejected",
                                                        message, this);
            break;
    }
}

void AAOMEditor::handleClear(int corner)
{
    proc_.clearCorner(corner);
    refreshCorners();
}

void AAOMEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1d22));
}

void AAOMEditor::resized()
{
    auto r = getLocalBounds().reduced(12);
    title_.setBounds(r.removeFromTop(28));
    status_.setBounds(r.removeFromTop(20));
    r.removeFromTop(6);

    auto gainsArea = r.removeFromBottom(96);
    gains_.setBounds(gainsArea);
    r.removeFromBottom(6);

    // Middle: corner slots pinned to the 4 corners, XY pad inset in the centre.
    const int slotW = 120;
    const int slotH = 52;
    auto middle = r;

    slots_[2]->setBounds(middle.getX(), middle.getY(), slotW, slotH);                          // TL
    slots_[3]->setBounds(middle.getRight() - slotW, middle.getY(), slotW, slotH);               // TR
    slots_[0]->setBounds(middle.getX(), middle.getBottom() - slotH, slotW, slotH);             // BL
    slots_[1]->setBounds(middle.getRight() - slotW, middle.getBottom() - slotH, slotW, slotH); // BR

    pad_.setBounds(middle.reduced(0, slotH + 8));
}

} // namespace aaom
