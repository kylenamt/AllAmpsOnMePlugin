#pragma once

#include <array>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace aaom
{

// A square 2D morph pad with a draggable dot. Emits normalised (x,y) in [0,1]
// (y up) while dragging via onDrag; the host/editor pushes automation back in
// through setDotPosition(). Corner markers brighten when a corner is assigned.
class XYPad : public juce::Component
{
public:
    XYPad();

    std::function<void(float x, float y)> onDrag;

    void setDotPosition(float x, float y); // no callback; for automation sync
    void setCornerAssigned(const std::array<bool, 4>& assigned);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> padArea() const;
    void handleMouse(const juce::MouseEvent&);

    float x_ = 0.5f;
    float y_ = 0.5f;
    std::array<bool, 4> assigned_{{false, false, false, false}};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(XYPad)
};

} // namespace aaom
