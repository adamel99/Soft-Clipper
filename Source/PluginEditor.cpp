#include "PluginEditor.h"

namespace Fonts
{
    static const juce::String& resolvedName()
    {
        static const juce::String name = []()
        {
            const auto available = juce::Font::findAllTypefaceNames();
            if (available.contains("Consolas")) return juce::String("Consolas");
            if (available.contains("Menlo"))    return juce::String("Menlo");
            return juce::String(juce::Font::getDefaultMonospacedFontName());
        }();

        return name;
    }

    static juce::Font mono(float height, bool bold = false)
    {
        return juce::Font(juce::FontOptions()
            .withName(resolvedName())
            .withHeight(height)
            .withStyle(bold ? "Bold" : "Regular"));
    }
}

namespace
{
    struct EditorLayout
    {
        juce::Rectangle<float> outerFrame;
        juce::Rectangle<float> headerBand;
        juce::Rectangle<float> visualizerFrame;
        juce::Rectangle<float> controlStrip;
        juce::Rectangle<float> inputCard;
        juce::Rectangle<float> outputCard;
        juce::Rectangle<float> satCard;
    };

    static EditorLayout makeEditorLayout(float W, float H)
    {
        constexpr float outerMargin = 8.0f;
        constexpr float cardGap = 12.0f;
        constexpr float cardInset = 10.0f;

        EditorLayout layout;
        layout.outerFrame = { outerMargin, outerMargin, W - outerMargin * 2.0f, H - outerMargin * 2.0f };
        layout.headerBand = { 0.0f, 0.0f, W, 58.0f };
        layout.controlStrip = { 0.0f, H - 126.0f, W, 126.0f };

        const float visualizerX = 20.0f;
        const float visualizerY = 58.0f + 14.0f;
        const float visualizerW = W - visualizerX * 2.0f;
        const float visualizerH = layout.controlStrip.getY() - visualizerY - 14.0f;
        layout.visualizerFrame = { visualizerX - 4.0f, visualizerY - 4.0f,
                                   visualizerW + 8.0f, visualizerH + 8.0f };

        const float satCardW = 176.0f;
        const float satCardX = W - visualizerX - satCardW;
        const float cardY = layout.controlStrip.getY() + cardInset;
        const float cardH = layout.controlStrip.getHeight() - cardInset * 2.0f;
        const float knobAreaX = visualizerX;
        const float knobAreaW = satCardX - cardGap - knobAreaX;
        const float leftCardW = knobAreaW * 0.42f;
        const float rightCardW = knobAreaW - leftCardW - cardGap;

        layout.inputCard = { knobAreaX, cardY, leftCardW, cardH };
        layout.outputCard = { knobAreaX + leftCardW + cardGap, cardY, rightCardW, cardH };
        layout.satCard = { satCardX, cardY, satCardW, cardH };
        return layout;
    }

    static void drawFloatingCard(juce::Graphics& g,
                                 const juce::Rectangle<float>& rect,
                                 float radius,
                                 juce::Colour fill,
                                 juce::Colour border,
                                 juce::Colour highlight,
                                 float shadowAlpha = 1.0f)
    {
        if (rect.isEmpty())
            return;

        const auto shadowRect = rect.translated(0.0f, 8.0f).expanded(2.0f, 2.0f);
        g.setColour(juce::Colours::black.withAlpha(0.32f * shadowAlpha));
        g.fillRoundedRectangle(shadowRect, radius + 2.0f);

        g.setColour(juce::Colours::black.withAlpha(0.16f * shadowAlpha));
        g.fillRoundedRectangle(rect.translated(0.0f, 3.0f).expanded(1.0f, 1.0f), radius + 1.0f);

        g.setColour(fill);
        g.fillRoundedRectangle(rect, radius);

        juce::ColourGradient glow(highlight.withAlpha(0.15f), rect.getCentreX(), rect.getY(),
                                  juce::Colours::transparentBlack, rect.getCentreX(), rect.getBottom(), false);
        g.setGradientFill(glow);
        g.fillRoundedRectangle(rect.reduced(1.5f), radius - 1.0f);

        g.setColour(border);
        g.drawRoundedRectangle(rect.reduced(0.5f), radius, 1.0f);

        g.setColour(highlight.withAlpha(0.22f));
        g.drawRoundedRectangle(rect.reduced(2.0f), radius - 1.5f, 0.8f);
    }
}

