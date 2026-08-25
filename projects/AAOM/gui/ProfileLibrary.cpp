#include "ProfileLibrary.h"
#include "Palette.h"

#include "AAOMProcessor.h"

namespace aaom
{

namespace
{
const juce::String kSearchGlyph(juce::CharPointer_UTF8("\xe2\x8c\x95")); // U+2315
const juce::String kCloseGlyph(juce::CharPointer_UTF8("\xc3\x97"));      // U+00D7

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

juce::String labelFor(int index)
{
    // Middot-separated, matching the design handoff's CORNER_LABEL strings.
    static const juce::String dot(juce::CharPointer_UTF8("\xc2\xb7"));
    switch (index)
    {
        case 0: return "BOTTOM " + dot + " LEFT";
        case 1: return "BOTTOM " + dot + " RIGHT";
        case 2: return "TOP " + dot + " LEFT";
        default: return "TOP " + dot + " RIGHT";
    }
}
} // namespace

ProfileLibrary::ProfileLibrary(AAOMProcessor& processor)
: proc_(processor)
{
    using namespace palette;

    setAlwaysOnTop(true);
    setVisible(false);
    setWantsKeyboardFocus(true);

    title_.setText("PROFILE LIBRARY", juce::dontSendNotification);
    title_.setFont(juce::Font(14.0f, juce::Font::plain).withExtraKerningFactor(0.14f));
    title_.setColour(juce::Label::textColourId, textPrimary);
    addAndMakeVisible(title_);

    subtitle_.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 9.5f, juce::Font::plain)
                          .withExtraKerningFactor(0.13f));
    subtitle_.setColour(juce::Label::textColourId, cyan);
    addAndMakeVisible(subtitle_);

    close_.setButtonText(kCloseGlyph);
    close_.setComponentID("icon");
    close_.setColour(juce::TextButton::textColourOffId, textSecondary);
    close_.onClick = [this] { close(); };
    addAndMakeVisible(close_);

    search_.setMultiLine(false);
    search_.setEscapeAndReturnKeysConsumed(false);
    search_.setTextToShowWhenEmpty("SEARCH AMPS...", textMuted);
    search_.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 12.5f, juce::Font::plain));
    search_.setIndents(22, 0);
    search_.addListener(this);
    addAndMakeVisible(search_);

    // Added after search_ so it paints on top of the field's own opaque
    // background; setIndents() above reserves the room for it.
    searchIcon_.setText(kSearchGlyph, juce::dontSendNotification);
    searchIcon_.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
    searchIcon_.setColour(juce::Label::textColourId, cyan);
    searchIcon_.setJustificationType(juce::Justification::centredLeft);
    searchIcon_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(searchIcon_);

    list_.setModel(this);
    list_.setRowHeight(46);
    addAndMakeVisible(list_);

    shownCount_.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 9.5f, juce::Font::plain)
                            .withExtraKerningFactor(0.12f));
    shownCount_.setColour(juce::Label::textColourId, textMuted);
    addAndMakeVisible(shownCount_);

    totalCount_.setFont(shownCount_.getFont());
    totalCount_.setColour(juce::Label::textColourId, textMuted);
    totalCount_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(totalCount_);
}

ProfileLibrary::~ProfileLibrary()
{
    search_.removeListener(this);
}

void ProfileLibrary::open(int targetCorner)
{
    target_ = targetCorner;

    const juce::String label = (targetCorner >= 0 && targetCorner < 4) ? labelFor(targetCorner) : juce::String();
    subtitle_.setText(juce::String("LOADING TO ") + label, juce::dontSendNotification);

    search_.clear();
    rebuildFilter();

    setVisible(true);
    toFront(true);
    search_.grabKeyboardFocus();
}

void ProfileLibrary::close()
{
    setVisible(false);
    if (onClose)
        onClose();
}

void ProfileLibrary::rebuildFilter()
{
    filtered_.clear();
    const auto query = search_.getText().trim().toLowerCase();
    const int n = proc_.numCatalogProfiles();
    for (int i = 0; i < n; ++i)
    {
        if (query.isNotEmpty() && !proc_.catalogProfileName(i).toLowerCase().contains(query))
            continue;
        filtered_.push_back(i);
    }
    list_.updateContent();
    list_.repaint();

    shownCount_.setText(juce::String(filtered_.size()) + " SHOWN", juce::dontSendNotification);
    totalCount_.setText(juce::String(n) + " PROFILES", juce::dontSendNotification);
}

void ProfileLibrary::textEditorTextChanged(juce::TextEditor&)
{
    rebuildFilter();
}

int ProfileLibrary::getNumRows()
{
    return static_cast<int>(filtered_.size());
}

