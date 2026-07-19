#include "ProfileLibrary.h"
#include "Palette.h"

#include "AAOMProcessor.h"

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

ProfileLibrary::ProfileLibrary(AAOMProcessor& processor)
: proc_(processor)
{
    setAlwaysOnTop(true);
    setVisible(false);
    setWantsKeyboardFocus(true);

    title_.setText("PROFILE LIBRARY", juce::dontSendNotification);
    title_.setFont(juce::Font(16.0f, juce::Font::bold).withExtraKerningFactor(0.04f));
    title_.setColour(juce::Label::textColourId, palette::textEngraved);
    addAndMakeVisible(title_);

    subtitle_.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::plain)
                          .withExtraKerningFactor(0.1f));
    subtitle_.setColour(juce::Label::textColourId, palette::accentHardware);
    addAndMakeVisible(subtitle_);

    close_.setComponentID("close");
    close_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a352e));
    close_.setColour(juce::TextButton::textColourOffId, palette::textLight);
    close_.onClick = [this] { close(); };
    addAndMakeVisible(close_);

    search_.setMultiLine(false);
    search_.setEscapeAndReturnKeysConsumed(false);
    search_.setTextToShowWhenEmpty("SEARCH AMPS...", palette::textFaint);
    search_.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
    search_.setColour(juce::TextEditor::textColourId, palette::lcdGreenText);
    search_.setColour(juce::TextEditor::highlightColourId, palette::accentHardware.withAlpha(0.4f));
    search_.setIndents(22, 0);
    search_.addListener(this);
    addAndMakeVisible(search_);

    list_.setModel(this);
    list_.setRowHeight(46);
    addAndMakeVisible(list_);

    shownCount_.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::plain));
    shownCount_.setColour(juce::Label::textColourId, palette::textDim);
    addAndMakeVisible(shownCount_);

    totalCount_.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::plain));
    totalCount_.setColour(juce::Label::textColourId, palette::textDim);
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

    static const char* const labels[4] = {"BOTTOM - LEFT", "BOTTOM - RIGHT", "TOP - LEFT", "TOP - RIGHT"};
    const juce::String label = (targetCorner >= 0 && targetCorner < 4) ? labels[targetCorner] : juce::String();
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

