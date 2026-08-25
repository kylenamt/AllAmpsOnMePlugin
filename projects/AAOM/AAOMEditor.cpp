#include "AAOMEditor.h"
#include "AAOMProcessor.h"
#include "gui/Palette.h"

#include <cmath>

namespace aaom
{

namespace
{
const juce::String kPlusMinus(juce::CharPointer_UTF8("\xc2\xb1")); // U+00B1
const juce::String kMinus(juce::CharPointer_UTF8("\xe2\x88\x92")); // U+2212

// Matches the design handoff's fmtSigned(): '+'/'-' prefix (dead zone around
// 0), then the magnitude with `decimals` places, then `unit`.
juce::String formatSigned(float v, int decimals, const juce::String& unit)
{
    const juce::String sign = v < -0.0499f ? kMinus : v > 0.0499f ? juce::String("+") : juce::String();
    return sign + juce::String(std::abs(v), decimals) + unit;
}

void styleSideLabel(juce::Label& label, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::plain)
                     .withExtraKerningFactor(0.12f));
    label.setColour(juce::Label::textColourId, palette::textMuted);
    label.setJustificationType(juce::Justification::centredLeft);
}
} // namespace

void AAOMEditor::ClickableLabel::mouseDown(const juce::MouseEvent&)
{
    if (onClick)
        onClick();
}

