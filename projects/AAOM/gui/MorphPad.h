#pragma once

#include <array>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace aaom
{

// Bilinear weights over the 4 corners, in MorphEngine's corner order
// (0=BL, 1=BR, 2=TL, 3=TR). x,y are normalised [0,1] with y=1 at the top,
// matching the puck's on-screen position. Ported verbatim from
// MorphEngine::computeTarget() so the UI meters always agree with the DSP.
struct CornerWeights
{
    float bl, br, tl, tr;
};
CornerWeights computeCornerWeights(float x, float y);

// The recessed CRT-style morph screen: phosphor grid, scanlines, additive
// corner glows, and a knurled draggable puck. The signature control of the
// plugin. Emits normalised (x,y) via onDrag while dragging; the host/editor
// pushes automation back in through setDotPosition().
class MorphPad : public juce::Component
{
public:
    MorphPad();

    std::function<void(float x, float y)> onDrag;

    void setDotPosition(float x, float y); // no callback; for automation sync
    void setCornerAssigned(const std::array<bool, 4>& assigned);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> screenArea() const;
    void handleMouse(const juce::MouseEvent&);

    float x_ = 0.5f;
    float y_ = 0.5f;
    std::array<bool, 4> assigned_{{false, false, false, false}};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MorphPad)
};

} // namespace aaom
