#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace aaom
{

// Small reusable "LCD" chip: dark recessed background, glowing mono text.
// Used for the preset chip, the input/output gain readouts, and (indirectly,
// via its colour scheme) anywhere else a phosphor-style readout is needed.
// Inherits SettableTooltipClient so a chip can carry diagnostic info (e.g.
// the preset chip surfaces the loaded model's stats/errors on hover, since
// the hardware chassis has no room to show that text permanently).
class LcdReadout : public juce::Component, public juce::SettableTooltipClient
{
public:
    LcdReadout();

    void setText(const juce::String& text);
    void setTextColour(juce::Colour colour);
    void setCaretShown(bool shown); // small dropdown triangle at the left edge

    void paint(juce::Graphics&) override;

private:
    juce::String text_;
    juce::Colour textColour_;
    bool caret_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LcdReadout)
};

} // namespace aaom
