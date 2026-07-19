#include "AAOMEditor.h"
#include "AAOMProcessor.h"
#include "gui/Palette.h"

namespace aaom
{

namespace
{
void styleSideLabel(juce::Label& label, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setFont(juce::Font(9.0f).withExtraKerningFactor(0.14f));
    label.setColour(juce::Label::textColourId, palette::textDim);
    label.setJustificationType(juce::Justification::centred);
}
} // namespace

AAOMEditor::AAOMEditor(AAOMProcessor& processor)
: juce::AudioProcessorEditor(processor)
, proc_(processor)
, library_(processor)
, eqBass_(pid::eqBass, processor.getParameterManager().getAPVTS())
, eqMid_(pid::eqMid, processor.getParameterManager().getAPVTS())
, eqTreble_(pid::eqTreble, processor.getParameterManager().getAPVTS())
, eqPresence_(pid::eqPresence, processor.getParameterManager().getAPVTS())
, inputGain_(pid::inputGain, processor.getParameterManager().getAPVTS())
, outputGain_(pid::outputGain, processor.getParameterManager().getAPVTS())
{
    setLookAndFeel(&lookAndFeel_);

    subtitle_.setFont(juce::Font(10.0f, juce::Font::plain).withExtraKerningFactor(0.35f));
    addAndMakeVisible(subtitle_);

    // No preset system exists in this plugin; the chip is decorative chrome
    // (matches the hardware look) rather than a functioning preset menu. The
    // loaded model's stats (or the bundle-load error) go on its tooltip,
    // since the chassis has no permanent room for that diagnostic text.
    presetChip_.setText("NO PRESET");
    presetChip_.setTextColour(juce::Colour(0xffc9a24a));
    presetChip_.setCaretShown(true);
    presetChip_.setInterceptsMouseClicks(true, false);
    addAndMakeVisible(presetChip_);

    if (proc_.modelLoaded())
    {
        const auto* m = proc_.engine().model();
        const juce::String status = juce::String("Model: C=") + juce::String(m->channels()) + ", E="
                                    + juce::String(m->embeddingDim()) + ", " + juce::String(m->numLayers())
                                    + " layers, run " + juce::String(m->runSha8());
        subtitle_.setText("FOUR-CORNER TONE MIXER", juce::dontSendNotification);
        subtitle_.setColour(juce::Label::textColourId, juce::Colour(0xff8a8178));
        presetChip_.setTooltip(status);
    }
    else
    {
        const juce::String error = proc_.bundleError().isNotEmpty() ? proc_.bundleError()
                                                                    : juce::String("No model loaded");
        subtitle_.setText(error, juce::dontSendNotification);
        subtitle_.setColour(juce::Label::textColourId, palette::clearTop.brighter(0.6f));
        presetChip_.setTooltip(error);
    }

    // EQ: UI + automatable parameters only for now, not yet wired into the
    // signal path (see AAOMProcessor::makeParameters).
    for (auto* slider : {&eqBass_, &eqMid_, &eqTreble_, &eqPresence_})
    {
        slider->setSliderStyle(juce::Slider::LinearVertical);
        slider->setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        addAndMakeVisible(*slider);
    }
    styleSideLabel(eqLabels_[0], "BASS");
    styleSideLabel(eqLabels_[1], "MID");
    styleSideLabel(eqLabels_[2], "TREBLE");
    styleSideLabel(eqLabels_[3], "PRES");
    for (auto& l : eqLabels_)
        addAndMakeVisible(l);

    for (auto* knob : {&inputGain_, &outputGain_})
    {
        knob->setSliderStyle(juce::Slider::RotaryVerticalDrag);
        knob->setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        addAndMakeVisible(*knob);
    }

    inputGain_.onValueChange = [this] { inputLcd_.setText(juce::String(inputGain_.getValue(), 1)); };
    outputGain_.onValueChange = [this] { outputLcd_.setText(juce::String(outputGain_.getValue(), 1)); };
    inputLcd_.setText(juce::String(inputGain_.getValue(), 1));
    outputLcd_.setText(juce::String(outputGain_.getValue(), 1));
    addAndMakeVisible(inputLcd_);
    addAndMakeVisible(outputLcd_);
    styleSideLabel(inputLabel_, "INPUT");
    styleSideLabel(outputLabel_, "OUTPUT");
    addAndMakeVisible(inputLabel_);
    addAndMakeVisible(outputLabel_);

    pad_.onDrag = [this](float x, float y) {
        auto& apvts = proc_.getParameterManager().getAPVTS();
        if (auto* px = apvts.getParameter(pid::morphX))
            px->setValueNotifyingHost(x);
        if (auto* py = apvts.getParameter(pid::morphY))
            py->setValueNotifyingHost(y);
    };
    addAndMakeVisible(pad_);

    for (int i = 0; i < 4; ++i)
    {
        const bool alignRight = (i == 1 || i == 3); // BR, TR sit in the right column
        slots_[static_cast<std::size_t>(i)] = std::make_unique<AmpSlotComponent>(i, alignRight);
        slots_[static_cast<std::size_t>(i)]->onSelect = [this](int c) { handleSelect(c); };
        slots_[static_cast<std::size_t>(i)]->onPaste = [this](int c) { handlePaste(c); };
        slots_[static_cast<std::size_t>(i)]->onClear = [this](int c) { handleClear(c); };
        addAndMakeVisible(*slots_[static_cast<std::size_t>(i)]);
    }

    library_.onPick = [this](int corner, int idx) { handlePick(corner, idx); };
    addChildComponent(library_);

    refreshCorners();

    setResizable(true, true);
    setResizeLimits(480, 448, 960, 896);
    if (auto* constrainer = getConstrainer())
        constrainer->setFixedAspectRatio(620.0 / 578.0);
    setSize(620, 578);
    startTimerHz(30);
}

AAOMEditor::~AAOMEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void AAOMEditor::timerCallback()
{
    auto& apvts = proc_.getParameterManager().getAPVTS();
    float x = 0.5f, y = 0.5f;
    if (auto* rx = apvts.getRawParameterValue(pid::morphX))
        x = rx->load();
    if (auto* ry = apvts.getRawParameterValue(pid::morphY))
        y = ry->load();
    pad_.setDotPosition(x, y);

    const CornerWeights w = computeCornerWeights(x, y);
    const std::array<float, 4> weights{{w.bl, w.br, w.tl, w.tr}};
    for (int i = 0; i < 4; ++i)
        slots_[static_cast<std::size_t>(i)]->setWeight(weights[static_cast<std::size_t>(i)]);

    if (proc_.cornerGeneration() != lastCornerGen_)
        refreshCorners();
}

void AAOMEditor::refreshCorners()
{
    lastCornerGen_ = proc_.cornerGeneration();
    std::array<bool, 4> assigned{{false, false, false, false}};
    for (int i = 0; i < 4; ++i)
    {
        const auto& c = proc_.corner(i);
        assigned[static_cast<std::size_t>(i)] = c.assigned;
        slots_[static_cast<std::size_t>(i)]->setContents(c.assigned, c.name);
    }
    pad_.setCornerAssigned(assigned);
}

void AAOMEditor::handleSelect(int corner)
{
    if (proc_.numCatalogProfiles() <= 0)
    {
        juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Profile Library",
                                                    "No default profiles are available for this model.", this);
        return;
    }
    library_.open(corner);
}

