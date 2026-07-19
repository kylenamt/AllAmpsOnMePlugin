#include "LcdReadout.h"
#include "Palette.h"

namespace aaom
{

LcdReadout::LcdReadout()
: textColour_(palette::lcdAmberText)
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

void LcdReadout::setCaretShown(bool shown)
{
    caret_ = shown;
    repaint();
}

void LcdReadout::paint(juce::Graphics& g)
{
    using namespace palette;
    const auto bounds = getLocalBounds().toFloat();

    juce::ColourGradient bg(lcdBgTop, bounds.getX(), bounds.getY(), inkLcdBottom, bounds.getX(), bounds.getBottom(),
                            false);
    g.setGradientFill(bg);
    g.fillRoundedRectangle(bounds, 5.0f);
    g.setColour(inkBezel);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 5.0f, 1.0f);

    auto textArea = bounds.reduced(8.0f, 0.0f);
    if (caret_)
    {
        const float s = 5.0f;
        juce::Path tri;
        auto c = juce::Point<float>(textArea.getX() + s * 0.5f, bounds.getCentreY());
        tri.addTriangle(c.x - s * 0.5f, c.y - s * 0.4f, c.x + s * 0.5f, c.y - s * 0.4f, c.x, c.y + s * 0.5f);
        g.setColour(textColour_);
        g.fillPath(tri);
        textArea.removeFromLeft(s + 6.0f);
    }

    const auto font = juce::Font(juce::Font::getDefaultMonospacedFontName(), bounds.getHeight() * 0.52f,
                                 juce::Font::plain);

    // Cheap glow: a soft low-alpha pass behind the crisp text.
    g.setColour(textColour_.withAlpha(0.35f));
    g.setFont(font.withHeight(font.getHeight() * 1.06f));
    g.drawText(text_, textArea, juce::Justification::centredLeft);

    g.setColour(textColour_);
    g.setFont(font);
    g.drawText(text_, textArea, juce::Justification::centredLeft);
}

} // namespace aaom