void ProfileLibrary::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    using namespace palette;
    if (rowNumber < 0 || rowNumber >= static_cast<int>(filtered_.size()))
        return;

    const int idx = filtered_[static_cast<std::size_t>(rowNumber)];
    const auto name = proc_.catalogProfileName(idx);
    const bool isCurrent = target_ >= 0 && proc_.corner(target_).assigned && proc_.corner(target_).name == name;

    const auto bounds = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height))
                            .reduced(4.0f, 2.5f);

    if (isCurrent)
    {
        juce::ColourGradient grad(accentHardware.withAlpha(0.28f), bounds.getX(), 0.0f,
                                  accentHardware.withAlpha(0.1f), bounds.getRight(), 0.0f, false);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(bounds, 7.0f);
        g.setColour(accentHardware);
        g.drawRoundedRectangle(bounds.reduced(0.5f), 7.0f, 1.2f);
    }
    else
    {
        juce::ColourGradient grad(juce::Colour(0xff241f1a), bounds.getX(), bounds.getY(), juce::Colour(0xff1e1a15),
                                  bounds.getX(), bounds.getBottom(), false);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(bounds, 7.0f);
        g.setColour(rowIsSelected ? accentHardware : inkBorder);
        g.drawRoundedRectangle(bounds.reduced(0.5f), 7.0f, 1.0f);
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

    auto textArea = bounds.reduced(12.0f, 6.0f);
    auto nameArea = textArea;
    if (badge.isNotEmpty())
        nameArea.removeFromRight(30.0f);

    g.setColour(textName);
    g.setFont(juce::Font(13.0f, juce::Font::bold));
    g.drawText(name, nameArea.toNearestInt(), juce::Justification::centredLeft, true);

    if (badge.isNotEmpty())
    {
        g.setColour(accentHardware);
        g.setFont(juce::Font(9.0f, juce::Font::bold).withExtraKerningFactor(0.1f));
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
    const int dialogH = static_cast<int>(static_cast<float>(full.getHeight()) * 0.82f);
    const auto dialog = juce::Rectangle<int>(0, 0, dialogW, dialogH).withCentre(full.getCentre());
    dialogBounds_ = dialog.toFloat();

    auto r = dialog;
    auto header = r.removeFromTop(64).reduced(18, 12);
    close_.setBounds(header.removeFromRight(30).withSizeKeepingCentre(30, 30));
    header.removeFromRight(8);
    title_.setBounds(header.removeFromTop(20));
    subtitle_.setBounds(header);

    auto searchBlock = r.removeFromTop(58).reduced(18, 12);
    search_.setBounds(searchBlock);

    auto footer = r.removeFromBottom(36).reduced(18, 0);
    shownCount_.setBounds(footer.removeFromLeft(footer.getWidth() / 2));
    totalCount_.setBounds(footer);

    list_.setBounds(r.reduced(8));
}

void ProfileLibrary::paint(juce::Graphics& g)
{
    using namespace palette;

    g.fillAll(scrim);
    if (dialogBounds_.isEmpty())
        return;

    const auto d = dialogBounds_;
    juce::ColourGradient dialogGrad(chassisTop, d.getX(), d.getY(), panelBottom, d.getX(), d.getBottom(), false);
    g.setGradientFill(dialogGrad);
    g.fillRoundedRectangle(d, 14.0f);

    g.saveState();
    juce::Path clip;
    clip.addRoundedRectangle(d, 14.0f);
    g.reduceClipRegion(clip);

    const auto headerRect = d.withHeight(64.0f);
    juce::ColourGradient headerGrad(juce::Colour(0xff3a352e), headerRect.getX(), headerRect.getY(), panelBottom,
                                    headerRect.getX(), headerRect.getBottom(), false);
    g.setGradientFill(headerGrad);
    g.fillRect(headerRect);
    g.setColour(inkBorder);
    g.drawHorizontalLine(juce::roundToInt(headerRect.getBottom()), d.getX(), d.getRight());

    const auto searchRect = juce::Rectangle<float>(d.getX(), headerRect.getBottom(), d.getWidth(), 58.0f);
    g.setColour(juce::Colour(0xff241f1a));
    g.fillRect(searchRect);

    const auto footerRect = juce::Rectangle<float>(d.getX(), d.getBottom() - 36.0f, d.getWidth(), 36.0f);
    g.setColour(panelDeep);
    g.fillRect(juce::Rectangle<float>(d.getX(), searchRect.getBottom(), d.getWidth(),
                                      footerRect.getY() - searchRect.getBottom()));

    juce::ColourGradient footerGrad(panelTop, footerRect.getX(), footerRect.getY(), panelBottom, footerRect.getX(),
                                    footerRect.getBottom(), false);
    g.setGradientFill(footerGrad);
    g.fillRect(footerRect);
    g.setColour(inkBorder);
    g.drawHorizontalLine(juce::roundToInt(footerRect.getY()), d.getX(), d.getRight());

    g.restoreState();
    g.setColour(inkBorder);
    g.drawRoundedRectangle(d.reduced(0.5f), 14.0f, 1.0f);

    // Magnifying glass, drawn beside the search field (the field's own text
    // is indented via setIndents() to leave room for it).
    const auto searchBounds = search_.getBounds().toFloat();
    const auto glassCentre = juce::Point<float>(searchBounds.getX() + 9.0f, searchBounds.getCentreY() - 1.0f);
    g.setColour(juce::Colour(0xff6f8a3f));
    g.drawEllipse(juce::Rectangle<float>(8.0f, 8.0f).withCentre(glassCentre), 1.5f);
    g.drawLine(glassCentre.x + 2.6f, glassCentre.y + 2.6f, glassCentre.x + 6.0f, glassCentre.y + 6.0f, 1.6f);
}

} // namespace aaom
