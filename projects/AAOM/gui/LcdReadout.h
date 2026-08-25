#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace aaom
{

// The header's status pill: a small recessed chip with glowing mono text.
// Currently used only for the model selector, but kept as its own component
// (rather than folded into AAOMEditor) since it also carries a diagnostic
// tooltip (the loaded model's stats, or the load error/warning) and a click
// handler, both independent of the editor's own layout code.
class LcdReadout : public juce::Component, public juce::SettableTooltipClient
{
public:
    LcdReadout();

    void setText(const juce::String& text);
    void setTextColour(juce::Colour colour);

    // Set to make the chip behave as a button (e.g. the model selector menu).
    // Only fires when the chip also intercepts mouse clicks.
    std::function<void()> onClick;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    juce::String text_;
    juce::Colour textColour_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LcdReadout)
};

} // namespace aaom