AAOMEditor::AAOMEditor(AAOMProcessor& processor)
: juce::AudioProcessorEditor(processor)
, proc_(processor)
, library_(processor)
, rangeSlider_(pid::morphRange, processor.getParameterManager().getAPVTS())
, smoothSlider_(pid::morphSmooth, processor.getParameterManager().getAPVTS())
, inputGain_(pid::inputGain, processor.getParameterManager().getAPVTS())
, eqBass_(pid::eqBass, processor.getParameterManager().getAPVTS())
, eqMid_(pid::eqMid, processor.getParameterManager().getAPVTS())
, eqTreble_(pid::eqTreble, processor.getParameterManager().getAPVTS())
, eqPresence_(pid::eqPresence, processor.getParameterManager().getAPVTS())
, outputGain_(pid::outputGain, processor.getParameterManager().getAPVTS())
, cabToggle_(pid::cabOn, processor.getParameterManager().getAPVTS())
{
    setLookAndFeel(&lookAndFeel_);
    using namespace palette;

    help_.setInterceptsMouseClicks(true, false);
    help_.setTooltip("Drag the field to blend the four corner profiles. Drag past a corner to "
                     "extrapolate beyond it -- RANGE controls how far.");
    addAndMakeVisible(help_);

    // Status pill: shows which bundle is live and opens a menu to switch. The
    // loaded model's stats (or the load error / warning) go on its tooltip,
    // since the header has no permanent room for that text.
    presetChip_.setInterceptsMouseClicks(true, false);
    presetChip_.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    presetChip_.onClick = [this] { showModelMenu(); };
    addAndMakeVisible(presetChip_);
    // Populated by refreshModelChip() below -- it refreshes the corner slots
    // too, so it has to run after those are constructed.

    pad_.onDrag = [this](float x, float y) {
        // setValueNotifyingHost expects a normalised [0,1] value; the pad
        // reports actual morph coordinates (which range past [0,1] to
        // extrapolate).
        auto& apvts = proc_.getParameterManager().getAPVTS();
        if (auto* px = apvts.getParameter(pid::morphX))
            px->setValueNotifyingHost(px->convertTo0to1(x));
        if (auto* py = apvts.getParameter(pid::morphY))
            py->setValueNotifyingHost(py->convertTo0to1(y));
    };
    addAndMakeVisible(pad_);

    for (int i = 0; i < 4; ++i)
    {
        slots_[static_cast<std::size_t>(i)] = std::make_unique<AmpSlotComponent>(i);
        slots_[static_cast<std::size_t>(i)]->onSelect = [this](int c) { handleSelect(c); };
        slots_[static_cast<std::size_t>(i)]->onPaste = [this](int c) { handlePaste(c); };
        slots_[static_cast<std::size_t>(i)]->onClear = [this](int c) { handleClear(c); };
        addAndMakeVisible(*slots_[static_cast<std::size_t>(i)]);
    }

    library_.onPick = [this](int corner, int idx) { handlePick(corner, idx); };
    addChildComponent(library_);

    // RANGE / SMOOTH controls block.
    styleSideLabel(rangeLabel_, "RANGE");
    addAndMakeVisible(rangeLabel_);
    rangeSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    rangeSlider_.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    rangeSlider_.onValueChange = [this] {
        const auto r = static_cast<float>(rangeSlider_.getValue());
        pad_.setRange(r);
        rangeValue_.setText(kPlusMinus + juce::String(r, 2), juce::dontSendNotification);
    };
    addAndMakeVisible(rangeSlider_);
    rangeValue_.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 11.5f, juce::Font::plain));
    rangeValue_.setColour(juce::Label::textColourId, textValue);
    rangeValue_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(rangeValue_);
    rangeSlider_.onValueChange();

    styleSideLabel(smoothLabel_, "SMOOTH");
    addAndMakeVisible(smoothLabel_);
    smoothSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    smoothSlider_.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    smoothSlider_.onValueChange = [this] {
        smoothValue_.setText(juce::String(juce::roundToInt(smoothSlider_.getValue())) + " ms",
                             juce::dontSendNotification);
    };
    addAndMakeVisible(smoothSlider_);
    smoothValue_.setFont(rangeValue_.getFont());
    smoothValue_.setColour(juce::Label::textColourId, textValue);
    smoothValue_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(smoothValue_);
    smoothSlider_.onValueChange();

    // Knob row.
    static const char* const kKnobLabels[6] = {"Input", "Bass", "Mid", "Treble", "Presence", "Output"};
    static const int kKnobDecimals[6] = {1, 1, 1, 1, 1, 1};
    static const char* const kKnobUnits[6] = {" dB", "", "", "", "", " dB"};
    const std::array<mrta::ParameterSlider*, 6> knobs{
        {&inputGain_, &eqBass_, &eqMid_, &eqTreble_, &eqPresence_, &outputGain_}};
    for (std::size_t i = 0; i < knobs.size(); ++i)
    {
        auto* knob = knobs[i];
        knob->setSliderStyle(juce::Slider::RotaryVerticalDrag);
        knob->setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        // JUCE's own default sweep (7 o'clock..5 o'clock, 288deg) is close but
        // not identical to the design's exact conic-gradient geometry; pin it
        // to -135deg..+135deg (270deg) so the ring/pointer match the spec.
        knob->setRotaryParameters(-juce::MathConstants<float>::pi * 0.75f, juce::MathConstants<float>::pi * 0.75f,
                                  true);
        addAndMakeVisible(*knob);

        knobLabels_[i].setText(kKnobLabels[i], juce::dontSendNotification);
        knobLabels_[i].setFont(juce::Font(10.0f, juce::Font::plain).withExtraKerningFactor(0.06f));
        knobLabels_[i].setColour(juce::Label::textColourId, textLabel);
        knobLabels_[i].setJustificationType(juce::Justification::centred);
        addAndMakeVisible(knobLabels_[i]);

        knobValues_[i].setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 11.5f, juce::Font::plain));
        knobValues_[i].setColour(juce::Label::textColourId, textValue);
        knobValues_[i].setJustificationType(juce::Justification::centred);
        addAndMakeVisible(knobValues_[i]);

        const int decimals = kKnobDecimals[i];
        const juce::String unit = kKnobUnits[i];
        auto& valueLabel = knobValues_[i];
        knob->onValueChange = [knob, &valueLabel, decimals, unit] {
            valueLabel.setText(formatSigned(static_cast<float>(knob->getValue()), decimals, unit),
                               juce::dontSendNotification);
        };
        knob->onValueChange();
    }

    // Cab IR strip: label | name (click opens the load menu) | on/off switch.
    styleSideLabel(cabLabel_, "CAB IR");
    addAndMakeVisible(cabLabel_);

    cabName_.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
    cabName_.setJustificationType(juce::Justification::centred);
    cabName_.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    cabName_.onClick = [this] { showCabMenu(); };
    addAndMakeVisible(cabName_);

    cabToggle_.setComponentID("cabSwitch");
    cabToggle_.setButtonText({});
    cabToggle_.setTooltip("Switch the cabinet IR in and out of the chain.");
    addAndMakeVisible(cabToggle_);
    refreshCabChip();

    // Fills in the status pill and corner slots. Must come after slots_ exist.
    refreshModelChip();

    setResizable(false, false);
    setSize(940, 740);
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
    int leadIdx = 0;
    for (int i = 1; i < 4; ++i)
        if (weights[static_cast<std::size_t>(i)] > weights[static_cast<std::size_t>(leadIdx)])
            leadIdx = i;
    for (int i = 0; i < 4; ++i)
    {
        slots_[static_cast<std::size_t>(i)]->setWeight(weights[static_cast<std::size_t>(i)]);
        slots_[static_cast<std::size_t>(i)]->setLeading(i == leadIdx);
    }

    if (proc_.cornerGeneration() != lastCornerGen_)
        refreshCorners();

    // The model can also change outside the menu (e.g. session restore).
    if (proc_.modelGeneration() != lastModelGen_)
        refreshModelChip();

    // Same for the cab IR: a restored session loads one behind the UI's back.
    if (proc_.irGeneration() != lastIrGen_)
        refreshCabChip();

    // CabOn is a plain toggle button (no onValueChange-style hook for
    // automation-driven changes), so its effect on the IR name text is
    // polled here alongside everything else.
    bool cabOn = true;
    if (auto* rc = apvts.getRawParameterValue(pid::cabOn))
        cabOn = rc->load() > 0.5f;
    if (cabOn != lastCabOn_)
    {
        lastCabOn_ = cabOn;
        updateCabText();
    }
}