void AAOMEditor::handlePick(int corner, int profileIndex)
{
    if (proc_.loadCatalogProfile(corner, profileIndex))
        refreshCorners();
}

void AAOMEditor::handlePaste(int corner)
{
    const juce::String json = juce::SystemClipboard::getTextFromClipboard();
    if (json.trim().isEmpty())
    {
        juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Paste profile",
                                                    "Copy an aaom_profile JSON to the clipboard, then right-click a "
                                                    "corner and choose Paste.",
                                                    this);
        return;
    }
    doPaste(corner, json, false);
}

void AAOMEditor::doPaste(int corner, const juce::String& json, bool allowRunMismatch)
{
    juce::String message;
    const PasteResult result = proc_.tryPasteProfile(corner, json, allowRunMismatch, message);

    switch (result)
    {
        case PasteResult::Ok:
        case PasteResult::OkRunMismatch:
            refreshCorners();
            break;

        case PasteResult::RejectedRunMismatch:
            juce::AlertWindow::showOkCancelBox(
                juce::MessageBoxIconType::WarningIcon, "Run mismatch", message, "Load anyway", "Cancel", this,
                juce::ModalCallbackFunction::create([this, corner, json](int r) {
                    if (r == 1)
                        doPaste(corner, json, true);
                }));
            break;

        case PasteResult::RejectedDim:
        case PasteResult::RejectedParse:
        default:
            juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Paste rejected",
                                                        message, this);
            break;
    }
}

