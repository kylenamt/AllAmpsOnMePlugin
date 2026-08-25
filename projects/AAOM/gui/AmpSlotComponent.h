#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace aaom
{

// One corner card in the right-hand column: corner tag (TL/TR/BL/BR), ⌕
// (open the profile library for this slot) / × (clear) icon buttons, weight
// percentage, profile name, and a magnitude weight bar. Right-click offers
// "Paste profile" (from the clipboard) -- no equivalent in the design mock,
// but preserved from the previous UI since it's a real, working feature
// (paste an aaom_profile JSON exported from the training pipeline).
class AmpSlotComponent : public juce::Component
{
public:
    explicit AmpSlotComponent(int index);

    std::function<void(int)> onSelect; // opens the Profile Library for this slot
    std::function<void(int)> onClear;
    std::function<void(int)> onPaste;

    void setContents(bool assigned, const juce::String& name);
    // Raw bilinear weight (unclamped -- can exceed +/-1 under extrapolation).
    void setWeight(float weight);
    // True when this corner has the highest weight of the 4, right now.
    void setLeading(bool leading);

    void resized() override;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    const int index_;
    const juce::String tag_;
    const juce::Colour tagColour_;

    bool assigned_ = false;
    bool leading_ = false;
    float weight_ = 0.0f;

    juce::Label name_;
    juce::TextButton search_;
    juce::TextButton clear_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmpSlotComponent)
};

} // namespace aaom
