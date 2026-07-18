#include "CornerSlot.h"

namespace aaom
{

CornerSlot::CornerSlot(int index)
: index_(index)
{
    name_.setJustificationType(juce::Justification::centredLeft);
    name_.setFont(juce::Font(13.0f, juce::Font::bold));
    name_.setMinimumHorizontalScale(0.6f);
    addAndMakeVisible(name_);

    paste_.onClick = [this] {
        if (onPaste)
            onPaste(index_);
    };
    clear_.onClick = [this] {
        if (onClear)
            onClear(index_);
    };
    addAndMakeVisible(paste_);
    addAndMakeVisible(clear_);

    setContents(false, {});
}

void CornerSlot::setContents(bool assigned, const juce::String& name)
{
    assigned_ = assigned;
    name_.setText(assigned ? name : juce::String("(empty)"), juce::dontSendNotification);
    name_.setColour(juce::Label::textColourId,
                    assigned ? juce::Colours::white : juce::Colours::grey);
    clear_.setEnabled(assigned);
    repaint();
}

void CornerSlot::resized()
{
    auto r = getLocalBounds().reduced(6);
    name_.setBounds(r.removeFromTop(20));
    r.removeFromTop(2);
    auto row = r.removeFromTop(24);
    paste_.setBounds(row.removeFromLeft(row.getWidth() / 2).reduced(1));
    clear_.setBounds(row.reduced(1));
}

void CornerSlot::paint(juce::Graphics& g)
{
    g.setColour(juce::Colour(0xff262b33));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 5.0f);
    g.setColour(assigned_ ? juce::Colour(0xff5fb0ff) : juce::Colour(0xff3a4049));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.75f), 5.0f, 1.5f);
}

} // namespace aaom