void AAOMEditor::refreshModelChip()
{
    lastModelGen_ = proc_.modelGeneration();

    presetChip_.setText(proc_.modelDisplayName().toUpperCase() + "  " + juce::String(proc_.numCatalogProfiles()));

    if (proc_.modelLoaded())
    {
        const auto* m = proc_.engine().model();
        juce::String status = juce::String("Model: C=") + juce::String(m->channels()) + ", E="
                              + juce::String(m->embeddingDim()) + ", " + juce::String(m->numLayers())
                              + " layers, run " + juce::String(m->runSha8());
        status << "\nSource: "
               << (proc_.usingBuiltInModel() ? juce::String("built-in") : proc_.modelFile().getFullPathName());
        status << "\nProfiles: " << juce::String(proc_.numCatalogProfiles());
        status << "\n\nClick to switch model.";

        const bool warned = proc_.modelWarning().isNotEmpty();
        if (warned)
            status = proc_.modelWarning() + "\n\n" + status;

        presetChip_.setTextColour(warned ? palette::negativeText : palette::cyan);
        presetChip_.setTooltip(status);
    }
    else
    {
        const juce::String error = proc_.bundleError().isNotEmpty() ? proc_.bundleError()
                                                                    : juce::String("No model loaded");
        presetChip_.setTextColour(palette::negativeText);
        presetChip_.setTooltip(error + "\n\nClick to switch model.");
    }

    // A new model brings a new catalogue and freshly reseeded corners.
    refreshCorners();
}

