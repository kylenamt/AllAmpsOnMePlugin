#pragma once

#include "ParameterComponents.h"

namespace mrta
{

class ImageKnob : public ParameterSlider
{
public:
    ImageKnob(const juce::String& paramID, juce::AudioProcessorValueTreeState& apvts, const juce::Image& image);
    ~ImageKnob();

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    juce::Image knobImage;
    juce::NormalisableRange<float> normRange;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ImageKnob)
};

}
