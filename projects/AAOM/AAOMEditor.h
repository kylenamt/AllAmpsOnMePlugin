#pragma once

#include <array>
#include <memory>

#include <GenericParameterEditor.h>

#include "gui/CornerSlot.h"
#include "gui/XYPad.h"

namespace aaom
{

class AAOMProcessor;

// M4 editor: custom XY morph pad with 4 corner profile slots (paste/clear +
// validation), input/output gain knobs, and a model status line.
class AAOMEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AAOMEditor(AAOMProcessor& processor);
    ~AAOMEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshCorners();
    void handlePaste(int corner);
    void handleClear(int corner);
    void doPaste(int corner, const juce::String& json, bool allowRunMismatch);

    AAOMProcessor& proc_;

    juce::Label title_;
    juce::Label status_;
    XYPad pad_;
    std::array<std::unique_ptr<CornerSlot>, 4> slots_;
    mrta::GenericParameterEditor gains_;

    int lastCornerGen_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AAOMEditor)
};

} // namespace aaom
