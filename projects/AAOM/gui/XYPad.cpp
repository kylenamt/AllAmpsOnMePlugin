#include "XYPad.h"

namespace aaom
{

XYPad::XYPad()
{
    setInterceptsMouseClicks(true, false);
}

void XYPad::setDotPosition(float x, float y)
{
    x_ = juce::jlimit(0.0f, 1.0f, x);
    y_ = juce::jlimit(0.0f, 1.0f, y);
    repaint();
}

void XYPad::setCornerAssigned(const std::array<bool, 4>& assigned)
{
    if (assigned != assigned_)
    {
        assigned_ = assigned;
        repaint();
    }
}

juce::Rectangle<float> XYPad::padArea() const
{
    return getLocalBounds().toFloat().reduced(6.0f);
}

void XYPad::paint(juce::Graphics& g)
{
    const auto area = padArea();

    g.setColour(juce::Colour(0xff20242b));
    g.fillRoundedRectangle(area, 6.0f);
    g.setColour(juce::Colour(0xff3a4049));
    g.drawRoundedRectangle(area, 6.0f, 1.5f);

    // Grid.
    g.setColour(juce::Colour(0x22ffffff));
    for (int i = 1; i < 4; ++i)
    {
        const float fx = area.getX() + area.getWidth() * (i / 4.0f);
        const float fy = area.getY() + area.getHeight() * (i / 4.0f);
        g.drawVerticalLine(juce::roundToInt(fx), area.getY(), area.getBottom());
        g.drawHorizontalLine(juce::roundToInt(fy), area.getX(), area.getRight());
    }

    // Corner markers: index 0=BL, 1=BR, 2=TL, 3=TR (matches MorphEngine weights).
    const std::array<juce::Point<float>, 4> corners{{{area.getX(), area.getBottom()},
                                                     {area.getRight(), area.getBottom()},
                                                     {area.getX(), area.getY()},
                                                     {area.getRight(), area.getY()}}};
    for (int i = 0; i < 4; ++i)
    {
        g.setColour(assigned_[static_cast<std::size_t>(i)] ? juce::Colour(0xff5fb0ff)
                                                           : juce::Colour(0x33ffffff));
        g.fillEllipse(juce::Rectangle<float>(14.0f, 14.0f).withCentre(corners[static_cast<std::size_t>(i)]));
    }

    // Dot.
    const float dx = area.getX() + x_ * area.getWidth();
    const float dy = area.getY() + (1.0f - y_) * area.getHeight();
    g.setColour(juce::Colour(0xffffc857));
    g.fillEllipse(juce::Rectangle<float>(16.0f, 16.0f).withCentre({dx, dy}));
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.drawEllipse(juce::Rectangle<float>(16.0f, 16.0f).withCentre({dx, dy}), 1.5f);
}

void XYPad::handleMouse(const juce::MouseEvent& e)
{
    const auto area = padArea();
    if (area.getWidth() <= 0.0f || area.getHeight() <= 0.0f)
        return;
    const float x = juce::jlimit(0.0f, 1.0f, (e.position.x - area.getX()) / area.getWidth());
    const float y = juce::jlimit(0.0f, 1.0f, 1.0f - (e.position.y - area.getY()) / area.getHeight());
    x_ = x;
    y_ = y;
    repaint();
    if (onDrag)
        onDrag(x, y);
}

void XYPad::mouseDown(const juce::MouseEvent& e)
{
    handleMouse(e);
}

void XYPad::mouseDrag(const juce::MouseEvent& e)
{
    handleMouse(e);
}

} // namespace aaom
