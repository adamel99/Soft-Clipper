#include "PluginProcessor.h"
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
    titleLabel.setText("VIVE", juce::dontSendNotification);
    titleLabel.setFont(Fonts::mono(24.0f, true));
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setColour(juce::Label::textColourId, EditorPalette::textPrimary());
    addAndMakeVisible(titleLabel);

    subtitleLabel.setText("SATURATOR  //  BY DOOMS", juce::dontSendNotification);
    subtitleLabel.setFont(Fonts::mono(8.5f));
    subtitleLabel.setJustificationType(juce::Justification::centredLeft);
    subtitleLabel.setColour(juce::Label::textColourId, EditorPalette::textTertiary());
    addAndMakeVisible(subtitleLabel);
}

void ViveAudioProcessorEditor::configureSaturationSection()
{
    satSectionLabel.setText("SATURATION MODE", juce::dontSendNotification);
    satSectionLabel.setFont(Fonts::mono(7.5f, true));
    satSectionLabel.setColour(juce::Label::textColourId, EditorPalette::textTertiary());
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

        if (newMode == -1)
        {
            if (auto* driveParam = apvts.getParameter(Params::IDs::DRIVE_ID))
                driveParam->setValueNotifyingHost(
                    driveParam->getNormalisableRange().convertTo0to1(0.0f));
            return;
        }

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
    nameLabel.setFont(Fonts::mono(7.5f, true));
    nameLabel.setJustificationType(juce::Justification::centred);
    nameLabel.setColour(juce::Label::textColourId, EditorPalette::textTertiary());
    addAndMakeVisible(nameLabel);

    valueLabel.setText("-", juce::dontSendNotification);
    valueLabel.setFont(Fonts::mono(9.5f));
    valueLabel.setJustificationType(juce::Justification::centred);
    valueLabel.setColour(juce::Label::textColourId, EditorPalette::textSecondary());
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

    configureKnob(inputGainSlider, inputGainLabel, inputGainValue, "INPUT",   "inputGain",  " dB");
    configureKnob(driveSlider,     driveLabel,     driveValue,     "DRIVE",   "drive",      " %");
    configureKnob(ceilingSlider,   ceilingLabel,   ceilingValue,   "CEILING", "ceiling",    " dB");
    configureKnob(mixSlider,       mixLabel,       mixValue,       "MIX",     "mix",        " %");
    configureKnob(outputGainSlider,outputGainLabel,outputGainValue,"OUTPUT",  "outputGain", " dB");

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
void ViveAudioProcessorEditor::applyGenrePreset(int presetIndex)
{
    if (isRestoringState)
        return;

    if (presetIndex <= 0 || presetIndex >= Params::numGenrePresets)
        return;

    const auto& preset = Params::genrePresets[presetIndex];
    auto& apvts = audioProcessor.getAPVTS();

    auto setParam = [&](const char* id, float value)
    {
        if (auto* param = apvts.getParameter(id))
            param->setValueNotifyingHost(
                param->getNormalisableRange().convertTo0to1(value));
    };

    setParam(Params::IDs::INPUT_GAIN_ID, preset.inputGainDb);
    setParam(Params::IDs::DRIVE_ID,      preset.drivePercent);
    setParam(Params::IDs::CEILING_ID,    preset.ceilingDb);
    setParam(Params::IDs::OUTPUT_GAIN_ID,preset.outputGainDb);
    setParam(Params::IDs::MIX_ID,        preset.mixPercent);

    const int satIndex = static_cast<int>(preset.satMode);
    if (auto* satParam = dynamic_cast<juce::AudioParameterChoice*>(
            apvts.getParameter(Params::IDs::SAT_MODE_ID)))
    {
        satParam->setValueNotifyingHost(
            satParam->getNormalisableRange().convertTo0to1(static_cast<float>(satIndex)));
    }

    satSelector.setActiveMode(satIndex);
}

//==============================================================================
void ViveAudioProcessorEditor::paint(juce::Graphics& g)
{
    namespace P = EditorPalette;

    const auto bounds = getLocalBounds().toFloat();
    const float W = bounds.getWidth();
    const float H = bounds.getHeight();
    const auto layout = makeEditorLayout(W, H);

    g.setColour(juce::Colour(0xff08060a));
    g.fillRoundedRectangle(bounds, 16.0f);

    {
        juce::ColourGradient bg(P::dark2, W * 0.5f, H, P::dark, W * 0.5f, 0.0f, true);
        g.setGradientFill(bg);
        g.fillRoundedRectangle(bounds.reduced(1.0f), 16.0f);
    }

    {
        juce::ColourGradient haze(P::pink.withAlpha(0.08f), W * 0.08f, 0.0f,
                                  juce::Colours::transparentBlack, W * 0.46f, H * 0.38f, false);
        g.setGradientFill(haze);
        g.fillRoundedRectangle(bounds.reduced(2.0f), 16.0f);
    }

    {
        juce::ColourGradient lowGlow(P::blue.withAlpha(0.06f), W * 0.74f, H,
                                     juce::Colours::transparentBlack, W * 0.42f, H * 0.45f, false);
        g.setGradientFill(lowGlow);
        g.fillRoundedRectangle(bounds.reduced(3.0f), 15.0f);
    }

    g.setColour(juce::Colour(0xff120c12));
    g.fillRect(0.0f, 0.0f, W, 58.0f);
    g.setColour(P::cream.withAlpha(0.02f));
    g.fillRect(0.0f, 0.0f, W, 58.0f);

    {
        const float spineX = layout.visualizerFrame.getCentreX();
        g.setColour(P::pink.withAlpha(0.08f));
        g.fillRoundedRectangle(spineX - 2.0f, 18.0f, 4.0f, H - 40.0f, 2.0f);
        g.setColour(P::blue.withAlpha(0.06f));
        g.fillRoundedRectangle(spineX + 6.0f, 28.0f, 2.0f, H - 64.0f, 1.0f);
    }

    {
        const auto titleBlock = juce::Rectangle<float>(layout.headerBand.getX() + 16.0f, 10.0f, 214.0f, 40.0f);
        drawFloatingCard(g, titleBlock, 12.0f,
                         juce::Colour(0xff1a1119),
                         juce::Colour(0xff312639),
                         P::pink.withAlpha(0.95f), 0.95f);

        const float readX = W - static_cast<float>(padX) - 104.0f;
        const auto ceilingCard = juce::Rectangle<float>(readX - 18.0f, 10.0f, 122.0f, 40.0f);
        drawFloatingCard(g, ceilingCard, 12.0f,
                         juce::Colour(0xff151015),
                         juce::Colour(0xff2e2332),
                         P::blue.withAlpha(0.88f), 0.9f);

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

    {
        drawFloatingCard(g, layout.visualizerFrame, 16.0f,
                         juce::Colour(0xff120d16),
                         juce::Colour(0xff33273a),
                         P::pink.withAlpha(0.88f), 1.0f);

        const auto vizBounds = visualizer.getBounds().toFloat().reduced(2.0f);
        g.setColour(P::cream.withAlpha(0.05f));
        g.drawRoundedRectangle(vizBounds.expanded(0.5f), 10.0f, 0.5f);

        constexpr int gridCols = 10;
        constexpr int gridRows = 5;
        g.setColour(P::blue.withAlpha(0.030f));
        for (int col = 1; col < gridCols; ++col)
        {
            const float gx = vizBounds.getX() + vizBounds.getWidth() * col / gridCols;
            g.fillRect(gx, vizBounds.getY(), 0.5f, vizBounds.getHeight());
        }
        for (int row = 1; row < gridRows; ++row)
        {
            const float gy = vizBounds.getY() + vizBounds.getHeight() * row / gridRows;
            g.fillRect(vizBounds.getX(), gy, vizBounds.getWidth(), 0.5f);
        }

        g.setColour(P::pink.withAlpha(0.08f));
        g.fillRoundedRectangle(vizBounds.getCentreX() - 26.0f, vizBounds.getY() - 6.0f,
                               52.0f, 6.0f, 3.0f);
    }

    g.setColour(juce::Colour(0xff0d090d).withAlpha(0.68f));
    g.fillRect(layout.controlStrip);
    g.setColour(P::cream.withAlpha(0.03f));
    g.fillRect(layout.controlStrip.getX(), layout.controlStrip.getY(), layout.controlStrip.getWidth(), 0.5f);

    drawFloatingCard(g, layout.inputCard, 12.0f,
                     juce::Colour(0xff141015), juce::Colour(0xff2b2230), P::blue.withAlpha(0.92f));
    drawFloatingCard(g, layout.outputCard, 12.0f,
                     juce::Colour(0xff150f16), juce::Colour(0xff312539), P::pink.withAlpha(0.92f));
    drawFloatingCard(g, layout.satCard, 12.0f,
                     juce::Colour(0xff140d17), juce::Colour(0xff2d2232), P::pink.withAlpha(0.84f));

    auto drawGroupLabel = [&g] (const juce::Rectangle<float>& card,
                                const juce::String& title,
                                const juce::String& subtitle,
                                const juce::Colour& accent)
    {
        g.setFont(Fonts::mono(7.5f, true));
        g.setColour(accent.withAlpha(0.95f));
        g.drawText(title, card.toNearestInt().reduced(12).withHeight(12),
                   juce::Justification::centredLeft);

        g.setFont(Fonts::mono(7.0f));
        g.setColour(P::textTertiary());
        g.drawText(subtitle, card.toNearestInt().reduced(12).withTop(12).withHeight(12),
                   juce::Justification::centredLeft);
    };

    drawGroupLabel(layout.inputCard,  "INPUT / DRIVE",  "GAIN SHAPE",  P::blue);
    drawGroupLabel(layout.outputCard, "CEILING / MIX",  "OUTPUT STAGE", P::pink);

    g.setFont(Fonts::mono(8.0f));
    g.setColour(P::textGhost());
    g.drawText(JucePlugin_VersionString,
               getLocalBounds().reduced(padX).translated(0, -7),
               juce::Justification::bottomRight);
}

//==============================================================================
void ViveAudioProcessorEditor::resized()
{
    const auto layout = makeEditorLayout((float)getWidth(), (float)getHeight());

    titleLabel.setBounds((int)layout.headerBand.getX() + 28, 12, 160, 24);
    subtitleLabel.setBounds((int)layout.headerBand.getX() + 28, 34, 210, 12);

    visualizer.setBounds(layout.visualizerFrame.reduced(4.0f).toNearestInt());

    satSectionLabel.setBounds((int)layout.satCard.getX() + 12,
                              (int)layout.satCard.getY() + 10,
                              (int)layout.satCard.getWidth() - 24,
                              12);
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
                          (int)std::round(labelW), 12);
        slider.setBounds((int)std::round(centerX - knobSize * 0.5f),
                         (int)std::round(blockY + 12.0f),
                         (int)std::round(knobSize),
                         (int)std::round(knobSize));
        valueLbl.setBounds((int)std::round(labelX), (int)std::round(blockY + 12.0f + knobSize),
                           (int)std::round(labelW), 16);
    };

    placeKnob(inputGainSlider,  inputGainLabel,  inputGainValue,  layout.inputCard,  0, 2);
    placeKnob(driveSlider,      driveLabel,      driveValue,      layout.inputCard,  1, 2);
    placeKnob(ceilingSlider,    ceilingLabel,    ceilingValue,    layout.outputCard, 0, 3);
    placeKnob(mixSlider,        mixLabel,        mixValue,        layout.outputCard, 1, 3);
    placeKnob(outputGainSlider, outputGainLabel, outputGainValue, layout.outputCard, 2, 3);
}