//==============================================================================
void ViveAudioProcessorEditor::configureHeader()
{
    titleLabel.setText(Params::Branding::productName, juce::dontSendNotification);
    titleLabel.setFont(Fonts::mono(24.0f, true));
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setColour(juce::Label::textColourId, EditorPalette::textPrimary());
    addAndMakeVisible(titleLabel);

    subtitleLabel.setText(Params::Branding::descriptor.toUpperCase(), juce::dontSendNotification);
    subtitleLabel.setFont(Fonts::mono(10.0f, true));
    subtitleLabel.setJustificationType(juce::Justification::centredLeft);
    subtitleLabel.setColour(juce::Label::textColourId, EditorPalette::textSecondary());
    addAndMakeVisible(subtitleLabel);

    presetStatusLabel.setFont(Fonts::mono(8.5f, true));
    presetStatusLabel.setJustificationType(juce::Justification::centredLeft);
    presetStatusLabel.setColour(juce::Label::textColourId, EditorPalette::textSecondary());
    addAndMakeVisible(presetStatusLabel);

    presetButton.setButtonText(Params::Branding::presetButton.toUpperCase());
    presetButton.onClick = [this]
    {
        showPresetMenu();
    };
    addAndMakeVisible(presetButton);
}

void ViveAudioProcessorEditor::configureSaturationSection()
{
    satSectionLabel.setText(Params::Names::SAT_MODE.toUpperCase(), juce::dontSendNotification);
    satSectionLabel.setFont(Fonts::mono(10.0f, true));
    satSectionLabel.setColour(juce::Label::textColourId, EditorPalette::textPrimary());
    satSectionLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(satSectionLabel);

    addAndMakeVisible(satSelector);

    auto& apvts = audioProcessor.getAPVTS();
    if (auto* param = dynamic_cast<juce::AudioParameterChoice*>(
            apvts.getParameter(Params::IDs::SAT_MODE_ID)))
    {
        satSelector.setActiveMode(param->getIndex());
    }

    satSelector.onModeChanged = [this](int newMode)
    {
        auto& apvts = audioProcessor.getAPVTS();

        if (auto* param = dynamic_cast<juce::AudioParameterChoice*>(
                apvts.getParameter(Params::IDs::SAT_MODE_ID)))
        {
            param->setValueNotifyingHost(
                param->getNormalisableRange().convertTo0to1(static_cast<float>(newMode)));
        }
    };
}

void ViveAudioProcessorEditor::configureKnob(juce::Slider& slider,
                                             juce::Label& nameLabel,
                                             juce::Label& valueLabel,
                                             const char* name,
                                             const char* sliderName,
                                             const juce::String& suffix)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setName(sliderName);
    addAndMakeVisible(slider);

    nameLabel.setText(name, juce::dontSendNotification);
    nameLabel.setFont(Fonts::mono(9.5f, true));
    nameLabel.setJustificationType(juce::Justification::centred);
    nameLabel.setColour(juce::Label::textColourId, EditorPalette::textPrimary());
    addAndMakeVisible(nameLabel);

    valueLabel.setText("-", juce::dontSendNotification);
    valueLabel.setFont(Fonts::mono(11.5f, true));
    valueLabel.setJustificationType(juce::Justification::centred);
    valueLabel.setColour(juce::Label::textColourId, EditorPalette::textPrimary());
    addAndMakeVisible(valueLabel);

    auto updateValueLabel = [this, &slider, &valueLabel, suffix]()
    {
        valueLabel.setText(juce::String(slider.getValue(), 1) + suffix,
                           juce::dontSendNotification);
    };

    updateValueLabel();
    slider.onValueChange = updateValueLabel;

    valueLabel.onTextChange = [&slider, &valueLabel, suffix]()
    {
        const juce::String text = valueLabel.getText().replace(suffix, "", true).trim();
        const double newVal = text.getDoubleValue();

        if (newVal >= slider.getMinimum() && newVal <= slider.getMaximum())
            slider.setValue(newVal, juce::sendNotificationAsync);
        else
            valueLabel.setText(juce::String(slider.getValue(), 1) + suffix,
                               juce::dontSendNotification);
    };
}

