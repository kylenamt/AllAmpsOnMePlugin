#include "AmpSlotComponent.h"
#include "Palette.h"

#include <cmath>

namespace aaom
{

namespace
{
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

AmpSlotComponent::AmpSlotComponent(int index, bool alignRight)
: index_(index), alignRight_(alignRight), tag_(tagFor(index))
{
    name_.setJustificationType(alignRight_ ? juce::Justification::centredRight : juce::Justification::centredLeft);
    name_.setFont(juce::Font(12.0f, juce::Font::bold));
    name_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(name_);

    select_.setComponentID("select");
    select_.setColour(juce::TextButton::buttonColourId, palette::accentHardware);
    select_.setColour(juce::TextButton::textColourOffId, palette::textEngravedDeep);
    select_.onClick = [this] {
        if (onSelect)
            onSelect(index_);
    };
    addAndMakeVisible(select_);

    clear_.setComponentID("clear");
    clear_.setColour(juce::TextButton::buttonColourId, palette::clearTop);
    clear_.setColour(juce::TextButton::textColourOffId, palette::clearText);
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
    name_.setText(assigned ? name : juce::String("Empty Slot"), juce::dontSendNotification);
    name_.setColour(juce::Label::textColourId, assigned ? palette::textName : palette::textFaint);
    clear_.setEnabled(assigned);
    repaint();
}

void AmpSlotComponent::setWeight(float weight01)
{
    weight01 = juce::jlimit(0.0f, 1.0f, weight01);
    if (std::abs(weight01 - weight_) < 0.001f)
        return;
    weight_ = weight01;
    repaint();
}

void AmpSlotComponent::resized()
{
    auto r = getLocalBounds().reduced(10);

    r.removeFromTop(12); // corner tag, drawn in paint()
    r.removeFromTop(3);
    name_.setBounds(r.removeFromTop(28));
    r.removeFromTop(4);
    r.removeFromTop(9); // LED meter, drawn in paint()
    r.removeFromTop(4);
    r.removeFromTop(13); // percent readout, drawn in paint()
    r.removeFromTop(7);

    auto btnRow = r.removeFromTop(20);
    clear_.setBounds(btnRow.removeFromRight(38));
    btnRow.removeFromRight(5);
    select_.setBounds(btnRow);
}

void AmpSlotComponent::paint(juce::Graphics& g)
{
    using namespace palette;
    const auto bounds = getLocalBounds().toFloat();

    juce::ColourGradient bg(panelTop, bounds.getX(), bounds.getY(), panelBottom, bounds.getX(), bounds.getBottom(),
                            false);
    g.setGradientFill(bg);
    g.fillRoundedRectangle(bounds, 8.0f);
    g.setColour(inkBorder);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 8.0f, 1.0f);
    g.setColour(juce::Colours::white.withAlpha(0.07f));
    g.drawLine(bounds.getX() + 9.0f, bounds.getY() + 1.0f, bounds.getRight() - 9.0f, bounds.getY() + 1.0f, 1.0f);

    auto r = getLocalBounds().reduced(10);
    const auto just = alignRight_ ? juce::Justification::centredRight : juce::Justification::centredLeft;

    auto tagBounds = r.removeFromTop(12);
    r.removeFromTop(3 + 28 + 4);

    g.setColour(accentHardware);
    g.setFont(juce::Font(9.0f, juce::Font::bold).withExtraKerningFactor(0.18f));
    g.drawText(tag_, tagBounds, just);

    const auto meterBounds = r.removeFromTop(9).toFloat();
    r.removeFromTop(4);
    const auto pctBounds = r.removeFromTop(13);

    g.setColour(inkTrack);
    g.fillRoundedRectangle(meterBounds, 2.0f);
    const float fillW = meterBounds.getWidth() * weight_;
    if (fillW > 0.5f)
    {
        juce::ColourGradient fillGrad(accent.withAlpha(0.55f), meterBounds.getX(), meterBounds.getY(), accent,
                                      meterBounds.getX() + fillW, meterBounds.getY(), false);
        g.setGradientFill(fillGrad);
        g.fillRoundedRectangle(meterBounds.withWidth(fillW), 2.0f);
    }
    g.setColour(panelBottom);
    for (float sx = meterBounds.getX(); sx < meterBounds.getRight(); sx += 9.0f)
        g.fillRect(sx + 7.0f, meterBounds.getY(), 2.0f, meterBounds.getHeight());
    g.setColour(inkBorder);
    g.drawRoundedRectangle(meterBounds, 2.0f, 1.0f);

    const auto pctText = juce::String(juce::roundToInt(weight_ * 100.0f)) + "%";
    const auto pctFont = juce::Font(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain);
    g.setColour(lcdAmberText.withAlpha(0.35f));
    g.setFont(pctFont.withHeight(pctFont.getHeight() * 1.08f));
    g.drawText(pctText, pctBounds, just);
    g.setColour(lcdAmberText);
    g.setFont(pctFont);
    g.drawText(pctText, pctBounds, just);
}

void AmpSlotComponent::mouseDown(const juce::MouseEvent& e)
{
    if (!e.mods.isPopupMenu())
        return;

    juce::PopupMenu menu;
    menu.setLookAndFeel(&getLookAndFeel());
    menu.addItem(1, "Paste Profile From Clipboard");
    menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(this), [this](int result) {
        if (result == 1 && onPaste)
            onPaste(index_);
    });
}

} // namespace aaom
