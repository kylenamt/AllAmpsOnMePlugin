#include "AAOMLookAndFeel.h"
#include "Palette.h"

#include <cmath>

namespace aaom
{

AAOMLookAndFeel::AAOMLookAndFeel()
{
    using namespace palette;

    setColour(juce::ResizableWindow::backgroundColourId, deskMid);

    setColour(juce::Label::textColourId, textLight);

    setColour(juce::Slider::textBoxTextColourId, lcdAmberText);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);

    setColour(juce::TextButton::textColourOffId, textEngravedDeep);
    setColour(juce::TextButton::textColourOnId, textEngravedDeep);

    setColour(juce::ListBox::backgroundColourId, panelDeep);
    setColour(juce::ListBox::textColourId, textLight);
    setColour(juce::ListBox::outlineColourId, inkBorder);

    setColour(juce::ScrollBar::backgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::ScrollBar::thumbColourId, accentHardware.withAlpha(0.6f));

    setColour(juce::PopupMenu::backgroundColourId, panelTop);
    setColour(juce::PopupMenu::textColourId, textLight);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, accentHardware.withAlpha(0.3f));
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);

    setColour(juce::TooltipWindow::backgroundColourId, panelTop);
    setColour(juce::TooltipWindow::textColourId, textLight);
    setColour(juce::TooltipWindow::outlineColourId, inkBorder);

    setColour(juce::CaretComponent::caretColourId, lcdGreenText);
}

void AAOMLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                       float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider)
{
    using namespace palette;

    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(2.0f);
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto centre = bounds.getCentre();
    const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // Knob body: off-centre highlight radial gradient (matches the CSS
    // "radial-gradient(circle at 36% 28%, ...)" focus point).
    const juce::Point<float> focus(centre.x - radius * 0.28f, centre.y - radius * 0.44f);
    juce::ColourGradient body(juce::Colour(0xff6a6258), focus.x, focus.y, juce::Colour(0xff2a2620), centre.x,
                              centre.y + radius, true);
    g.setGradientFill(body);
    g.fillEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre));

    // Knurled inset disc: alternating wedges, inset from the rim so a thin
    // bezel of the body gradient shows through at the edge.
    const float ringRadius = radius - juce::jmax(2.5f, radius * 0.16f);
    const int numWedges = 30;
    for (int i = 0; i < numWedges; ++i)
    {
        const float a0 = (juce::MathConstants<float>::twoPi * i) / numWedges;
        const float a1 = (juce::MathConstants<float>::twoPi * (i + 1)) / numWedges;
        juce::Path wedge;
        wedge.addPieSegment(centre.x - ringRadius, centre.y - ringRadius, ringRadius * 2.0f, ringRadius * 2.0f, a0,
                            a1, 0.0f);
        g.setColour(i % 2 == 0 ? knurlLight : knurlDark);
        g.fillPath(wedge);
    }

    g.setColour(inkBezel);
    g.drawEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre), 1.4f);

    // Amber pointer with a soft glow (approximated as a wider, low-alpha copy
    // of the same shape behind the crisp one).
    juce::Path pointer;
    const float pointerLen = ringRadius * 0.92f;
    const float pointerThickness = juce::jmax(1.8f, radius * 0.09f);
    pointer.addRoundedRectangle(-pointerThickness * 0.5f, -pointerLen, pointerThickness, pointerLen * 0.55f,
                                pointerThickness * 0.4f);
    const auto pointerTransform = juce::AffineTransform::rotation(angle).translated(centre.x, centre.y);
    const auto colour = slider.isEnabled() ? accentHardware : textFaint;

    g.setColour(colour.withAlpha(0.35f));
    g.fillPath(pointer, juce::AffineTransform::scale(2.0f, 1.1f).followedBy(pointerTransform));
    g.setColour(colour);
    g.fillPath(pointer, pointerTransform);
}

void AAOMLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                       float minSliderPos, float maxSliderPos, const juce::Slider::SliderStyle style,
                                       juce::Slider& slider)
{
    using namespace palette;

    if (style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style,
                                         slider);
        return;
    }

    const float trackW = 5.0f;
    const float cx = static_cast<float>(x) + static_cast<float>(width) * 0.5f;
    const juce::Rectangle<float> track(cx - trackW * 0.5f, static_cast<float>(y), trackW, static_cast<float>(height));
    g.setColour(inkTrack);
    g.fillRoundedRectangle(track, trackW * 0.5f);
    g.setColour(inkBorder);
    g.drawRoundedRectangle(track, trackW * 0.5f, 1.0f);

    // Detent marks, one per parameter step. JUCE maps a vertical slider's value
    // linearly across [y, y + height] with the maximum at the top, so
    // valueToProportionOfLength gives the exact tick positions (and stays right
    // if the parameter is ever skewed). Ticks are thinned out when the steps are
    // finer than the pixels can separate, and the centre detent is emphasised.
    const double interval = slider.getInterval();
    const double span = slider.getMaximum() - slider.getMinimum();
    if (interval > 0.0 && span > 0.0)
    {
        const int numIntervals = juce::roundToInt(span / interval);
        if (numIntervals > 0 && numIntervals <= 512)
        {
            constexpr float minSpacing = 7.0f;
            int stride = 1;
            while (stride < numIntervals
                   && static_cast<float>(height) * static_cast<float>(stride)
                          / static_cast<float>(numIntervals) < minSpacing)
                ++stride;

            const float outer = juce::jmin(9.0f, static_cast<float>(width) * 0.5f);
            const float inner = trackW * 0.5f + 2.0f;
            if (outer > inner)
            {
                for (int i = 0; i <= numIntervals; i += stride)
                {
                    const double value = slider.getMinimum() + interval * i;
                    const auto prop = static_cast<float>(slider.valueToProportionOfLength(value));
                    const float ty = static_cast<float>(y) + (1.0f - prop) * static_cast<float>(height);

                    const bool isCentre = std::abs(prop - 0.5f) < 1.0e-3f;
                    g.setColour(isCentre ? accentHardware.withAlpha(0.55f) : textFaint.withAlpha(0.45f));
                    const float len = isCentre ? outer - inner : (outer - inner) * 0.7f;
                    g.fillRect(cx - inner - len, ty - 0.5f, len, 1.0f);
                    g.fillRect(cx + inner, ty - 0.5f, len, 1.0f);
                }
            }
        }
    }

    const float capH = 14.0f;
    const float capW = juce::jmax(18.0f, static_cast<float>(width) - 2.0f);
    const juce::Rectangle<float> cap(cx - capW * 0.5f, sliderPos - capH * 0.5f, capW, capH);

    juce::ColourGradient capGrad(juce::Colour(0xff5a544b), cap.getX(), cap.getY(), juce::Colour(0xff2a2620),
                                 cap.getX(), cap.getBottom(), false);
    g.setGradientFill(capGrad);
    g.fillRoundedRectangle(cap, 3.0f);
    g.setColour(inkBorder);
    g.drawRoundedRectangle(cap, 3.0f, 1.0f);

    g.setColour(slider.isEnabled() ? accentHardware : textFaint);
    g.fillRect(cap.getX() + 2.0f, cap.getCentreY() - 1.0f, cap.getWidth() - 4.0f, 2.0f);
}

void AAOMLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                           const juce::Colour& backgroundColour, bool isHighlighted, bool isDown)
{
    using namespace palette;

    auto bounds = button.getLocalBounds().toFloat();
    float radius = 4.0f;
    if (button.getComponentID() == "pill")
        radius = bounds.getHeight() * 0.5f;
    else if (button.getComponentID() == "close")
        radius = 6.0f;

    if (isDown)
        bounds = bounds.translated(0.0f, 1.0f);

    juce::ColourGradient grad(backgroundColour.brighter(0.3f), bounds.getX(), bounds.getY(),
                             backgroundColour.darker(0.2f), bounds.getX(), bounds.getBottom(), false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(bounds, radius);

    if (isDown)
    {
        g.setColour(juce::Colours::black.withAlpha(0.3f));
        g.fillRoundedRectangle(bounds, radius);
    }
    else if (isHighlighted)
    {
        g.setColour(juce::Colours::white.withAlpha(0.07f));
        g.fillRoundedRectangle(bounds, radius);
    }

    g.setColour(inkBezel);
    g.drawRoundedRectangle(bounds.reduced(0.5f), radius, 1.0f);
}

void AAOMLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool isDown)
{
    auto font = juce::Font(11.0f, juce::Font::bold);
    font.setExtraKerningFactor(0.09f);
    font.setHorizontalScale(0.92f);
    g.setFont(font);
    g.setColour(button.findColour(button.getToggleState() ? juce::TextButton::textColourOnId
                                                           : juce::TextButton::textColourOffId));
    auto bounds = button.getLocalBounds();
    if (isDown)
        bounds = bounds.translated(0, 1);
    g.drawText(button.getButtonText().toUpperCase(), bounds, juce::Justification::centred);
}

void AAOMLookAndFeel::fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    using namespace palette;
    juce::ignoreUnused(editor);
    juce::ColourGradient grad(lcdBgTop, 0.0f, 0.0f, inkLcdBottom, 0.0f, static_cast<float>(height), false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
                           6.0f);
}

void AAOMLookAndFeel::drawTextEditorOutline(juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    juce::ignoreUnused(editor);
    g.setColour(palette::inkBezel);
    g.drawRoundedRectangle(
        juce::Rectangle<float>(0.5f, 0.5f, static_cast<float>(width) - 1.0f, static_cast<float>(height) - 1.0f),
        6.0f, 1.0f);
}

} // namespace aaom