void ViveAudioProcessorEditor::syncSatModeSelector()
{
    if (auto* satParam = dynamic_cast<juce::AudioParameterChoice*>(
            audioProcessor.getAPVTS().getParameter(Params::IDs::SAT_MODE_ID)))
    {
        const int satIndex = satParam->getIndex();
        if (satSelector.getActiveMode() != satIndex)
            satSelector.setActiveMode(satIndex);
    }
}

void ViveAudioProcessorEditor::updatePresetStatus(const juce::String& presetName, bool isUserPreset)
{
    activePresetName = presetName;
    activePresetIsUser = isUserPreset;

    const juce::String source = isUserPreset ? "USER" : "FACTORY";
    presetStatusLabel.setText(source + " / " + presetName.toUpperCase(), juce::dontSendNotification);
}

void ViveAudioProcessorEditor::rebuildBackgroundCache()
{
    backgroundCacheDirty = false;

    const auto bounds = getLocalBounds();
    if (bounds.isEmpty())
    {
        backgroundCache = {};
        return;
    }

    backgroundCache = juce::Image(juce::Image::ARGB, bounds.getWidth(), bounds.getHeight(), true);
    juce::Graphics g(backgroundCache);

    const auto floatBounds = bounds.toFloat();
    const float W = floatBounds.getWidth();
    const float H = floatBounds.getHeight();
    const auto layout = makeEditorLayout(W, H);

    g.setColour(juce::Colour(0xff08060a));
    g.fillRoundedRectangle(floatBounds, 16.0f);

    {
        juce::ColourGradient bg(EditorPalette::dark2, W * 0.5f, H, EditorPalette::dark, W * 0.5f, 0.0f, true);
        g.setGradientFill(bg);
        g.fillRoundedRectangle(floatBounds.reduced(1.0f), 16.0f);
    }

    {
        juce::ColourGradient haze(EditorPalette::pink.withAlpha(0.08f), W * 0.08f, 0.0f,
                                  juce::Colours::transparentBlack, W * 0.46f, H * 0.38f, false);
        g.setGradientFill(haze);
        g.fillRoundedRectangle(floatBounds.reduced(2.0f), 16.0f);
    }

    {
        juce::ColourGradient lowGlow(EditorPalette::blue.withAlpha(0.06f), W * 0.74f, H,
                                     juce::Colours::transparentBlack, W * 0.42f, H * 0.45f, false);
        g.setGradientFill(lowGlow);
        g.fillRoundedRectangle(floatBounds.reduced(3.0f), 15.0f);
    }

    g.setColour(juce::Colour(0xff120c12));
    g.fillRect(0.0f, 0.0f, W, 58.0f);
    g.setColour(EditorPalette::cream.withAlpha(0.02f));
    g.fillRect(0.0f, 0.0f, W, 58.0f);

    {
        const float spineX = layout.visualizerFrame.getCentreX();
        g.setColour(EditorPalette::pink.withAlpha(0.08f));
        g.fillRoundedRectangle(spineX - 2.0f, 18.0f, 4.0f, H - 40.0f, 2.0f);
        g.setColour(EditorPalette::blue.withAlpha(0.06f));
        g.fillRoundedRectangle(spineX + 6.0f, 28.0f, 2.0f, H - 64.0f, 1.0f);
    }

    {
        const auto titleBlock = juce::Rectangle<float>(layout.headerBand.getX() + 16.0f, 10.0f, 214.0f, 40.0f);
        drawFloatingCard(g, titleBlock, 12.0f,
                         juce::Colour(0xff1a1119),
                         juce::Colour(0xff312639),
                         EditorPalette::pink.withAlpha(0.95f), 0.95f);

        const float readX = W - static_cast<float>(padX) - 104.0f;
        const auto ceilingCard = juce::Rectangle<float>(readX - 18.0f, 10.0f, 122.0f, 40.0f);
        drawFloatingCard(g, ceilingCard, 12.0f,
                         juce::Colour(0xff151015),
                         juce::Colour(0xff2e2332),
                         EditorPalette::blue.withAlpha(0.88f), 0.9f);
    }

    {
        const auto vizBounds = layout.visualizerFrame;
        drawFloatingCard(g, vizBounds, 16.0f,
                         juce::Colour(0xff120d16),
                         juce::Colour(0xff33273a),
                         EditorPalette::pink.withAlpha(0.88f), 1.0f);

        const auto inner = vizBounds.reduced(2.0f);
        g.setColour(EditorPalette::cream.withAlpha(0.05f));
        g.drawRoundedRectangle(inner.expanded(0.5f), 10.0f, 0.5f);

        constexpr int gridCols = 10;
        constexpr int gridRows = 5;
        g.setColour(EditorPalette::blue.withAlpha(0.030f));
        for (int col = 1; col < gridCols; ++col)
        {
            const float gx = inner.getX() + inner.getWidth() * col / gridCols;
            g.fillRect(gx, inner.getY(), 0.5f, inner.getHeight());
        }
        for (int row = 1; row < gridRows; ++row)
        {
            const float gy = inner.getY() + inner.getHeight() * row / gridRows;
            g.fillRect(inner.getX(), gy, inner.getWidth(), 0.5f);
        }

        g.setColour(EditorPalette::pink.withAlpha(0.08f));
        g.fillRoundedRectangle(inner.getCentreX() - 26.0f, inner.getY() - 6.0f,
                               52.0f, 6.0f, 3.0f);
    }

    g.setColour(juce::Colour(0xff0d090d).withAlpha(0.68f));
    g.fillRect(layout.controlStrip);
    g.setColour(EditorPalette::cream.withAlpha(0.03f));
    g.fillRect(layout.controlStrip.getX(), layout.controlStrip.getY(), layout.controlStrip.getWidth(), 0.5f);

    drawFloatingCard(g, layout.inputCard, 12.0f,
                     juce::Colour(0xff141015), juce::Colour(0xff2b2230), EditorPalette::blue.withAlpha(0.92f));
    drawFloatingCard(g, layout.outputCard, 12.0f,
                     juce::Colour(0xff150f16), juce::Colour(0xff312539), EditorPalette::pink.withAlpha(0.92f));
    drawFloatingCard(g, layout.satCard, 12.0f,
                     juce::Colour(0xff140d17), juce::Colour(0xff2d2232), EditorPalette::pink.withAlpha(0.84f));

    g.setColour(EditorPalette::blue.withAlpha(0.10f));
    g.fillRect(layout.inputCard.getX() + 8.0f, layout.inputCard.getY() + 8.0f, 32.0f, 2.0f);
    g.setColour(EditorPalette::pink.withAlpha(0.10f));
    g.fillRect(layout.outputCard.getX() + 8.0f, layout.outputCard.getY() + 8.0f, 32.0f, 2.0f);
    g.setColour(EditorPalette::pink.withAlpha(0.08f));
    g.fillRect(layout.satCard.getX() + 8.0f, layout.satCard.getY() + 8.0f, 48.0f, 2.0f);

    g.setFont(Fonts::mono(8.0f));
    g.setColour(EditorPalette::textGhost());
    g.drawText(JucePlugin_VersionString,
               bounds.reduced(padX).translated(0, -7),
               juce::Justification::bottomRight);
}