void AAOMEditor::showModelMenu()
{
    const juce::StringArray recents = proc_.recentModelFiles();
    const juce::File current = proc_.modelFile();

    // IDs: 2 = browse, 3 = clear recents, 100+n = recents[n], 1000+i = built-in i.
    juce::PopupMenu menu;
    const int numBuiltIn = AAOMProcessor::numBuiltInModels();
    if (numBuiltIn > 1)
        menu.addSectionHeader("Built-in");
    for (int i = 0; i < numBuiltIn; ++i)
        menu.addItem(1000 + i, AAOMProcessor::builtInModelName(i), true, proc_.builtInModelIndex() == i);

    if (!recents.isEmpty())
    {
        menu.addSeparator();
        menu.addSectionHeader("Recent");
        for (int i = 0; i < recents.size(); ++i)
        {
            const juce::File f{recents[i]};
            // Missing files stay listed but greyed out, so a moved bundle is
            // visible rather than silently vanishing from the menu.
            menu.addItem(100 + i, f.getFileNameWithoutExtension(), f.existsAsFile(), f == current);
        }
    }

    menu.addSeparator();
    menu.addItem(2, "Load bundle...");
    menu.addItem(3, "Clear recent list", !recents.isEmpty(), false);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(presetChip_),
                       [this, recents](int result) {
                           if (result == 0)
                               return;
                           if (result >= 1000)
                           {
                               proc_.loadBuiltInModel(result - 1000);
                               refreshModelChip();
                           }
                           else if (result == 2)
                           {
                               browseForModel();
                           }
                           else if (result == 3)
                           {
                               proc_.clearRecentModelFiles();
                           }
                           else if (result >= 100 && result - 100 < recents.size())
                           {
                               loadModel(juce::File{recents[result - 100]});
                           }
                       });
}

void AAOMEditor::browseForModel()
{
    const juce::File start = proc_.usingBuiltInModel()
                                 ? juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                 : proc_.modelFile().getParentDirectory();

    chooser_ = std::make_unique<juce::FileChooser>("Load an AAOM bundle", start, "*.json");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this](const juce::FileChooser& fc) {
                              const juce::File file = fc.getResult();
                              if (file != juce::File{})
                                  loadModel(file);
                          });
}

void AAOMEditor::loadModel(const juce::File& file)
{
    juce::String message;
    if (proc_.loadModelFromFile(file, message))
    {
        refreshModelChip();
        return;
    }

    // The live model is untouched on failure, so this is purely informational.
    juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Model not loaded",
                                                message, this);
}

void AAOMEditor::updateCabText()
{
    using namespace palette;
    const bool loaded = proc_.irLoaded();
    const juce::String text =
        !loaded ? juce::String("No cab IR") : (lastCabOn_ ? proc_.irDisplayName() : juce::String("Cab bypassed"));
    cabName_.setText(text, juce::dontSendNotification);
    cabName_.setColour(juce::Label::textColourId, (loaded && lastCabOn_) ? textSecondary : textDisabled);
}

void AAOMEditor::refreshCabChip()
{
    lastIrGen_ = proc_.irGeneration();
    updateCabText();

    juce::String tip = proc_.irStatus();
    if (tip.isNotEmpty())
        tip << "\n\n";
    tip << "Click to load a cabinet impulse response.";
    cabName_.setTooltip(tip);
}

void AAOMEditor::showCabMenu()
{
    const juce::StringArray recents = proc_.recentIrFiles();
    const juce::File current = proc_.irFile();

    // IDs: 2 = browse, 3 = clear recents, 4 = unload, 100+n = recents[n].
    juce::PopupMenu menu;
    menu.addItem(2, "Load IR...");
    menu.addItem(4, "Remove cab IR", proc_.irLoaded(), false);

    if (!recents.isEmpty())
    {
        menu.addSeparator();
        menu.addSectionHeader("Recent");
        for (int i = 0; i < recents.size(); ++i)
        {
            const juce::File f{recents[i]};
            // Missing files stay listed but greyed out, so a moved IR is visible
            // rather than silently vanishing from the menu.
            menu.addItem(100 + i, f.getFileNameWithoutExtension(), f.existsAsFile(), f == current);
        }
        menu.addSeparator();
        menu.addItem(3, "Clear recent list");
    }

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(cabName_), [this, recents](int result) {
        if (result == 0)
            return;
        if (result == 2)
        {
            browseForIr();
        }
        else if (result == 3)
        {
            proc_.clearRecentIrFiles();
        }
        else if (result == 4)
        {
            proc_.clearIr();
            refreshCabChip();
        }
        else if (result >= 100 && result - 100 < recents.size())
        {
            loadIr(juce::File{recents[result - 100]});
        }
    });
}

