#pragma once

#include <array>
#include <memory>

#include <ParameterComponents.h>

#include "gui/AAOMLookAndFeel.h"
#include "gui/AmpSlotComponent.h"
#include "gui/LcdReadout.h"
#include "gui/MorphPad.h"
#include "gui/ProfileLibrary.h"

namespace aaom
{

class AAOMProcessor;

// Hardware-styled editor: brushed-metal chassis, engraved nameplate, 4 amp
// corner modules around a CRT-style morph screen, an EQ fader bank, and
// input/output knobs. Ported from the design handoff in
// .claude/Audio plugin profile mixer/design_handoff_amp_morph.
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
    void handleSelect(int corner);
    void handlePaste(int corner);
    void handleClear(int corner);
    void handlePick(int corner, int profileIndex);
    void doPaste(int corner, const juce::String& json, bool allowRunMismatch);

    // Model selector (the LCD chip in the nameplate).
    void showModelMenu();
    void browseForModel();
    void loadModel(const juce::File& file);
    void refreshModelChip();

    AAOMProcessor& proc_;

    AAOMLookAndFeel lookAndFeel_;
    juce::TooltipWindow tooltipWindow_{this};

    MorphPad pad_;
    std::array<std::unique_ptr<AmpSlotComponent>, 4> slots_;
    ProfileLibrary library_;

    juce::Label subtitle_;
    LcdReadout presetChip_;

    mrta::ParameterSlider eqBass_;
    mrta::ParameterSlider eqMid_;
    mrta::ParameterSlider eqTreble_;
    mrta::ParameterSlider eqPresence_;
    std::array<juce::Label, 4> eqLabels_;

    mrta::ParameterSlider inputGain_;
    mrta::ParameterSlider outputGain_;
    LcdReadout inputLcd_;
    LcdReadout outputLcd_;
    juce::Label inputLabel_;
    juce::Label outputLabel_;

    // Bounds cached in resized(), drawn in paint().
    juce::Rectangle<float> titleBounds_;
    juce::Rectangle<float> bottomStripBounds_;
    int bottomStripDividerX_ = 0;

    int lastCornerGen_ = -1;
    int lastModelGen_ = -1;

    // Kept alive for the duration of the async native file dialog.
    std::unique_ptr<juce::FileChooser> chooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AAOMEditor)
};

} // namespace aaom
