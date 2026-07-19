#pragma once

#include <juce_graphics/juce_graphics.h>

namespace aaom::palette
{

// Hardware theme design tokens, ported from the design handoff
// (.claude/Audio plugin profile mixer/design_handoff_amp_morph). Brushed-metal
// chassis, engraved nameplate, CRT morph screen, LED meters, LCD readouts.

// Chassis metal (top -> bottom gradient stops).
inline const juce::Colour chassisTop{0xff454039};
inline const juce::Colour chassisMid{0xff38332c};
inline const juce::Colour chassisBottom{0xff2c2823};

// Module / strip panels (top -> bottom gradient stops).
inline const juce::Colour panelTop{0xff2c2822};
inline const juce::Colour panelBottom{0xff211d18};
inline const juce::Colour panelDeep{0xff1a1611};

// Deep insets / near-black recesses.
inline const juce::Colour inkBorder{0xff14110d};
inline const juce::Colour inkTrack{0xff12100c};
inline const juce::Colour inkLcdBottom{0xff0c0f0a};
inline const juce::Colour inkBezel{0xff0a0806};

// Screw metal (radial gradient stops).
inline const juce::Colour screwLight{0xff9a9188};
inline const juce::Colour screwDark{0xff3a352f};

// Accent (amber/gold).
inline const juce::Colour accent{0xffe8ab3d};
inline const juce::Colour accentHardware{0xffc78a24};
inline const juce::Colour accentBright{0xfff0c766};

// LCD readouts.
inline const juce::Colour lcdAmberText{0xffffcf7a};
inline const juce::Colour lcdGreenText{0xffbfe27a};
inline const juce::Colour lcdBgTop{0xff141a12};

// Text.
inline const juce::Colour textLight{0xffe8ddcb};
inline const juce::Colour textName{0xffddd3c4};
inline const juce::Colour textMuted{0xffa89a86};
inline const juce::Colour textDim{0xff9a9188};
inline const juce::Colour textFaint{0xff8a7f70};
inline const juce::Colour textEngraved{0xff211d17};
inline const juce::Colour textEngravedDeep{0xff1a1206};

// CLR / destructive (red).
inline const juce::Colour clearTop{0xff813127};
inline const juce::Colour clearBottom{0xff4a160f};
inline const juce::Colour clearText{0xfff0cfc7};

// CRT morph screen bezel + phosphor background.
inline const juce::Colour crtBezelTop{0xff161310};
inline const juce::Colour crtBezelBottom{0xff0c0a07};
inline const juce::Colour crtScreenCentre{0xff1a1811};
inline const juce::Colour crtScreenEdge{0xff0a0805};

// Knurled metal (knob body + puck ridges).
inline const juce::Colour knurlLight{0xff4a453d};
inline const juce::Colour knurlDark{0xff34302a};
inline const juce::Colour knurlRim{0xff23201b};
inline const juce::Colour puckCoreHi{0xfffff2d0};
inline const juce::Colour puckCoreEdge{0xff8a5514};

// Desk backdrop behind the chassis.
inline const juce::Colour deskTop{0xff2c2114};
inline const juce::Colour deskMid{0xff1a130c};
inline const juce::Colour deskBottom{0xff120d07};
inline const juce::Colour scrim{0xdd080604};

} // namespace aaom::palette
