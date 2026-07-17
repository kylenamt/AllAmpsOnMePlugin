#include <ParameterFIFO.h>

namespace mrta
{

ParameterFIFO::ParameterFIFO() :
    abstractFIFO { Capacity }
{
    static_assert(Capacity > 0, "Capacity should be at least 1.");
}

void ParameterFIFO::clear()
{
    abstractFIFO.reset();
}

bool ParameterFIFO::pushParameter(const juce::String& parameterID, float newValue)
{
    if (abstractFIFO.getFreeSpace() == 0)
        return false;

    auto scope = abstractFIFO.write(1);

    if (scope.blockSize1 > 0)
        buffer[scope.startIndex1] = std::make_pair(parameterID, newValue);

    if (scope.blockSize2 > 0)
        buffer[scope.startIndex2] = std::make_pair(parameterID, newValue);

    return true;
}

std::pair<bool, std::pair<juce::String, float>> ParameterFIFO::popParameter()
{
    if (abstractFIFO.getNumReady() == 0)
        return {};

    auto scope = abstractFIFO.read(1);

    if (scope.blockSize1 > 0)
        return { true, buffer[scope.startIndex1] };

    if (scope.blockSize2 > 0)
        return {true, buffer[scope.startIndex2] };

    return { false, { "", 0.f } };
}

}
