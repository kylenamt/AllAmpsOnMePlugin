#include "AmpSlotComponent.h"
#include "Palette.h"

#include <cmath>

namespace aaom
{

namespace
{
const juce::String kSearchGlyph(juce::CharPointer_UTF8("\xe2\x8c\x95")); // U+2315
const juce::String kClearGlyph(juce::CharPointer_UTF8("\xc3\x97"));      // U+00D7
const juce::String kMinus(juce::CharPointer_UTF8("\xe2\x88\x92"));       // U+2212

constexpr int kIconSize = 18;
constexpr int kIconGap = 6;
constexpr int kPctWidth = 44;

// Matches MorphEngine's corner weight order: 0=BL, 1=BR, 2=TL, 3=TR.
juce::String tagFor(int index)
{
    switch (index)
    {
        case 0: return "BL";
        case 1: return "BR";
        case 2: return "TL";
        default: return "TR";
    }
}
} // namespace

AmpSlotComponent::AmpSlotComponent(int index)
: index_(index), tag_(tagFor(index)), tagColour_(palette::cornerColour(index))
{
    name_.setJustificationType(juce::Justification::centredLeft);
    name_.setFont(juce::Font(12.5f, juce::Font::plain));
    name_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(name_);

    search_.setButtonText(kSearchGlyph);
    search_.setComponentID("icon");
    search_.setColour(juce::TextButton::textColourOffId, palette::textIcon);
    search_.setTooltip("Browse profiles");
    search_.onClick = [this] {
        if (onSelect)
            onSelect(index_);
    };
    addAndMakeVisible(search_);

    clear_.setButtonText(kClearGlyph);
    clear_.setComponentID("icon");
    clear_.setTooltip("Clear slot");
    clear_.onClick = [this] {
        if (onClear)
            onClear(index_);
    };
    addAndMakeVisible(clear_);

    setContents(false, {});
}

void AmpSlotComponent::setContents(bool assigned, const juce::String& name)
{
    assigned_ = assigned;
    name_.setText(assigned ? name : juce::String("Empty slot"), juce::dontSendNotification);
    name_.setColour(juce::Label::textColourId, assigned ? palette::textPrimary : palette::textDisabled2);
    clear_.setColour(juce::TextButton::textColourOffId, assigned ? palette::textIcon : palette::textDisabled3);
    repaint();
}

void AmpSlotComponent::setWeight(float weight)
{
    if (juce::approximatelyEqual(weight, weight_))
        return;
    weight_ = weight;
    repaint();
}

void AmpSlotComponent::setLeading(bool leading)
{
    if (leading == leading_)
        return;
    leading_ = leading;
    repaint();
}

void AmpSlotComponent::resized()
{
    auto r = getLocalBounds().reduced(13, 10);
    auto row1 = r.removeFromTop(kIconSize);
    r.removeFromTop(5);
    name_.setBounds(r.removeFromTop(16));

    row1.removeFromRight(kPctWidth);
    row1.removeFromRight(kIconGap);
    clear_.setBounds(row1.removeFromRight(kIconSize));
    row1.removeFromRight(kIconGap);
    search_.setBounds(row1.removeFromRight(kIconSize));
}

void AmpSlotComponent::paint(juce::Graphics& g)
{
    using namespace palette;
    const auto bounds = getLocalBounds().toFloat();

    if (assigned_)
    {
        juce::ColourGradient bg(leading_ ? shellLeadTop : shellLightTop, bounds.getX(), bounds.getY(),
                                leading_ ? shellLeadBottom : shellLightBottom, bounds.getX(), bounds.getBottom(),
                                false);
        g.setGradientFill(bg);
        g.fillRoundedRectangle(bounds, 10.0f);
        g.setColour(juce::Colours::white.withAlpha(0.06f));
        g.drawLine(bounds.getX() + 1.0f, bounds.getY() + 0.5f, bounds.getRight() - 1.0f, bounds.getY() + 0.5f, 1.0f);
        g.setColour(tagColour_.withAlpha(leading_ ? 0.45f : 0.16f));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 10.0f, 1.0f);
    }
    else
    {
        g.setColour(emptyCardBg);
        g.fillRoundedRectangle(bounds, 10.0f);
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 10.0f, 1.0f);
    }

    auto r = getLocalBounds().reduced(13, 10);
    auto row1 = r.removeFromTop(kIconSize);
    r.removeFromTop(5);
    r.removeFromTop(16); // name -- real Label component
    r.removeFromTop(7);
    const auto barBounds = r.removeFromTop(5).toFloat();

    const auto pctArea = row1.removeFromRight(kPctWidth);
    row1.removeFromRight(kIconGap);
    row1.removeFromRight(kIconSize); // clear_ -- real Component
    row1.removeFromRight(kIconGap);
    row1.removeFromRight(kIconSize); // search_ -- real Component
    // row1 is now the remaining tag area.

    g.setColour(assigned_ ? tagColour_ : textDisabled3);
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::plain)
                 .withExtraKerningFactor(0.13f));
    g.drawText(tag_, row1, juce::Justification::centredLeft);

    const bool neg = weight_ < -0.005f;
    const juce::String pctText =
        (neg ? kMinus : juce::String()) + juce::String(juce::roundToInt(std::abs(weight_) * 100.0f)) + "%";
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
    g.setColour(neg ? negativeText : leading_ ? tagColour_ : textValue);
    g.drawText(pctText, pctArea, juce::Justification::centredRight);

    // Row 3: magnitude weight bar (0..2 maps to 0..100% width), with a
    // segmenting overlay every 5px to read as a bank of LED cells.
    g.setColour(trackBg);
    g.fillRect(barBounds);
    const float mag = juce::jlimit(0.0f, 2.0f, std::abs(weight_));
    const float fillW = (mag / 2.0f) * barBounds.getWidth();
    if (fillW > 0.5f)
    {
        juce::ColourGradient fillGrad(neg ? negativeBarTop : tagColour_.withAlpha(0.95f), barBounds.getX(),
                                      barBounds.getY(), neg ? negativeBarBottom : tagColour_.withAlpha(0.55f),
                                      barBounds.getX(), barBounds.getBottom(), false);
        g.setGradientFill(fillGrad);
        g.fillRect(barBounds.withWidth(fillW));
    }
    g.setColour(trackBg);
    for (float sx = barBounds.getX(); sx < barBounds.getRight(); sx += 5.0f)
        g.fillRect(sx + 4.0f, barBounds.getY(), 1.0f, barBounds.getHeight());
}

void AmpSlotComponent::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu menu;
        menu.setLookAndFeel(&getLookAndFeel());
        menu.addItem(1, "Paste Profile From Clipboard");
        menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(this), [this](int result) {
            if (result == 1 && onPaste)
                onPaste(index_);
        });
        return;
    }

    if (onSelect)
        onSelect(index_);
}

} // namespace aaom
