#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace aaom
{

// "Morph field" theme: conic-arc rotary dials, flat horizontal sliders
// (RANGE/SMOOTH), a pill on/off switch (CAB IR, keyed by component ID
// "cabSwitch"), square icon buttons, and an LCD-styled TextEditor for the
// profile library's search field.
class AAOMLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AAOMLookAndFeel();

    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float sliderPosProportional,
                          float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                         float minSliderPos, float maxSliderPos, const juce::Slider::SliderStyle,
                         juce::Slider&) override;

    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawButtonText(juce::Graphics&, juce::TextButton&, bool shouldDrawButtonAsHighlighted,
                        bool shouldDrawButtonAsDown) override;

    void fillTextEditorBackground(juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline(juce::Graphics&, int width, int height, juce::TextEditor&) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AAOMLookAndFeel)
};

} // namespace aaom
