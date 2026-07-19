#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace aaom
{

// One corner module of the morph chassis: corner tag (TL/TR/BL/BR), amp name,
// a segmented LED influence meter + percent readout, and SELECT / CLR
// buttons. Right-click offers "Paste profile" (from the clipboard), which
// has no equivalent button in the hardware mock but is preserved from the
// previous UI since it's a real, working feature (paste an aaom_profile JSON
// exported from the training pipeline).
class AmpSlotComponent : public juce::Component
{
public:
    // alignRight: right-column modules (TR/BR) mirror their text alignment.
    AmpSlotComponent(int index, bool alignRight);

    std::function<void(int)> onSelect; // opens the Profile Library for this slot
    std::function<void(int)> onClear;
    std::function<void(int)> onPaste;

    void setContents(bool assigned, const juce::String& name);
    void setWeight(float weight01); // live bilinear influence, updated every frame

    void resized() override;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    const int index_;
    const bool alignRight_;
    const juce::String tag_;

    bool assigned_ = false;
    float weight_ = 0.0f;

    juce::Label name_;
    juce::TextButton select_{"Select"};
    juce::TextButton clear_{"Clr"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmpSlotComponent)
};

} // namespace aaom