void AAOMEditor::browseForIr()
{
    const juce::File start = proc_.irLoaded()
                                 ? proc_.irFile().getParentDirectory()
                                 : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);

    chooser_ = std::make_unique<juce::FileChooser>("Load a cabinet impulse response", start,
                                                   "*.wav;*.aif;*.aiff");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this](const juce::FileChooser& fc) {
                              const juce::File file = fc.getResult();
                              if (file != juce::File{})
                                  loadIr(file);
                          });
}

void AAOMEditor::loadIr(const juce::File& file)
{
    juce::String message;
    if (proc_.loadIrFromFile(file, message))
    {
        refreshCabChip();
        return;
    }

    // The loaded cab is untouched on failure, so this is purely informational.
    juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "IR not loaded", message,
                                                this);
}

void AAOMEditor::refreshCorners()
{
    // Reachable from refreshModelChip(), which the constructor calls; bail out
    // rather than dereferencing slots that have not been built yet.
    if (slots_[0] == nullptr)
        return;

    lastCornerGen_ = proc_.cornerGeneration();
    for (int i = 0; i < 4; ++i)
    {
        const auto& c = proc_.corner(i);
        slots_[static_cast<std::size_t>(i)]->setContents(c.assigned, c.name);
    }
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

    g.setColour(shell);
    g.fillRoundedRectangle(bounds, 20.0f);

    juce::ColourGradient highlight(juce::Colours::white.withAlpha(0.07f), bounds.getCentreX(),
                                   bounds.getY() - bounds.getHeight() * 0.18f, juce::Colours::transparentWhite,
                                   bounds.getCentreX(), bounds.getBottom(), true);
    g.setGradientFill(highlight);
    g.fillRoundedRectangle(bounds, 20.0f);

    // Ribbed vents, left and right.
    auto drawVent = [&](float ventX) {
        const juce::Rectangle<float> vent(ventX, bounds.getY() + 120.0f, 11.0f, bounds.getHeight() - 240.0f);
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.fillRoundedRectangle(vent, 5.0f);

        g.saveState();
        juce::Path clip;
        clip.addRoundedRectangle(vent, 5.0f);
        g.reduceClipRegion(clip);
        g.setColour(juce::Colours::white.withAlpha(0.10f));
        for (float ry = vent.getY(); ry < vent.getBottom(); ry += 3.0f)
            g.fillRect(vent.getX(), ry, vent.getWidth(), 1.0f);
        g.restoreState();

        g.setColour(juce::Colours::black.withAlpha(0.7f));
        g.drawRoundedRectangle(vent.reduced(0.5f), 5.0f, 1.0f);
    };
    drawVent(bounds.getX() + 9.0f);
    drawVent(bounds.getRight() - 20.0f);

    g.setColour(juce::Colours::black.withAlpha(0.65f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 20.0f, 1.0f);

    // Header hairline divider.
    juce::ColourGradient divGrad(juce::Colours::transparentWhite, dividerBounds_.getX(), dividerBounds_.getY(),
                                 juce::Colours::transparentWhite, dividerBounds_.getRight(),
                                 dividerBounds_.getY(), false);
    divGrad.addColour(0.5, juce::Colours::white.withAlpha(0.10f));
    g.setGradientFill(divGrad);
    g.fillRect(dividerBounds_);

    // Wordmark: two-tone "ALLAMPSONME" + accent "MORPH".
    auto wordFont = juce::Font(25.0f, juce::Font::plain).withExtraKerningFactor(0.22f);
    g.setFont(wordFont);
    const juce::String part1("ALLAMPSONME ");
    const float w1 = juce::GlyphArrangement::getStringWidth(wordFont, part1);
    g.setColour(textValue);
    g.drawText(part1, wordmarkBounds_.withWidth(w1).toNearestInt(), juce::Justification::centredLeft, false);
    g.setColour(cyan);
    g.drawText("MORPH", wordmarkBounds_.withX(wordmarkBounds_.getX() + w1).toNearestInt(),
              juce::Justification::centredLeft, false);

    // Help affordance.
    g.setColour(textPrimary.withAlpha(0.45f));
    g.drawEllipse(helpBounds_, 1.5f);
    g.setColour(textPrimary.withAlpha(0.6f));
    g.setFont(juce::Font(14.0f, juce::Font::plain));
    g.drawText("?", helpBounds_, juce::Justification::centred);

    // RANGE / SMOOTH controls block chrome.
    juce::ColourGradient controlsBg(shellLightTop, controlsBlockBounds_.getX(), controlsBlockBounds_.getY(),
                                    shellLightBottom, controlsBlockBounds_.getX(), controlsBlockBounds_.getBottom(),
                                    false);
    g.setGradientFill(controlsBg);
    g.fillRoundedRectangle(controlsBlockBounds_, 10.0f);
    g.setColour(juce::Colours::white.withAlpha(0.06f));
    g.drawLine(controlsBlockBounds_.getX() + 1.0f, controlsBlockBounds_.getY() + 0.5f,
              controlsBlockBounds_.getRight() - 1.0f, controlsBlockBounds_.getY() + 0.5f, 1.0f);
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.drawRoundedRectangle(controlsBlockBounds_.reduced(0.5f), 10.0f, 1.0f);

    // Knob row chrome.
    juce::ColourGradient knobBg(juce::Colour(0xff1b1f22), knobRowBounds_.getX(), knobRowBounds_.getY(), shell,
                                knobRowBounds_.getX(), knobRowBounds_.getBottom(), false);
    g.setGradientFill(knobBg);
    g.fillRoundedRectangle(knobRowBounds_, 12.0f);
    g.setColour(juce::Colours::white.withAlpha(0.07f));
    g.drawLine(knobRowBounds_.getX() + 1.0f, knobRowBounds_.getY() + 0.5f, knobRowBounds_.getRight() - 1.0f,
              knobRowBounds_.getY() + 0.5f, 1.0f);
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.drawRoundedRectangle(knobRowBounds_.reduced(0.5f), 12.0f, 1.0f);

    juce::ColourGradient kDiv(juce::Colours::transparentBlack, knobDividerX_, knobRowBounds_.getCentreY() - 38.0f,
                              juce::Colours::transparentBlack, knobDividerX_, knobRowBounds_.getCentreY() + 38.0f,
                              false);
    kDiv.addColour(0.5, juce::Colours::black.withAlpha(0.7f));
    g.setGradientFill(kDiv);
    g.fillRect(juce::Rectangle<float>(knobDividerX_ - 0.5f, knobRowBounds_.getCentreY() - 38.0f, 1.0f, 76.0f));

    // CAB IR bar chrome.
    g.setColour(recess);
    g.fillRoundedRectangle(cabBarBounds_, 8.0f);
    g.setColour(juce::Colours::black.withAlpha(0.8f));
    g.drawRoundedRectangle(cabBarBounds_.reduced(0.5f), 8.0f, 1.0f);
    g.setColour(juce::Colours::white.withAlpha(0.07f));
    g.fillRect(cabDividerX_ - 0.5f, cabBarBounds_.getY() + 10.0f, 1.0f, 18.0f);
}

void AAOMEditor::resized()
{
    auto content = getLocalBounds().reduced(22);

    // Header (56) / hairline divider (1) / main area (flex) / knob row (126)
    // / CAB IR bar (38), each separated by a 13px gap.
    auto header = content.removeFromTop(56);
    content.removeFromTop(13);
    dividerBounds_ = content.removeFromTop(1).toFloat();
    content.removeFromTop(13);

    auto cabBar = content.removeFromBottom(38);
    content.removeFromBottom(13);
    auto knobRow = content.removeFromBottom(126);
    content.removeFromBottom(13);

    auto mainArea = content;

    // --- Header --------------------------------------------------------
    auto headerRow = header.reduced(14, 0);
    auto helpArea = headerRow.removeFromRight(26);
    helpBounds_ = helpArea.withSizeKeepingCentre(26, 26).toFloat();
    help_.setBounds(helpArea.withSizeKeepingCentre(26, 26));
    headerRow.removeFromRight(16);
    auto pillArea = headerRow.removeFromRight(190);
    presetChip_.setBounds(pillArea.withSizeKeepingCentre(pillArea.getWidth(), 30));
    headerRow.removeFromRight(16);
    wordmarkBounds_ = headerRow.toFloat();

    // --- Main area: pad (flex) + 288px right column ---------------------
    auto rightCol = mainArea.removeFromRight(288);
    mainArea.removeFromRight(14);
    pad_.setBounds(mainArea);

    constexpr int kCardH = 71;
    constexpr int kGap = 9;
    auto placeCard = [&](AmpSlotComponent& slot) {
        slot.setBounds(rightCol.removeFromTop(kCardH));
        rightCol.removeFromTop(kGap);
    };
    // Order top-to-bottom: TL, TR, BL, BR (indices 2, 3, 0, 1).
    placeCard(*slots_[2]);
    placeCard(*slots_[3]);
    placeCard(*slots_[0]);
    placeCard(*slots_[1]);
    controlsBlockBounds_ = rightCol.toFloat();

    {
        auto controls = rightCol.reduced(13, 11);
        constexpr int kRowH = 20;
        auto centred = controls.withSizeKeepingCentre(controls.getWidth(), kRowH * 2 + 11);
        auto rangeRow = centred.removeFromTop(kRowH);
        centred.removeFromTop(11);
        auto smoothRow = centred;

        auto layoutRow = [](juce::Rectangle<int> row, juce::Label& label, juce::Component& slider,
                            juce::Label& value) {
            label.setBounds(row.removeFromLeft(50));
            row.removeFromLeft(11);
            value.setBounds(row.removeFromRight(48));
            row.removeFromRight(11);
            slider.setBounds(row);
        };
        layoutRow(rangeRow, rangeLabel_, rangeSlider_, rangeValue_);
        layoutRow(smoothRow, smoothLabel_, smoothSlider_, smoothValue_);
    }

    library_.setBounds(getLocalBounds());

    // --- Knob row: 5 main knobs, a divider, then Output -----------------
    knobRowBounds_ = knobRow.toFloat();
    {
        auto knobs = knobRow.reduced(22, 0);
        constexpr int kNumSlots = 7; // 5 main knobs + divider + Output
        const int slotW = knobs.getWidth() / kNumSlots;

        auto placeKnob = [&](mrta::ParameterSlider& knob, juce::Label& label, juce::Label& value) {
            auto slot = knobs.removeFromLeft(slotW);
            auto col = slot.withSizeKeepingCentre(juce::jmin(70, slot.getWidth()), 98);
            label.setBounds(col.removeFromTop(13));
            col.removeFromTop(7);
            knob.setBounds(col.removeFromTop(56).withSizeKeepingCentre(56, 56));
            col.removeFromTop(7);
            value.setBounds(col);
        };
        placeKnob(inputGain_, knobLabels_[0], knobValues_[0]);
        placeKnob(eqBass_, knobLabels_[1], knobValues_[1]);
        placeKnob(eqMid_, knobLabels_[2], knobValues_[2]);
        placeKnob(eqTreble_, knobLabels_[3], knobValues_[3]);
        placeKnob(eqPresence_, knobLabels_[4], knobValues_[4]);
        knobDividerX_ = static_cast<float>(knobs.removeFromLeft(slotW).getCentreX());
        placeKnob(outputGain_, knobLabels_[5], knobValues_[5]);
    }

    // --- CAB IR bar -------------------------------------------------------
    cabBarBounds_ = cabBar.toFloat();
    {
        auto cab = cabBar.reduced(12, 0);
        cabLabel_.setBounds(cab.removeFromLeft(50));
        cab.removeFromLeft(10);
        cabDividerX_ = static_cast<float>(cab.getX());
        cab.removeFromLeft(1 + 10);
        cabToggle_.setBounds(cab.removeFromRight(38).withSizeKeepingCentre(38, 19));
        cab.removeFromRight(10);
        cabName_.setBounds(cab);
    }
}

} // namespace aaom