void ProfileLibrary::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool)
{
    using namespace palette;
    if (rowNumber < 0 || rowNumber >= static_cast<int>(filtered_.size()))
        return;

    const int idx = filtered_[static_cast<std::size_t>(rowNumber)];
    const auto name = proc_.catalogProfileName(idx);
    const bool isCurrent = target_ >= 0 && proc_.corner(target_).assigned && proc_.corner(target_).name == name;

    const auto bounds = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height))
                            .reduced(3.0f, 3.0f);

    if (isCurrent)
    {
        g.setColour(cyan.withAlpha(0.14f));
        g.fillRect(bounds);
        g.setColour(cyan.withAlpha(0.5f));
        g.drawRect(bounds, 1.0f);
    }
    else
    {
        g.setColour(listRowBg);
        g.fillRect(bounds);
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.drawRect(bounds, 1.0f);
    }

    juce::String badge;
    for (int c = 0; c < 4; ++c)
    {
        if (proc_.corner(c).assigned && proc_.corner(c).name == name)
        {
            badge = tagFor(c);
            break;
        }
    }

    auto textArea = bounds.reduced(13.0f, 6.0f);
    auto nameArea = textArea;
    if (badge.isNotEmpty())
        nameArea.removeFromRight(34.0f);

    g.setColour(isCurrent ? cyanLight : textPrimary);
    g.setFont(juce::Font(12.5f, juce::Font::plain));
    g.drawText(name, nameArea.toNearestInt(), juce::Justification::centredLeft, true);

    if (badge.isNotEmpty())
    {
        g.setColour(cyan);
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::plain)
                     .withExtraKerningFactor(0.13f));
        g.drawText(badge, textArea.toNearestInt(), juce::Justification::centredRight, false);
    }
}

void ProfileLibrary::listBoxItemClicked(int row, const juce::MouseEvent&)
{
    if (row < 0 || row >= static_cast<int>(filtered_.size()) || target_ < 0)
        return;
    const int idx = filtered_[static_cast<std::size_t>(row)];
    if (onPick)
        onPick(target_, idx);
    close();
}

void ProfileLibrary::mouseDown(const juce::MouseEvent& e)
{
    if (!dialogBounds_.contains(e.position))
        close();
}

bool ProfileLibrary::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        close();
        return true;
    }
    return false;
}

void ProfileLibrary::resized()
{
    const auto full = getLocalBounds();
    const int dialogW = juce::jmin(560, static_cast<int>(static_cast<float>(full.getWidth()) * 0.92f));
    const int dialogH = juce::jmin(560, static_cast<int>(static_cast<float>(full.getHeight()) * 0.9f));
    const auto dialog = juce::Rectangle<int>(0, 0, dialogW, dialogH).withCentre(full.getCentre());
    dialogBounds_ = dialog.toFloat();

    auto r = dialog;
    auto header = r.removeFromTop(64).reduced(16, 14);
    close_.setBounds(header.removeFromRight(26).withSizeKeepingCentre(26, 26));
    header.removeFromRight(8);
    title_.setBounds(header.removeFromTop(18));
    subtitle_.setBounds(header);

    auto searchBlock = r.removeFromTop(58).reduced(16, 12);
    search_.setBounds(searchBlock);
    searchIcon_.setBounds(searchBlock.withTrimmedLeft(11).withWidth(20));

    auto footer = r.removeFromBottom(37).reduced(16, 0);
    shownCount_.setBounds(footer.removeFromLeft(footer.getWidth() / 2));
    totalCount_.setBounds(footer);

    list_.setBounds(r.reduced(0, 4));
}

void ProfileLibrary::paint(juce::Graphics& g)
{
    using namespace palette;

    g.fillAll(scrim);
    if (dialogBounds_.isEmpty())
        return;

    const auto d = dialogBounds_;
    g.setColour(shell);
    g.fillRect(d);

    const auto headerRect = d.withHeight(64.0f);
    juce::ColourGradient headerGrad(shellLeadTop, headerRect.getX(), headerRect.getY(), shellLeadBottom,
                                    headerRect.getX(), headerRect.getBottom(), false);
    g.setGradientFill(headerGrad);
    g.fillRect(headerRect);
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.drawHorizontalLine(juce::roundToInt(headerRect.getBottom()), d.getX(), d.getRight());

    const auto searchRect = juce::Rectangle<float>(d.getX(), headerRect.getBottom(), d.getWidth(), 58.0f);
    g.setColour(searchBarBg);
    g.fillRect(searchRect);
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.drawHorizontalLine(juce::roundToInt(searchRect.getBottom()), d.getX(), d.getRight());

    const auto footerRect = juce::Rectangle<float>(d.getX(), d.getBottom() - 37.0f, d.getWidth(), 37.0f);
    g.setColour(listBody);
    g.fillRect(juce::Rectangle<float>(d.getX(), searchRect.getBottom(), d.getWidth(),
                                      footerRect.getY() - searchRect.getBottom()));

    juce::ColourGradient footerGrad(shellLightTop, footerRect.getX(), footerRect.getY(), shellLightBottom,
                                    footerRect.getX(), footerRect.getBottom(), false);
    g.setGradientFill(footerGrad);
    g.fillRect(footerRect);
    g.setColour(juce::Colours::white.withAlpha(0.06f));
    g.drawHorizontalLine(juce::roundToInt(footerRect.getY()), d.getX(), d.getRight());

    g.setColour(juce::Colours::black.withAlpha(0.7f));
    g.drawRect(d, 1.0f);
}

} // namespace aaom
