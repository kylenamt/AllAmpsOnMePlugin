#pragma once

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

namespace aaom
{

// Bilinear weights over the 4 corners, in MorphEngine's corner order
// (0=BL, 1=BR, 2=TL, 3=TR). x,y are field coordinates: [0,1] is the assigned
// corner square (y=1 at the top, matching the puck's on-screen position), the
// rest extrapolates past it. Matches the design handoff's
// TL=(1-x)y, TR=xy, BL=(1-x)(1-y), BR=x(1-y) exactly.
struct CornerWeights
{
    float bl, br, tl, tr;
};
CornerWeights computeCornerWeights(float x, float y);

// The recessed morph field: a flat color mosaic (one swatch per grid cell,
// blended from the 4 corner colors), phosphor-style tick grid, and a
// draggable square puck. The signature control of the plugin. RANGE
// (setRange) controls how far past the [0,1] corner square the field extends;
// the puck can leave the corner square to linearly extrapolate out to that
// domain. Emits field-space (x,y) via onDrag while dragging; the
// host/editor pushes automation back in through setDotPosition().
class MorphPad : public juce::Component
{
public:
    MorphPad();

    std::function<void(float x, float y)> onDrag;

    void setRange(float r);              // 0.10..1.00; rescales the field domain
    void setDotPosition(float x, float y); // no callback; for automation sync

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;

private:
    struct Cell
    {
        juce::Rectangle<float> rect;
        juce::Colour colour;
    };

    void rebuildMosaic();
    void handleMouse(const juce::MouseEvent&);
    // Field -> local pixel space. Clamped to the current [-r,1+r] domain, so a
    // stored position from a wider RANGE still pins to the visible edge
    // rather than drawing off-component.
    juce::Point<float> fieldToLocal(float fx, float fy) const;
    juce::Point<float> localToField(juce::Point<float> local) const; // inverse, snapped to 0.05

    float r_ = 0.5f;
    float x_ = 0.5f;
    float y_ = 0.5f;

    std::vector<Cell> mosaic_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MorphPad)
};

} // namespace aaom
