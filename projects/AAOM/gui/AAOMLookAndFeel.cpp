#include "AAOMLookAndFeel.h"
#include "Palette.h"

namespace aaom
{

AAOMLookAndFeel::AAOMLookAndFeel()
{
    using namespace palette;

    setColour(juce::ResizableWindow::backgroundColourId, page);

    setColour(juce::Label::textColourId, textPrimary);

    setColour(juce::Slider::textBoxTextColourId, textValue);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);

    setColour(juce::TextButton::textColourOffId, textIcon);
    setColour(juce::TextButton::textColourOnId, textIcon);

    setColour(juce::ListBox::backgroundColourId, listBody);
    setColour(juce::ListBox::textColourId, textPrimary);
    setColour(juce::ListBox::outlineColourId, juce::Colours::transparentBlack);

    setColour(juce::ScrollBar::backgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::ScrollBar::thumbColourId, cyan.withAlpha(0.5f));

    setColour(juce::PopupMenu::backgroundColourId, shellLeadTop);
    setColour(juce::PopupMenu::textColourId, textPrimary);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, cyan.withAlpha(0.28f));
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);

    setColour(juce::TooltipWindow::backgroundColourId, shellLeadTop);
    setColour(juce::TooltipWindow::textColourId, textPrimary);
    setColour(juce::TooltipWindow::outlineColourId, juce::Colours::black);

    setColour(juce::CaretComponent::caretColourId, cyanLight);

    setColour(juce::TextEditor::textColourId, cyanLight);
    setColour(juce::TextEditor::highlightColourId, cyan.withAlpha(0.4f));
}

void AAOMLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                       float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider)
{
    using namespace palette;

    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
    const auto centre = bounds.getCentre();
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;

    // Ring: two annular pie segments (filled + dim remainder) reproduce the
    // design's conic-gradient(from -135deg, cyan 0 deg, rgba(255,255,255,.07)
    // deg 270deg, transparent 270deg). rotaryStartAngle/rotaryEndAngle are
    // used as-is (rather than hardcoded) so the ring always matches whatever
    // sweep the Slider is actually configured with; the editor pins every
    // knob's RotaryParameters to exactly -135deg..+135deg to match the design
    // (JUCE's own default sweep is close but not identical).
    const float innerProportion = juce::jlimit(0.0f, 0.95f, (radius - 6.0f) / radius);
    const float sweptAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const auto ringColour = slider.isEnabled() ? cyan : textDisabled3;

    if (sliderPos > 0.0f)
    {
        juce::Path fillArc;
        fillArc.addPieSegment(bounds, rotaryStartAngle, sweptAngle, innerProportion);
        g.setColour(ringColour);
        g.fillPath(fillArc);
    }
    if (sliderPos < 1.0f)
    {
        juce::Path restArc;
        restArc.addPieSegment(bounds, sweptAngle, rotaryEndAngle, innerProportion);
        g.setColour(juce::Colours::white.withAlpha(0.07f));
        g.fillPath(restArc);
    }

    // Cap: dark off-centre radial gradient, inset by the ring width.
    const float capRadius = radius * innerProportion;
    const juce::Point<float> focus(centre.x - capRadius * 0.28f, centre.y - capRadius * 0.44f);
    juce::ColourGradient cap(juce::Colour(0xff3c4247), focus.x, focus.y, juce::Colour(0xff171a1e), centre.x,
                             centre.y + capRadius, true);
    g.setGradientFill(cap);
    g.fillEllipse(juce::Rectangle<float>(capRadius * 2.0f, capRadius * 2.0f).withCentre(centre));
    g.setColour(juce::Colours::white.withAlpha(0.10f));
    g.drawEllipse(juce::Rectangle<float>(capRadius * 2.0f, capRadius * 2.0f).withCentre(centre), 1.0f);

    // Pointer: small glowing dot near the rim of the cap. Cheap glow: a
    // soft low-alpha pass behind the crisp dot (same trick used for the LCD
    // text glow and the morph-pad puck).
    const float pointerDist = juce::jmax(0.0f, capRadius - 5.5f);
    const juce::Point<float> pointerPos = centre.getPointOnCircumference(pointerDist, sweptAngle);
    const float dotR = 2.5f;
    g.setColour(ringColour.withAlpha(0.55f));
    g.fillEllipse(juce::Rectangle<float>(dotR * 3.2f, dotR * 3.2f).withCentre(pointerPos));
    g.setColour(ringColour);
    g.fillEllipse(juce::Rectangle<float>(dotR * 2.0f, dotR * 2.0f).withCentre(pointerPos));
}

void AAOMLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                       float minSliderPos, float maxSliderPos, const juce::Slider::SliderStyle style,
                                       juce::Slider& slider)
{
    using namespace palette;

    if (style != juce::Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style,
                                         slider);
        return;
    }

    const float trackH = 5.0f;
    const float cy = static_cast<float>(y) + static_cast<float>(height) * 0.5f;
    const juce::Rectangle<float> track(static_cast<float>(x), cy - trackH * 0.5f, static_cast<float>(width), trackH);
    g.setColour(trackBg);
    g.fillRoundedRectangle(track, trackH * 0.5f);

    const float fillW = juce::jlimit(0.0f, static_cast<float>(width), sliderPos - static_cast<float>(x));
    if (fillW > 0.5f)
    {
        juce::ColourGradient fillGrad(cyanMid2, track.getX(), track.getY(), cyanMid1, track.getX(), track.getBottom(),
                                      false);
        g.setGradientFill(fillGrad);
        g.fillRoundedRectangle(track.withWidth(fillW), trackH * 0.5f);
    }

    const float thumbD = 15.0f;
    const juce::Point<float> thumbCentre(sliderPos, cy);
    juce::ColourGradient thumb(juce::Colour(0xff4b5257), thumbCentre.x - thumbD * 0.12f, thumbCentre.y - thumbD * 0.2f,
                               juce::Colour(0xff202428), thumbCentre.x, thumbCentre.y, true);
    g.setGradientFill(thumb);
    g.fillEllipse(juce::Rectangle<float>(thumbD, thumbD).withCentre(thumbCentre));
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.drawEllipse(juce::Rectangle<float>(thumbD, thumbD).withCentre(thumbCentre), 1.0f);
}

void AAOMLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                           bool isHighlighted, bool isDown)
{
    using namespace palette;

    const auto bounds = button.getLocalBounds().toFloat();

    if (button.getComponentID() == "cabSwitch")
    {
        const bool on = button.getToggleState();
        const float radius = bounds.getHeight() * 0.5f;
        juce::ColourGradient track(on ? cyanMid1 : juce::Colour(0xff1a1d20), bounds.getX(), bounds.getY(),
                                   on ? cyan : juce::Colour(0xff22262a), bounds.getX(), bounds.getBottom(), false);
        g.setGradientFill(track);
        g.fillRoundedRectangle(bounds, radius);

        const float knobD = 15.0f;
        const float knobX = on ? bounds.getRight() - knobD - 2.0f : bounds.getX() + 2.0f;
        const juce::Rectangle<float> knob(knobX, bounds.getY() + 2.0f, knobD, knobD);
        juce::ColourGradient knobGrad(juce::Colour(0xfff2f6f7), knob.getX() + knobD * 0.38f, knob.getY() + knobD * 0.3f,
                                      juce::Colour(0xffc3ced0), knob.getCentreX(), knob.getCentreY(), true);
        g.setGradientFill(knobGrad);
        g.fillEllipse(knob);
        return;
    }

    // Small square icon buttons (search / clear / close) -- 0 radius by
    // design (the token table calls out small buttons and the modal as
    // deliberately square).
    g.setColour(smallButtonBg);
    g.fillRect(bounds);
    g.setColour(juce::Colours::white.withAlpha(isDown ? 0.16f : isHighlighted ? 0.13f : 0.09f));
    g.drawRect(bounds, 1.0f);
}

void AAOMLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    if (button.getComponentID() == "cabSwitch")
        return; // pill switch carries no label

    const auto bounds = button.getLocalBounds();
    const float size = juce::jmin(13.0f, static_cast<float>(bounds.getHeight()) * 0.55f);
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), size, juce::Font::plain));
    g.setColour(button.findColour(button.getToggleState() ? juce::TextButton::textColourOnId
                                                           : juce::TextButton::textColourOffId));
    g.drawText(button.getButtonText(), bounds, juce::Justification::centred);
}

void AAOMLookAndFeel::fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor&)
{
    g.setColour(palette::recessDeep);
    g.fillRect(0, 0, width, height);
}

void AAOMLookAndFeel::drawTextEditorOutline(juce::Graphics&, int, int, juce::TextEditor&)
{
    // No visible outline in the new design -- the recessed fill alone reads
    // as the field boundary.
}

} // namespace aaom
