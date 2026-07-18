#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace aaom
{

// A single XY-pad corner slot: shows the assigned profile name and offers
// Paste / Clear. Callbacks report the corner index back to the editor.
class CornerSlot : public juce::Component
{
public:
    explicit CornerSlot(int index);

    std::function<void(int)> onPaste;
    std::function<void(int)> onClear;

    void setContents(bool assigned, const juce::String& name);

    void resized() override;
    void paint(juce::Graphics&) override;

private:
    const int index_;
    bool assigned_ = false;
    juce::Label name_;
    juce::TextButton paste_{"Paste"};
    juce::TextButton clear_{"Clear"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CornerSlot)
};

} // namespace aaom
