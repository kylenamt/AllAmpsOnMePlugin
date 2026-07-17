#include <ParameterComponents.h>

namespace mrta
{

ParameterSlider::ParameterSlider(const juce::String& paramID, juce::AudioProcessorValueTreeState& apvts) :
    juce::Slider(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxRight),
    att(apvts, paramID, *this)
{
    juce::AudioParameterFloat* param { dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter(paramID)) };
    if (!param)
    {
        // Parameter type is not Float
        jassertfalse;
    }
}

ParameterComboBox::ParameterComboBox(const juce::String& paramID, juce::AudioProcessorValueTreeState& apvts) :
    att(apvts, paramID, *this)
{
    juce::AudioParameterChoice* param { dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(paramID)) };
    if (!param)
    {
        // Parameter type is not Choice
        jassertfalse;
    }

    addItemList(param->choices, 1);
    setSelectedItemIndex(param->getIndex());
}

ParameterButton::ParameterButton(const juce::String& paramID, juce::AudioProcessorValueTreeState& apvts) :
    att(apvts, paramID, *this)
{
    juce::AudioParameterBool* param { dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter(paramID)) };
    if (!param)
    {
        // Parameter type is not Bool
        jassertfalse;
    }

    juce::StringArray labels { param->getAllValueStrings() };

    setClickingTogglesState(true);
    setButtonText(labels[param->get() ? 1 : 0]);
    onStateChange = [labels, this]
    {
        setButtonText(labels[getToggleState() ? 1 : 0]);
    };
}

}
