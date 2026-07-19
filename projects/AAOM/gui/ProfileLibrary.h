#pragma once

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

namespace aaom
{

class AAOMProcessor;

// Full-editor overlay: dim scrim + a searchable, virtualized list of the
// profile catalogue. Opened targeting one corner; picking a row assigns that
// profile to the target and closes. No category chips: the real catalogue
// (405 free-text profile names) has no brand/model/genre metadata to filter
// by, unlike the design mock's fictional demo catalogue.
class ProfileLibrary : public juce::Component,
                       private juce::ListBoxModel,
                       private juce::TextEditor::Listener
{
public:
    explicit ProfileLibrary(AAOMProcessor& processor);
    ~ProfileLibrary() override;

    std::function<void(int corner, int profileIndex)> onPick;
    std::function<void()> onClose;

    void open(int targetCorner);
    void close();

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics&, int width, int height, bool rowIsSelected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent&) override;

    void textEditorTextChanged(juce::TextEditor&) override;

    void rebuildFilter();

    AAOMProcessor& proc_;
    int target_ = -1;
    std::vector<int> filtered_; // catalog indices matching the current query

    juce::Rectangle<float> dialogBounds_;

    juce::Label title_;
    juce::Label subtitle_;
    juce::TextButton close_{"X"};
    juce::TextEditor search_;
    juce::ListBox list_{"profiles", nullptr};
    juce::Label shownCount_;
    juce::Label totalCount_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProfileLibrary)
};

} // namespace aaom
