#pragma once

#include <juce_core/juce_core.h>

namespace mrta
{

class ParameterFIFO
{
public:
    static constexpr size_t Capacity { 128 };

    ParameterFIFO();

    void clear();
    bool pushParameter(const juce::String& parameterID, float newValue);
    std::pair<bool, std::pair<juce::String, float>> popParameter();

private:
    juce::AbstractFifo abstractFIFO;
    std::array<std::pair<juce::String, float>, Capacity> buffer;

    JUCE_DECLARE_NON_COPYABLE(ParameterFIFO)
    JUCE_DECLARE_NON_MOVEABLE(ParameterFIFO)
    JUCE_LEAK_DETECTOR(ParameterFIFO)
};

}