void ViveAudioProcessorEditor::showPresetMenu()
{
    juce::PopupMenu menu;
    juce::PopupMenu factoryMenu;
    juce::PopupMenu userMenu;

    const auto factoryNames = audioProcessor.getFactoryPresetNames();
    for (int i = 0; i < factoryNames.size(); ++i)
        factoryMenu.addItem(1000 + i, factoryNames[i]);

    const auto userNames = audioProcessor.getUserPresetNames();
    if (userNames.isEmpty())
        userMenu.addItem(2100, "No saved presets", false, false);
    else
    {
        for (int i = 0; i < userNames.size(); ++i)
            userMenu.addItem(2000 + i, userNames[i]);
    }

    menu.addSubMenu(Params::Branding::factoryPresetTitle, factoryMenu, true);
    menu.addSubMenu(Params::Branding::userPresetTitle, userMenu, true);
    menu.addSeparator();
    menu.addItem(3000, Params::Branding::savePresetLabel);
    menu.addItem(3001, Params::Branding::deletePresetLabel, activePresetIsUser && activePresetName.isNotEmpty());

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(presetButton),
                       [this](int result)
                       {
                           handlePresetMenuResult(result);
                       });
}

void ViveAudioProcessorEditor::handlePresetMenuResult(int result)
{
    if (result == 0 || result == 2100)
        return;

    if (result == 3000)
    {
        saveCurrentAsUserPreset();
        return;
    }

    if (result == 3001)
    {
        deleteCurrentUserPreset();
        return;
    }

    if (result >= 1000 && result < 2000)
    {
        loadFactoryPreset(result - 1000);
        return;
    }

    if (result >= 2000 && result < 3000)
    {
        const auto userNames = audioProcessor.getUserPresetNames();
        const int idx = result - 2000;
        if (idx >= 0 && idx < userNames.size())
            loadUserPreset(userNames[idx]);
    }
}

