#include "MorphPad.h"
#include "Palette.h"

#include <cmath>

namespace aaom
{

namespace
{
const juce::String kMinus(juce::CharPointer_UTF8("\xe2\x88\x92")); // U+2212 MINUS SIGN

juce::Font monoFont(float px)
{
    return juce::Font(juce::Font::getDefaultMonospacedFontName(), px, juce::Font::plain);
}

juce::String fmtAxis(float v)
{
    return (v < 0.0f ? kMinus : juce::String()) + juce::String(std::abs(v), 1);
}

// Weighted average of the 4 corner colors, weights clamped to >=0 first (a
// negative/extrapolated weight contributes nothing to the mix, matching the
// design's mixCorners()). Falls back to a neutral grey when everything is
// (numerically) zero, which only happens right at r's outer edge.
juce::Colour mixCorners(const CornerWeights& w)
{
    using namespace palette;
    const std::array<float, 4> weights{{w.bl, w.br, w.tl, w.tr}};
    const std::array<juce::Colour, 4> colours{{cornerBL, cornerBR, cornerTL, cornerTR}};

    float total = 0.0f;
    float r = 0.0f, g = 0.0f, b = 0.0f;
    for (int i = 0; i < 4; ++i)
    {
        const float v = juce::jmax(0.0f, weights[static_cast<std::size_t>(i)]);
        total += v;
        r += colours[static_cast<std::size_t>(i)].getFloatRed() * v;
        g += colours[static_cast<std::size_t>(i)].getFloatGreen() * v;
        b += colours[static_cast<std::size_t>(i)].getFloatBlue() * v;
    }
    if (total < 0.001f)
        return juce::Colour::fromFloatRGBA(200.0f / 255.0f, 210.0f / 255.0f, 212.0f / 255.0f, 1.0f);
    return juce::Colour::fromFloatRGBA(r / total, g / total, b / total, 1.0f);
}
} // namespace

CornerWeights computeCornerWeights(float x, float y)
{
    x = juce::jlimit(-1.0f, 2.0f, x);
    y = juce::jlimit(-1.0f, 2.0f, y);
    return {(1.0f - x) * (1.0f - y), x * (1.0f - y), (1.0f - x) * y, x * y};
}

MorphPad::MorphPad()
{
    setInterceptsMouseClicks(true, false);
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
}

void MorphPad::setRange(float r)
{
    r = juce::jlimit(0.10f, 1.0f, r);
    if (juce::approximatelyEqual(r, r_))
        return;
    r_ = r;
    rebuildMosaic();
    repaint();
}

void MorphPad::setDotPosition(float x, float y)
{
    x = juce::jlimit(-1.0f, 2.0f, x);
    y = juce::jlimit(-1.0f, 2.0f, y);
    if (juce::approximatelyEqual(x, x_) && juce::approximatelyEqual(y, y_))
        return;
    x_ = x;
    y_ = y;
    repaint();
}

void MorphPad::resized()
{
    rebuildMosaic();
}

juce::Point<float> MorphPad::fieldToLocal(float fx, float fy) const
{
    const auto b = getLocalBounds().toFloat();
    const float span = 1.0f + 2.0f * r_;
    fx = juce::jlimit(-r_, 1.0f + r_, fx);
    fy = juce::jlimit(-r_, 1.0f + r_, fy);
    const float left = ((fx + r_) / span) * b.getWidth();
    const float top = (((1.0f + r_) - fy) / span) * b.getHeight();
    return {b.getX() + left, b.getY() + top};
}

juce::Point<float> MorphPad::localToField(juce::Point<float> local) const
{
    const auto b = getLocalBounds().toFloat();
    if (b.getWidth() <= 0.0f || b.getHeight() <= 0.0f)
        return {x_, y_};

    const float span = 1.0f + 2.0f * r_;
    const auto snap = [](float v) { return std::round(v / 0.05f) * 0.05f; };

    float fx = -r_ + ((local.x - b.getX()) / b.getWidth()) * span;
    float fy = (1.0f + r_) - ((local.y - b.getY()) / b.getHeight()) * span;
    fx = juce::jlimit(-r_, 1.0f + r_, snap(fx));
    fy = juce::jlimit(-r_, 1.0f + r_, snap(fy));
    return {fx, fy};
}

void MorphPad::rebuildMosaic()
{
    mosaic_.clear();

    const auto b = getLocalBounds().toFloat();
    if (b.getWidth() <= 0.0f || b.getHeight() <= 0.0f)
        return;

    const float span = 1.0f + 2.0f * r_;
    const float step = (span / 0.05f > 44.0f) ? 0.1f : 0.05f;
    const int n = juce::jmax(1, juce::roundToInt(span / step));
    mosaic_.reserve(static_cast<std::size_t>(n * n));

    for (int iy = 0; iy < n; ++iy)
    {
        for (int ix = 0; ix < n; ++ix)
        {
            const float cx = -r_ + (static_cast<float>(ix) + 0.5f) * step;
            const float cy = (1.0f + r_) - (static_cast<float>(iy) + 0.5f) * step;
            const bool outside = cx < 0.0f || cx > 1.0f || cy < 0.0f || cy > 1.0f;

            const juce::Rectangle<float> rect(b.getX() + b.getWidth() * static_cast<float>(ix) / static_cast<float>(n),
                                              b.getY() + b.getHeight() * static_cast<float>(iy) / static_cast<float>(n),
                                              b.getWidth() / static_cast<float>(n), b.getHeight() / static_cast<float>(n));
            mosaic_.push_back({rect, mixCorners(computeCornerWeights(cx, cy)).withAlpha(outside ? 0.14f : 0.34f)});
        }
    }
}

void MorphPad::paint(juce::Graphics& g)
{
    using namespace palette;
    const auto b = getLocalBounds().toFloat();
    if (b.isEmpty())
        return;

    g.saveState();
    juce::Path clip;
    clip.addRoundedRectangle(b, 12.0f);
    g.reduceClipRegion(clip);

    g.setColour(recessDeep);
    g.fillRect(b);

    // Base grid: fine lines every 0.05 field units across the whole pad.
    const float span = 1.0f + 2.0f * r_;
    const float vStep = (0.05f / span) * b.getWidth();
    const float hStep = (0.05f / span) * b.getHeight();
    g.setColour(juce::Colours::white.withAlpha(0.05f));
    if (vStep > 0.5f)
        for (float vx = b.getX(); vx < b.getRight(); vx += vStep)
            g.drawVerticalLine(juce::roundToInt(vx), b.getY(), b.getBottom());
    if (hStep > 0.5f)
        for (float hy = b.getY(); hy < b.getBottom(); hy += hStep)
            g.drawHorizontalLine(juce::roundToInt(hy), b.getX(), b.getRight());

    // Color mosaic (cached -- only depends on r_ and bounds, not the drag).
    for (const auto& cell : mosaic_)
    {
        g.setColour(cell.colour);
        g.fillRect(cell.rect);
    }

    // Inner well: the [0,1] interpolation square, inset by r/span on each side.
    const juce::Point<float> wellTL = fieldToLocal(0.0f, 1.0f);
    const juce::Point<float> wellBR = fieldToLocal(1.0f, 0.0f);
    const juce::Rectangle<float> well(wellTL, wellBR);

    g.setColour(juce::Colours::white.withAlpha(0.22f));
    g.drawRect(well, 1.0f);

    // Inner 5% grid, closed off at the right/bottom edges.
    g.setColour(juce::Colours::white.withAlpha(0.10f));
    for (int i = 1; i < 20; ++i)
    {
        const float fx = well.getX() + well.getWidth() * (static_cast<float>(i) / 20.0f);
        const float fy = well.getY() + well.getHeight() * (static_cast<float>(i) / 20.0f);
        g.drawVerticalLine(juce::roundToInt(fx), well.getY(), well.getBottom());
        g.drawHorizontalLine(juce::roundToInt(fy), well.getX(), well.getRight());
    }
    g.drawVerticalLine(juce::roundToInt(well.getRight()) - 1, well.getY(), well.getBottom());
    g.drawHorizontalLine(juce::roundToInt(well.getBottom()) - 1, well.getX(), well.getRight());

    // Labels.
    const juce::String axisMin = fmtAxis(-r_);
    const juce::String axisMax = fmtAxis(1.0f + r_);

    g.setColour(textDim);
    g.setFont(monoFont(9.0f).withExtraKerningFactor(0.13f));
    g.drawText("MORPH FIELD " + juce::String(juce::CharPointer_UTF8("\xc2\xb7")) + " " + axisMin + " "
                  + juce::String(juce::CharPointer_UTF8("\xe2\x80\xa6")) + " " + axisMax,
              b.withTrimmedRight(12.0f).withTrimmedTop(11.0f).withHeight(11.0f), juce::Justification::topRight);

    g.setColour(textDim3);
    g.setFont(monoFont(8.5f));
    g.drawText("y " + axisMax, juce::Rectangle<float>(5.0f, 16.0f, 60.0f, 11.0f), juce::Justification::topLeft);
    g.drawText("1.0", juce::Rectangle<float>(5.0f, well.getY() + 5.0f, 40.0f, 11.0f), juce::Justification::topLeft);
    g.drawText("0.0", juce::Rectangle<float>(5.0f, well.getBottom() - 16.0f, 40.0f, 11.0f),
              juce::Justification::topLeft);
    g.drawText(axisMin, juce::Rectangle<float>(5.0f, b.getBottom() - 22.0f, 40.0f, 11.0f),
              juce::Justification::topLeft);
    g.drawText("x " + axisMin, juce::Rectangle<float>(5.0f, b.getBottom() - 15.0f, 60.0f, 11.0f),
              juce::Justification::bottomLeft);
    g.drawText("0", juce::Rectangle<float>(well.getX() - 20.0f, b.getBottom() - 15.0f, 40.0f, 11.0f),
              juce::Justification::centredBottom);
    g.drawText("1", juce::Rectangle<float>(well.getRight() - 20.0f, b.getBottom() - 15.0f, 40.0f, 11.0f),
              juce::Justification::centredBottom);
    g.drawText(axisMax, juce::Rectangle<float>(b.getRight() - 45.0f, b.getBottom() - 15.0f, 40.0f, 11.0f),
              juce::Justification::bottomRight);

    auto tagFont = juce::Font(juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::plain)
                      .withExtraKerningFactor(0.13f);
    g.setFont(tagFont);
    g.setColour(cornerTL);
    g.drawText("TL", juce::Rectangle<float>(well.getX() + 8.0f, well.getY() + 7.0f, 30.0f, 11.0f),
              juce::Justification::topLeft);
    g.setColour(cornerTR);
    g.drawText("TR", juce::Rectangle<float>(well.getRight() - 38.0f, well.getY() + 7.0f, 30.0f, 11.0f),
              juce::Justification::topRight);
    g.setColour(cornerBL);
    g.drawText("BL", juce::Rectangle<float>(well.getX() + 8.0f, well.getBottom() - 18.0f, 30.0f, 11.0f),
              juce::Justification::bottomLeft);
    g.setColour(cornerBR);
    g.drawText("BR", juce::Rectangle<float>(well.getRight() - 38.0f, well.getBottom() - 18.0f, 30.0f, 11.0f),
              juce::Justification::bottomRight);

    // Crosshair + puck, at the (possibly range-clamped) on-screen position.
    const juce::Point<float> puck = fieldToLocal(x_, y_);
    g.setColour(juce::Colours::white.withAlpha(0.16f));
    g.drawHorizontalLine(juce::roundToInt(puck.y), b.getX(), b.getRight());
    g.drawVerticalLine(juce::roundToInt(puck.x), b.getY(), b.getBottom());

    const juce::Colour mix = mixCorners(computeCornerWeights(x_, y_));
    const juce::Colour lift = mix.interpolatedWith(juce::Colours::white, 0.42f);
    const juce::Colour sink = mix.darker(1.0f); // ~mix * 0.5

    const auto square = [puck](float half) {
        return juce::Rectangle<float>(puck.x - half, puck.y - half, half * 2.0f, half * 2.0f);
    };
    g.setColour(mix.withAlpha(0.20f));
    g.fillRect(square(23.0f));
    g.setColour(mix.withAlpha(0.45f));
    g.fillRect(square(14.0f));
    g.setColour(recessDeep);
    g.fillRect(square(13.0f));

    juce::ColourGradient body(lift, puck.x, puck.y - 11.0f, sink, puck.x, puck.y + 11.0f, false);
    body.addColour(0.45, mix);
    g.setGradientFill(body);
    g.fillRect(square(11.0f));
    g.setColour(juce::Colours::white.withAlpha(0.55f));
    g.drawHorizontalLine(juce::roundToInt(puck.y - 10.5f), puck.x - 11.0f, puck.x + 11.0f);
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.drawHorizontalLine(juce::roundToInt(puck.y + 10.4f), puck.x - 11.0f, puck.x + 11.0f);
    g.setColour(recessDeep.withAlpha(0.5f));
    g.fillRect(puck.x - 3.0f, puck.y - 3.0f, 6.0f, 6.0f);

    // Readout chip below the puck.
    const juce::String xyText = juce::String(x_, 2) + " / " + juce::String(y_, 2);
    const auto readoutFont = monoFont(10.0f);
    const float textW = juce::GlyphArrangement::getStringWidth(readoutFont, xyText);
    const juce::Rectangle<float> chip(puck.x - (textW + 18.0f) * 0.5f, puck.y + 25.0f, textW + 18.0f, 19.0f);
    juce::ColourGradient chipGrad(recess, chip.getX(), chip.getY(), juce::Colour(0xff121518), chip.getX(),
                                  chip.getBottom(), false);
    g.setGradientFill(chipGrad);
    g.fillRoundedRectangle(chip, 6.0f);
    g.setColour(textSecondary);
    g.setFont(readoutFont);
    g.drawText(xyText, chip, juce::Justification::centred);

    g.restoreState();
}

void MorphPad::handleMouse(const juce::MouseEvent& e)
{
    const auto f = localToField(e.position);
    x_ = f.x;
    y_ = f.y;
    repaint();
    if (onDrag)
        onDrag(x_, y_);
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
