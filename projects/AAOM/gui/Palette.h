#pragma once

#include <array>

#include <juce_graphics/juce_graphics.h>

namespace aaom::palette
{

// "Morph field" design tokens, ported from the design handoff
// (.claude/design_handoff_morph_pad). Dark cyan/graphite panel, recessed CRT-
// style morph pad, flat corner-color mosaic, LCD-style status pill.

// Neutrals.
inline const juce::Colour shell{0xff15181b};       // panel body, modal body
inline const juce::Colour shellLightTop{0xff1c2024};    // raised cards, knob row (top stop)
inline const juce::Colour shellLightBottom{0xff161a1d}; //   (bottom stop)
inline const juce::Colour shellLeadTop{0xff1e2226};      // leading card, modal header (top stop)
inline const juce::Colour shellLeadBottom{0xff171a1e};   //   (bottom stop)
inline const juce::Colour recessDeep{0xff0b0d0f};  // pad well, search field
inline const juce::Colour recess{0xff0d0f11};      // CAB bar, readout / status pill
inline const juce::Colour recessBottom{0xff101316}; // status pill bottom stop
inline const juce::Colour listBody{0xff0f1214};     // library list background
inline const juce::Colour searchBarBg{0xff101315};  // library search bar background
inline const juce::Colour emptyCardBg{0xff131619};  // unassigned corner card
inline const juce::Colour listRowBg{0xff14181b};    // library row background
inline const juce::Colour trackBg{0xff0c0e10};      // slider / weight-bar track
inline const juce::Colour smallButtonBg{0xff0e1113}; // small icon buttons (search/clear/close)
inline const juce::Colour page{0xff0a0b0c};         // area behind the plugin panel

// Text.
inline const juce::Colour textPrimary{0xffdbe3e5};
inline const juce::Colour textValue{0xffc9d2d4};
inline const juce::Colour textSecondary{0xff9fb0b2};
inline const juce::Colour textLabel{0xff9aa6a8};
inline const juce::Colour textMuted{0xff788688};
inline const juce::Colour textDim{0xff7d8d8f};
inline const juce::Colour textDim2{0xff7f8c8e};
inline const juce::Colour textDim3{0xff6a7a7c};
inline const juce::Colour textDisabled{0xff5d6a6c};
inline const juce::Colour textDisabled2{0xff546264};
inline const juce::Colour textDisabled3{0xff3f4c4e};
inline const juce::Colour textIcon{0xff8e9a9c};

// Accents.
inline const juce::Colour cyan{0xff2fbfc9};       // house accent
inline const juce::Colour cyanLight{0xff8ceaf0};  // search text, selected row
inline const juce::Colour cyanMid1{0xff1f9aa1};   // gradient stops
inline const juce::Colour cyanMid2{0xff54d7de};
inline const juce::Colour cyanMid3{0xff14757c};

inline const juce::Colour cornerTL{0xffe8a13c}; // amber
inline const juce::Colour cornerTR{0xff2fbfc9}; // cyan
inline const juce::Colour cornerBL{0xff9b7bf0}; // violet
inline const juce::Colour cornerBR{0xff8fd14f}; // lime

// Corner color by MorphEngine's weight order (0=BL, 1=BR, 2=TL, 3=TR) --
// matches AmpSlotComponent/MorphPad/ProfileLibrary's shared indexing.
inline juce::Colour cornerColour(int index)
{
    switch (index)
    {
        case 0: return cornerBL;
        case 1: return cornerBR;
        case 2: return cornerTL;
        default: return cornerTR;
    }
}

// Negative (extrapolated) weight.
inline const juce::Colour negativeText{0xffd98b7c};
inline const juce::Colour negativeBarTop{0xffe39c8d};
inline const juce::Colour negativeBarBottom{0xffa85d4d};

// Modal scrim.
inline const juce::Colour scrim{0xb8060809};

} // namespace aaom::palette