void ViveAudioProcessorEditor::saveCurrentAsUserPreset()
{
    auto* alert = new juce::AlertWindow("Save User Preset",
                                        "Enter a preset name:",
                                        juce::AlertWindow::NoIcon,
                                        this);
    alert->addTextEditor("presetName",
                         activePresetIsUser ? activePresetName : juce::String("New Preset"),
                         "Name:");
    alert->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    alert->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::Component::SafePointer<juce::AlertWindow> safeAlert(alert);
    alert->enterModalState(true,
                           juce::ModalCallbackFunction::create([this, safeAlert](int result)
                           {
                               if (result != 1 || safeAlert == nullptr)
                                   return;

                               const auto presetName = safeAlert->getTextEditorContents("presetName");
                               if (presetName.trim().isEmpty())
                                   return;

                               if (audioProcessor.saveUserPreset(presetName))
                                   updatePresetStatus(presetName, true);
                           }),
                           true);
}

void ViveAudioProcessorEditor::deleteCurrentUserPreset()
{
    if (! activePresetIsUser || activePresetName.isEmpty())
        return;

    if (! juce::AlertWindow::showOkCancelBox(
            juce::AlertWindow::WarningIcon,
            "Delete User Preset",
            "Delete \"" + activePresetName + "\"?",
            "Delete",
            "Cancel",
            this,
            nullptr))
    {
        return;
    }

    if (audioProcessor.deleteUserPreset(activePresetName))
        updatePresetStatus("Unsaved Changes", false);
}