void AAOMEditor::handleClear(int corner)
{
    proc_.clearCorner(corner);
    refreshCorners();
}

void AAOMEditor::paint(juce::Graphics& g)
{
    using namespace palette;
    const auto bounds = getLocalBounds().toFloat();
    const float scale = static_cast<float>(getWidth()) / 620.0f;

    juce::ColourGradient chassisGrad(chassisTop, bounds.getX(), bounds.getY(), chassisBottom, bounds.getX(),
                                     bounds.getBottom(), false);
    g.setGradientFill(chassisGrad);
    g.fillRoundedRectangle(bounds, 14.0f * scale);

    {
        g.saveState();
        juce::Path clip;
        clip.addRoundedRectangle(bounds, 14.0f * scale);
        g.reduceClipRegion(clip);
        g.setColour(juce::Colours::white.withAlpha(0.045f));
        for (float x = bounds.getX(); x < bounds.getRight(); x += 3.0f)
            g.drawVerticalLine(juce::roundToInt(x), bounds.getY(), bounds.getBottom());
        g.restoreState();
    }

    g.setColour(inkBorder);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 14.0f * scale, 1.0f);

    // Four corner screws.
    const float screwR = 7.5f * scale;
    const float inset = 9.0f * scale + screwR;
    const std::array<juce::Point<float>, 4> screwCentres{
        {{bounds.getX() + inset, bounds.getY() + inset},
         {bounds.getRight() - inset, bounds.getY() + inset},
         {bounds.getX() + inset, bounds.getBottom() - inset},
         {bounds.getRight() - inset, bounds.getBottom() - inset}}};
    const std::array<float, 4> screwAngles{{35.0f, -40.0f, -15.0f, 20.0f}};
    for (int i = 0; i < 4; ++i)
    {
        const auto c = screwCentres[static_cast<std::size_t>(i)];
        juce::ColourGradient screwGrad(screwLight, c.x - screwR * 0.3f, c.y - screwR * 0.3f, screwDark,
                                       c.x + screwR, c.y + screwR, true);
        g.setGradientFill(screwGrad);
        g.fillEllipse(juce::Rectangle<float>(screwR * 2.0f, screwR * 2.0f).withCentre(c));

        juce::Path slot;
        slot.addRoundedRectangle(-screwR * 0.75f, -1.0f * scale, screwR * 1.5f, 2.0f * scale, 1.0f);
        g.setColour(juce::Colour(0xff1a1712));
        g.fillPath(slot, juce::AffineTransform::rotation(juce::degreesToRadians(
                                                              screwAngles[static_cast<std::size_t>(i)]))
                             .translated(c.x, c.y));
    }

    // Nameplate title: two-tone "ALLAMPSONME" + accent "MORPH".
    auto titleFont = juce::Font(22.0f * scale, juce::Font::bold).withExtraKerningFactor(0.05f);
    g.setFont(titleFont);
    const juce::String part1("ALLAMPSONME ");
    const float w1 = juce::GlyphArrangement::getStringWidth(titleFont, part1);
    g.setColour(textEngraved);
    g.drawText(part1, titleBounds_.withWidth(w1).toNearestInt(), juce::Justification::centredLeft, false);
    g.setColour(accentHardware);
    g.drawText("MORPH", titleBounds_.withX(titleBounds_.getX() + w1).toNearestInt(),
              juce::Justification::centredLeft, false);

    // Bottom strip panel + divider.
    juce::ColourGradient stripGrad(panelTop, bottomStripBounds_.getX(), bottomStripBounds_.getY(), panelBottom,
                                   bottomStripBounds_.getX(), bottomStripBounds_.getBottom(), false);
    g.setGradientFill(stripGrad);
    g.fillRoundedRectangle(bottomStripBounds_, 10.0f * scale);
    g.setColour(inkBorder);
    g.drawRoundedRectangle(bottomStripBounds_.reduced(0.5f), 10.0f * scale, 1.0f);
    g.drawVerticalLine(bottomStripDividerX_, bottomStripBounds_.getY() + 12.0f * scale,
                       bottomStripBounds_.getBottom() - 12.0f * scale);
}

