#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace mrta
{

class ParameterSlider : public juce::Slider
{
public:
    ParameterSlider(const juce::String& paramID, juce::AudioProcessorValueTreeState& apvts);
    ParameterSlider() = delete;

private:
    juce::AudioProcessorValueTreeState::SliderAttachment att;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParameterSlider)
};

class ParameterComboBox : public juce::ComboBox
{
public:
    ParameterComboBox(const juce::String& paramID, juce::AudioProcessorValueTreeState& apvts);
    ParameterComboBox() = delete;

private:
    juce::AudioProcessorValueTreeState::ComboBoxAttachment att;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParameterComboBox)
};

class ParameterButton : public juce::TextButton
{
public:
    ParameterButton(const juce::String& paramID, juce::AudioProcessorValueTreeState& apvts);
    ParameterButton() = delete;

private:
    juce::AudioProcessorValueTreeState::ButtonAttachment att;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParameterButton)
};

}