void ViveAudioProcessorEditor::loadFactoryPreset(int presetIndex)
{
    if (! audioProcessor.loadFactoryPreset(presetIndex))
        return;

    const auto& names = audioProcessor.getFactoryPresetNames();
    const auto name = juce::isPositiveAndBelow(presetIndex, names.size()) ? names[presetIndex] : juce::String("Default");
    updatePresetStatus(name, false);
}

void ViveAudioProcessorEditor::loadUserPreset(const juce::String& presetName)
{
    if (! audioProcessor.loadUserPreset(presetName))
        return;

    updatePresetStatus(presetName, true);
}

//==============================================================================
ViveAudioProcessorEditor::ViveAudioProcessorEditor(ViveAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setLookAndFeel(&customLookAndFeel);
    audioProcessor.setVisualizer(&visualizer);

    boundsConstrainer.setMinimumSize(600, 380);
    boundsConstrainer.setMaximumSize(1400, 900);
    boundsConstrainer.setFixedAspectRatio(0.0);
    setResizable(true, true);
    setConstrainer(&boundsConstrainer);

    configureHeader();
    configureSaturationSection();
    addAndMakeVisible(visualizer);

    auto& apvts = audioProcessor.getAPVTS();

    configureKnob(inputGainSlider,  inputGainLabel,  inputGainValue,  "INPUT",   "inputGain",  " dB");
    configureKnob(driveSlider,     driveLabel,      driveValue,      "DRIVE",   "drive",      " %");
    configureKnob(ceilingSlider,   ceilingLabel,    ceilingValue,    "CEILING", "ceiling",    " dB");
    configureKnob(mixSlider,       mixLabel,        mixValue,        "MIX",     "mix",        " %");
    configureKnob(outputGainSlider,outputGainLabel, outputGainValue, "OUTPUT",  "outputGain", " dB");

    inputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, Params::IDs::INPUT_GAIN_ID, inputGainSlider);
    driveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, Params::IDs::DRIVE_ID, driveSlider);
    ceilingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, Params::IDs::CEILING_ID, ceilingSlider);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, Params::IDs::MIX_ID, mixSlider);
    outputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, Params::IDs::OUTPUT_GAIN_ID, outputGainSlider);

    const auto factoryNames = audioProcessor.getFactoryPresetNames();
    if (factoryNames.isEmpty())
    {
        updatePresetStatus(Params::Branding::nonePresetLabel, false);
    }
    else
    {
        const int genreIndex = juce::jlimit(0, factoryNames.size() - 1,
            (int) apvts.getRawParameterValue(Params::IDs::GENRE_ID)->load());
        updatePresetStatus(factoryNames[genreIndex], false);
    }

    isRestoringState = false;
    syncSatModeSelector();
    startTimerHz(30);
    setSize(860, 500);
}

ViveAudioProcessorEditor::~ViveAudioProcessorEditor()
{
    stopTimer();
    audioProcessor.setVisualizer(nullptr);
    setLookAndFeel(nullptr);
}

//==============================================================================
void ViveAudioProcessorEditor::timerCallback()
{
    headerCeilingDb = audioProcessor.getAPVTS()
        .getRawParameterValue(Params::IDs::CEILING_ID)->load();

    syncSatModeSelector();

    pulsePhase += 0.04f;
    if (pulsePhase > juce::MathConstants<float>::twoPi)
        pulsePhase -= juce::MathConstants<float>::twoPi;

    repaint();
}

//==============================================================================
void ViveAudioProcessorEditor::paint(juce::Graphics& g)
{
    if (backgroundCacheDirty || backgroundCache.isNull())
        rebuildBackgroundCache();

    g.drawImageAt(backgroundCache, 0, 0);

    namespace P = EditorPalette;
    const auto bounds = getLocalBounds().toFloat();
    const float W = bounds.getWidth();
    const float H = bounds.getHeight();

    const float readX = W - static_cast<float>(padX) - 104.0f;
    const auto ceilingCard = juce::Rectangle<float>(readX - 18.0f, 10.0f, 122.0f, 40.0f);

    g.setFont(Fonts::mono(7.5f, true));
    g.setColour(P::textTertiary());
    g.drawText("CEILING", ceilingCard.toNearestInt().reduced(10).withHeight(11),
               juce::Justification::centredRight);

    g.setFont(Fonts::mono(18.0f));
    g.setColour(P::textPrimary());
    g.drawText(juce::String(headerCeilingDb, 1) + " dB",
               ceilingCard.toNearestInt().reduced(10).withTop(18),
               juce::Justification::centredRight);
}

