#pragma once

#include <array>
#include <cmath>
#include <juce_core/juce_core.h>

#include "AddSynthConstants.h"

namespace DSP
{
class SineWavetable
{
public:
    static float lookup(float phase)
    {
        const auto& table = getTable();
        const float index = phase * phaseStep;
        const int i0 = juce::jlimit(0, kTableSize - 1, static_cast<int>(index));
        const float frac = index - static_cast<float>(i0);
        return table[i0] + frac * (table[i0 + 1] - table[i0]); // interpolating
    }

    
    static constexpr int kTableSize = kWavetableSize;
    inline static float phaseStep = (static_cast<float>(kTableSize) / (2*M_PI));

    static const std::array<float, kTableSize + 1>& getTable()
    {
        static const std::array<float, kTableSize + 1> table = buildTable(); // helps cache the table
        return table;
    }


private:
    static std::array<float, kTableSize + 1> buildTable()
    {
        std::array<float, kTableSize + 1> table {};
        for (int i = 0; i <= kTableSize; ++i)
            table[i] = std::sin(2*M_PI * i / static_cast<float>(kTableSize));
        return table;
    }
};
}
