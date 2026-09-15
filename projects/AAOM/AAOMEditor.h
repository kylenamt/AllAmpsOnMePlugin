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

// "Morph field" editor: dark cyan/graphite panel, a color-mosaic morph pad
// with an adjustable extrapolation RANGE, 4 corner cards, a 6-knob tone-stack
// row, and a CAB IR strip. Ported from the design handoff in
// .claude/design_handoff_morph_pad.
class AAOMEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AAOMEditor(AAOMProcessor& processor);
    ~AAOMEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    // Label that fires onClick on mouseDown -- used for the CAB IR name,
    // which (unlike a button) has no border/background of its own, just
    // centered plain text sitting in the CAB IR bar's own recessed panel.
    class ClickableLabel : public juce::Label
    {
    public:
        std::function<void()> onClick;
        void mouseDown(const juce::MouseEvent&) override;
    };

    void timerCallback() override;
    void refreshCorners();
    void handleSelect(int corner);
    void handlePaste(int corner);
    void handleClear(int corner);
    void handlePick(int corner, int profileIndex);
    void doPaste(int corner, const juce::String& json, bool allowRunMismatch);

    // Model selector (the status pill in the header).
    void showModelMenu();
    void browseForModel();
    void loadModel(const juce::File& file);
    void refreshModelChip();

    // Cabinet IR (the name text in the CAB IR bar).
    void showCabMenu();
    void browseForIr();
    void loadIr(const juce::File& file);
    void refreshCabChip();
    void updateCabText(); // text/colour only; called from refreshCabChip() and on cabOn changes

    AAOMProcessor& proc_;

    AAOMLookAndFeel lookAndFeel_;
    juce::TooltipWindow tooltipWindow_{this};

    MorphPad pad_;
    std::array<std::unique_ptr<AmpSlotComponent>, 4> slots_;
    ProfileLibrary library_;

    LcdReadout presetChip_;
    juce::Label help_;

    // RANGE / SMOOTH controls block, below the corner cards. SLERP is a third
    // row in the same block, visible only for a model whose embeddings are
    // normalised (MorphModel::embeddingsNormalized()) -- see refreshModelChip().
    mrta::ParameterSlider rangeSlider_;
    mrta::ParameterSlider smoothSlider_;
    juce::Label rangeLabel_, rangeValue_;
    juce::Label smoothLabel_, smoothValue_;
    mrta::ParameterButton slerpToggle_;
    juce::Label slerpLabel_;

    // Knob row: Input, Bass, Mid, Treble, Presence | divider | Output.
    mrta::ParameterSlider inputGain_;
    mrta::ParameterSlider eqBass_;
    mrta::ParameterSlider eqMid_;
    mrta::ParameterSlider eqTreble_;
    mrta::ParameterSlider eqPresence_;
    mrta::ParameterSlider outputGain_;
    std::array<juce::Label, 6> knobLabels_;
    std::array<juce::Label, 6> knobValues_;

    // CAB IR bar: engraved label, clickable IR name (opens the load menu),
    // on/off switch.
    juce::Label cabLabel_;
    ClickableLabel cabName_;
    mrta::ParameterButton cabToggle_;
    bool lastCabOn_ = true;

    // Bounds cached in resized(), drawn in paint().
    juce::Rectangle<float> dividerBounds_;
    juce::Rectangle<float> wordmarkBounds_;
    juce::Rectangle<float> helpBounds_;
    juce::Rectangle<float> controlsBlockBounds_;
    juce::Rectangle<float> knobRowBounds_;
    float knobDividerX_ = 0.0f;
    juce::Rectangle<float> cabBarBounds_;
    float cabDividerX_ = 0.0f;

    int lastCornerGen_ = -1;
    int lastModelGen_ = -1;
    int lastIrGen_ = -1;

    // Kept alive for the duration of the async native file dialog.
    std::unique_ptr<juce::FileChooser> chooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AAOMEditor)
};

} // namespace aaom