//==============================================================================
void ViveAudioProcessorEditor::resized()
{
    const auto layout = makeEditorLayout((float)getWidth(), (float)getHeight());

    titleLabel.setBounds((int)layout.headerBand.getX() + 28, 12, 190, 24);
    subtitleLabel.setBounds((int)layout.headerBand.getX() + 28, 34, 230, 12);

    const int presetButtonW = 86;
    const int presetStatusW = 180;
    const int ceilingCardW = 122;
    const int ceilingCardX = getWidth() - padX - ceilingCardW;
    const int presetButtonX = ceilingCardX - 12 - presetButtonW;
    const int presetStatusX = juce::jmax((int)layout.headerBand.getX() + 240,
                                          presetButtonX - presetStatusW - 10);

    presetStatusLabel.setBounds(presetStatusX, 18, presetStatusW, 16);
    presetButton.setBounds(presetButtonX, 16, presetButtonW, 18);

    visualizer.setBounds(layout.visualizerFrame.reduced(4.0f).toNearestInt());

    satSectionLabel.setBounds((int)layout.satCard.getX() + 12,
                              (int)layout.satCard.getY() + 10,
                              (int)layout.satCard.getWidth() - 24,
                              14);
    satSelector.setBounds((int)layout.satCard.getX() + 10,
                          (int)layout.satCard.getY() + 28,
                          (int)layout.satCard.getWidth() - 20,
                          (int)layout.satCard.getHeight() - 38);

    auto placeKnob = [] (juce::Slider& slider,
                         juce::Label& nameLbl,
                         juce::Label& valueLbl,
                         const juce::Rectangle<float>& card,
                         int index,
                         int count)
    {
        const float insetX = 10.0f;
        const float usableW = card.getWidth() - insetX * 2.0f;
        const float slotW = usableW / static_cast<float>(count);
        const float centerX = card.getX() + insetX + slotW * (static_cast<float>(index) + 0.5f);
        const float knobSize = juce::jlimit(42.0f, 56.0f, card.getHeight() - 42.0f);
        const float blockH = 12.0f + knobSize + 16.0f;
        const float blockY = card.getY() + (card.getHeight() - blockH) * 0.5f + 3.0f;
        const float labelW = slotW - 8.0f;
        const float labelX = centerX - labelW * 0.5f;

        nameLbl.setBounds((int)std::round(labelX), (int)std::round(blockY),
                          (int)std::round(labelW), 13);
        slider.setBounds((int)std::round(centerX - knobSize * 0.5f),
                         (int)std::round(blockY + 12.0f),
                         (int)std::round(knobSize),
                         (int)std::round(knobSize));
        valueLbl.setBounds((int)std::round(labelX), (int)std::round(blockY + 12.0f + knobSize),
                           (int)std::round(labelW), 18);
    };

    placeKnob(inputGainSlider,  inputGainLabel,  inputGainValue,  layout.inputCard,  0, 2);
    placeKnob(driveSlider,      driveLabel,      driveValue,      layout.inputCard,  1, 2);
    placeKnob(ceilingSlider,    ceilingLabel,    ceilingValue,    layout.outputCard, 0, 3);
    placeKnob(mixSlider,        mixLabel,        mixValue,        layout.outputCard, 1, 3);
    placeKnob(outputGainSlider, outputGainLabel, outputGainValue, layout.outputCard, 2, 3);

    backgroundCacheDirty = true;
}