void AAOMEditor::resized()
{
    const float scale = static_cast<float>(getWidth()) / 620.0f;
    auto s = [scale](int v) { return juce::roundToInt(static_cast<float>(v) * scale); };

    auto r = getLocalBounds().reduced(s(22));

    // Nameplate: title (left) + subtitle (below) + preset chip (right).
    auto nameplate = r.removeFromTop(s(46));
    auto chipArea = nameplate.removeFromRight(s(112));
    presetChip_.setBounds(chipArea.withSizeKeepingCentre(chipArea.getWidth(), s(26)));
    titleBounds_ = nameplate.removeFromTop(s(27)).toFloat();
    subtitle_.setBounds(nameplate);

    r.removeFromTop(s(14));

    // Bottom strip reserved first so the middle row gets the remaining height.
    auto bottomStrip = r.removeFromBottom(s(136));
    bottomStripBounds_ = bottomStrip.toFloat();
    r.removeFromBottom(s(14));

    // Middle row: left corner column / CRT pad (flex) / right corner column.
    const int colW = s(138);
    auto middle = r;
    auto leftCol = middle.removeFromLeft(colW);
    middle.removeFromLeft(s(14));
    auto rightCol = middle.removeFromRight(colW);
    middle.removeFromRight(s(14));

    const int slotGap = s(12);
    auto leftTop = leftCol.removeFromTop((leftCol.getHeight() - slotGap) / 2);
    leftCol.removeFromTop(slotGap);
    slots_[2]->setBounds(leftTop); // TL
    slots_[0]->setBounds(leftCol); // BL

    auto rightTop = rightCol.removeFromTop((rightCol.getHeight() - slotGap) / 2);
    rightCol.removeFromTop(slotGap);
    slots_[3]->setBounds(rightTop); // TR
    slots_[1]->setBounds(rightCol); // BR

    pad_.setBounds(middle);

    // Bottom strip: EQ faders (left) | divider | input/output (right).
    auto strip = bottomStrip.reduced(s(20), s(16));
    auto ioBlock = strip.removeFromRight(s(120));
    strip.removeFromRight(s(14));
    bottomStripDividerX_ = strip.getRight();
    strip.removeFromRight(s(14));

    const int eqW = strip.getWidth() / 4;
    auto layoutEq = [&](mrta::ParameterSlider& slider, juce::Label& label, juce::Rectangle<int> col) {
        auto labelArea = col.removeFromBottom(s(16));
        col.removeFromBottom(s(8));
        const int faderW = juce::jmax(20, s(26));
        slider.setBounds(col.withSizeKeepingCentre(faderW, col.getHeight()));
        label.setBounds(labelArea);
    };
    layoutEq(eqBass_, eqLabels_[0], strip.removeFromLeft(eqW));
    layoutEq(eqMid_, eqLabels_[1], strip.removeFromLeft(eqW));
    layoutEq(eqTreble_, eqLabels_[2], strip.removeFromLeft(eqW));
    layoutEq(eqPresence_, eqLabels_[3], strip);

    const int knobW = ioBlock.getWidth() / 2;
    auto layoutIo = [&](mrta::ParameterSlider& knob, LcdReadout& lcd, juce::Label& label,
                       juce::Rectangle<int> col) {
        auto knobArea = col.removeFromTop(s(48));
        col.removeFromTop(s(6));
        auto lcdArea = col.removeFromTop(s(22));
        col.removeFromTop(s(6));
        knob.setBounds(knobArea.withSizeKeepingCentre(s(46), s(46)));
        lcd.setBounds(lcdArea.reduced(s(2), 0));
        label.setBounds(col);
    };
    layoutIo(inputGain_, inputLcd_, inputLabel_, ioBlock.removeFromLeft(knobW));
    layoutIo(outputGain_, outputLcd_, outputLabel_, ioBlock);

    library_.setBounds(getLocalBounds());
}

} // namespace aaom
