#pragma once

#include <array>

#include "AddSynthConstants.h"

namespace DSP
{
using CoefficientArray = std::array<float, kMaxPartials>;

enum class CoefficientSourceType
{
    Manual = 0,
    Preset,
    WaveformTable
};

class SpectralCoefficients
{
public:
    enum class TableSource
    {
        None = 0,
        Flute,
        TrumpetC,
        Violin
    };

    enum class PresetShape
    {
        Sine = 0,
        Sawtooth,
        Square,
        Triangle,
    };

    SpectralCoefficients();

    void computeCoefficients(CoefficientArray& out, int numPartials, int midiNote, int velocity);


    // manual
    void setManualAmp(int index, float amp);
    void setManualLevel(float level);

    // preset
    void setPresetShape(PresetShape s);
    void setPresetBrightness(float b);
    void setPresetLevel(float level);

    // precompute table
    void setTableSource(TableSource src);
    void setTableLevel(float level);

    // optional external coefficients to use as a continuous synthesis such as vocoder
    void setExternalCoefficients(const CoefficientArray& coeffs);
    void setExternalLevel(float level);


private:
    std::array<float, kManualPartials> manualAmps {};
    PresetShape presetShape { PresetShape::Sine };
    float presetBrightness { 1.0f };

    CoefficientArray tableBuf {};
    CoefficientArray externalBuf {};
    float manualLevel { 1.0f };
    float presetLevel { 0.0f };
    float tableLevel { 0.0f };
    float externalLevel { 0.0f };
    TableSource tableSource { TableSource::None };
};
}
