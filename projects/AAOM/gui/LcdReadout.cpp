#include "LcdReadout.h"
#include "Palette.h"

namespace aaom
{

LcdReadout::LcdReadout()
: textColour_(palette::cyan)
{
    setInterceptsMouseClicks(false, false);
}

void LcdReadout::setText(const juce::String& text)
{
    if (text_ == text)
        return;
    text_ = text;
    repaint();
}

void LcdReadout::setTextColour(juce::Colour colour)
{
    textColour_ = colour;
    repaint();
}

void LcdReadout::mouseDown(const juce::MouseEvent&)
{
    if (onClick)
        onClick();
}

void LcdReadout::paint(juce::Graphics& g)
{
    using namespace palette;
    const auto bounds = getLocalBounds().toFloat();

    juce::ColourGradient bg(recess, bounds.getX(), bounds.getY(), recessBottom, bounds.getX(), bounds.getBottom(),
                            false);
    g.setGradientFill(bg);
    g.fillRoundedRectangle(bounds, 8.0f);
    g.setColour(juce::Colours::black.withAlpha(0.9f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 8.0f, 1.0f);

    const auto textArea = bounds.reduced(13.0f, 0.0f);
    // A scaled-up second pass (the "glow behind crisp text" trick used
    // elsewhere) drifts out of alignment over a string this long and reads as
    // ghosting, so this one draw call is deliberately the only pass.
    g.setColour(textColour_);
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
    g.drawText(text_, textArea, juce::Justification::centredLeft);
}

} // namespace aaom
