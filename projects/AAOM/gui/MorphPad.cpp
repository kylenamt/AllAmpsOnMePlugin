#include "MorphPad.h"
#include "Palette.h"

#include <cmath>

namespace aaom
{

CornerWeights computeCornerWeights(float x, float y)
{
    x = juce::jlimit(0.0f, 1.0f, x);
    y = juce::jlimit(0.0f, 1.0f, y);
    return {(1.0f - x) * (1.0f - y), x * (1.0f - y), (1.0f - x) * y, x * y};
}

MorphPad::MorphPad()
{
    setInterceptsMouseClicks(true, false);
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
}

void MorphPad::setDotPosition(float x, float y)
{
    x_ = juce::jlimit(0.0f, 1.0f, x);
    y_ = juce::jlimit(0.0f, 1.0f, y);
    repaint();
}

void MorphPad::setCornerAssigned(const std::array<bool, 4>& assigned)
{
    if (assigned != assigned_)
    {
        assigned_ = assigned;
        repaint();
    }
}

juce::Rectangle<float> MorphPad::screenArea() const
{
    return getLocalBounds().toFloat().reduced(9.0f);
}

void MorphPad::paint(juce::Graphics& g)
{
    using namespace palette;
    const auto outer = getLocalBounds().toFloat();
    const auto screen = screenArea();

    juce::ColourGradient bezelGrad(crtBezelTop, outer.getX(), outer.getY(), crtBezelBottom, outer.getX(),
                                   outer.getBottom(), false);
    g.setGradientFill(bezelGrad);
    g.fillRoundedRectangle(outer, 12.0f);

    g.saveState();
    juce::Path clip;
    clip.addRoundedRectangle(screen, 7.0f);
    g.reduceClipRegion(clip);

    juce::ColourGradient screenGrad(crtScreenCentre, screen.getCentreX(), screen.getCentreY(), crtScreenEdge,
                                    screen.getX(), screen.getY(), true);
    g.setGradientFill(screenGrad);
    g.fillRect(screen);

    // Corner glows: brighter corner = more morph influence there. Approximated
    // with normal alpha blending (JUCE has no additive/"screen" blend mode).
    const CornerWeights w = computeCornerWeights(x_, y_);
    const std::array<juce::Point<float>, 4> corners{
        {{screen.getX(), screen.getBottom()}, {screen.getRight(), screen.getBottom()}, {screen.getX(), screen.getY()},
         {screen.getRight(), screen.getY()}}};
    const std::array<float, 4> weights{{w.bl, w.br, w.tl, w.tr}};
    const float glowRadius = juce::jmax(screen.getWidth(), screen.getHeight()) * 0.62f;
    for (int i = 0; i < 4; ++i)
    {
        const float opacity = 0.10f + weights[static_cast<std::size_t>(i)] * 0.9f;
        const auto c = corners[static_cast<std::size_t>(i)];
        juce::ColourGradient glow(accent.withAlpha(0.85f * opacity), c.x, c.y, accent.withAlpha(0.0f),
                                  c.x + glowRadius, c.y, true);
        glow.addColour(0.35, accent.withAlpha(0.25f * opacity));
        g.setGradientFill(glow);
        g.fillRect(screen);
    }

    // Phosphor grid: fine 1% lines + coarse 10% lines.
    g.setColour(accentHardware.withAlpha(0.045f));
    for (int i = 1; i < 100; ++i)
    {
        const float fx = screen.getX() + screen.getWidth() * (static_cast<float>(i) / 100.0f);
        const float fy = screen.getY() + screen.getHeight() * (static_cast<float>(i) / 100.0f);
        g.drawVerticalLine(juce::roundToInt(fx), screen.getY(), screen.getBottom());
        g.drawHorizontalLine(juce::roundToInt(fy), screen.getX(), screen.getRight());
    }
    g.setColour(accentHardware.withAlpha(0.13f));
    for (int i = 1; i < 10; ++i)
    {
        const float fx = screen.getX() + screen.getWidth() * (static_cast<float>(i) / 10.0f);
        const float fy = screen.getY() + screen.getHeight() * (static_cast<float>(i) / 10.0f);
        g.drawVerticalLine(juce::roundToInt(fx), screen.getY(), screen.getBottom());
        g.drawHorizontalLine(juce::roundToInt(fy), screen.getX(), screen.getRight());
    }

    // Scanlines.
    g.setColour(juce::Colours::black.withAlpha(0.18f));
    for (float sy = screen.getY(); sy < screen.getBottom(); sy += 3.0f)
        g.drawHorizontalLine(juce::roundToInt(sy), screen.getX(), screen.getRight());

    // Puck: knurled ring + glowing amber core.
    const float px = screen.getX() + x_ * screen.getWidth();
    const float py = screen.getY() + (1.0f - y_) * screen.getHeight();
    const float puckR = 15.0f;

    g.setColour(accentHardware.withAlpha(0.22f));
    g.fillEllipse(juce::Rectangle<float>((puckR + 5.0f) * 2.0f, (puckR + 5.0f) * 2.0f).withCentre({px, py}));

    const int numWedges = 22;
    for (int i = 0; i < numWedges; ++i)
    {
        const float a0 = (juce::MathConstants<float>::twoPi * i) / numWedges;
        const float a1 = (juce::MathConstants<float>::twoPi * (i + 1)) / numWedges;
        juce::Path wedge;
        wedge.addPieSegment(px - puckR, py - puckR, puckR * 2.0f, puckR * 2.0f, a0, a1, 0.0f);
        g.setColour(i % 2 == 0 ? knurlLight : knurlDark);
        g.fillPath(wedge);
    }
    g.setColour(knurlRim);
    g.drawEllipse(juce::Rectangle<float>(puckR * 2.0f, puckR * 2.0f).withCentre({px, py}), 2.0f);

    const float coreR = 6.5f;
    juce::ColourGradient core(puckCoreHi, px - coreR * 0.24f, py - coreR * 0.36f, puckCoreEdge, px, py + coreR, true);
    core.addColour(0.55, accent);
    g.setGradientFill(core);
    g.fillEllipse(juce::Rectangle<float>(coreR * 2.0f, coreR * 2.0f).withCentre({px, py}));
    g.setColour(juce::Colours::white.withAlpha(0.55f));
    g.drawEllipse(juce::Rectangle<float>(coreR * 2.0f, coreR * 2.0f).withCentre({px, py}), 1.0f);

    g.restoreState();

    g.setColour(inkBezel);
    g.drawRoundedRectangle(outer.reduced(0.5f), 12.0f, 1.0f);
}

void MorphPad::handleMouse(const juce::MouseEvent& e)
{
    const auto area = screenArea();
    if (area.getWidth() <= 0.0f || area.getHeight() <= 0.0f)
        return;
    float x = (e.position.x - area.getX()) / area.getWidth();
    float y = 1.0f - (e.position.y - area.getY()) / area.getHeight();
    x = juce::jlimit(0.0f, 1.0f, x);
    y = juce::jlimit(0.0f, 1.0f, y);
    // Snap to 1% steps, matching the design prototype.
    x = std::round(x * 100.0f) / 100.0f;
    y = std::round(y * 100.0f) / 100.0f;
    x_ = x;
    y_ = y;
    repaint();
    if (onDrag)
        onDrag(x, y);
}

void MorphPad::mouseDown(const juce::MouseEvent& e)
{
    handleMouse(e);
}

void MorphPad::mouseDrag(const juce::MouseEvent& e)
{
    handleMouse(e);
}

} // namespace aaom
