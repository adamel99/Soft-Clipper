#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Font helpers
//==============================================================================
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

//==============================================================================
ViveAudioProcessorEditor::ViveAudioProcessorEditor(ViveAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setLookAndFeel(&customLookAndFeel);

    // ── Resizable window ──────────────────────────────────────────────────
    boundsConstrainer.setMinimumSize(600, 380);
    boundsConstrainer.setMaximumSize(1400, 900);
    boundsConstrainer.setFixedAspectRatio(0.0);
    setResizable(true, true);
    setConstrainer(&boundsConstrainer);

    // ── Title ─────────────────────────────────────────────────────────────
    titleLabel.setText("VIVE", juce::dontSendNotification);
    titleLabel.setFont(Fonts::mono(22.0f, true));
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setColour(juce::Label::textColourId, EditorPalette::textPrimary());
    addAndMakeVisible(titleLabel);

    subtitleLabel.setText("INTELLIGENT CLIPPER  //  BY DOOMS",
                          juce::dontSendNotification);
    subtitleLabel.setFont(Fonts::mono(8.5f));
    subtitleLabel.setJustificationType(juce::Justification::centredLeft);
    subtitleLabel.setColour(juce::Label::textColourId, EditorPalette::textTertiary());
    addAndMakeVisible(subtitleLabel);

    auto& apvts = audioProcessor.getAPVTS();

    // ── Saturation ────────────────────────────────────────────────────────
    satSectionLabel.setText("SAT MODE", juce::dontSendNotification);
    satSectionLabel.setFont(Fonts::mono(7.5f, true));
    satSectionLabel.setColour(juce::Label::textColourId, EditorPalette::textTertiary());
    satSectionLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(satSectionLabel);
    addAndMakeVisible(satSelector);

    {
        auto* param = dynamic_cast<juce::AudioParameterChoice*>(
            apvts.getParameter(Params::IDs::SAT_MODE_ID));
        satSelector.setActiveMode(param ? param->getIndex() : 0);
    }

    satSelector.onModeChanged = [this](int newMode)
    {
        if (newMode == -1)
        {
            auto* driveParam = audioProcessor.getAPVTS().getParameter(Params::IDs::DRIVE_ID);
            if (driveParam)
                driveParam->setValueNotifyingHost(
                    driveParam->getNormalisableRange().convertTo0to1(0.0f));
        }
        else
        {
            auto* param = dynamic_cast<juce::AudioParameterChoice*>(
                audioProcessor.getAPVTS().getParameter(Params::IDs::SAT_MODE_ID));
            if (param)
                param->setValueNotifyingHost(
                    param->getNormalisableRange().convertTo0to1(
                        static_cast<float>(newMode)));
        }
    };

    // ── Visualizer ────────────────────────────────────────────────────────
    addAndMakeVisible(audioProcessor.getVisualizer());

    // ── Knob setup ────────────────────────────────────────────────────────
    auto setupKnob = [&](juce::Slider& s,
                         juce::Label&  nameLabel,
                         juce::Label&  valLabel,
                         const char*   name,
                         const char*   sliderName)
    {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        s.setName(sliderName);
        addAndMakeVisible(s);

        nameLabel.setText(name, juce::dontSendNotification);
        nameLabel.setFont(Fonts::mono(7.5f, true));
        nameLabel.setJustificationType(juce::Justification::centred);
        nameLabel.setColour(juce::Label::textColourId, EditorPalette::textTertiary());
        addAndMakeVisible(nameLabel);

        valLabel.setText("-", juce::dontSendNotification);
        valLabel.setFont(Fonts::mono(9.5f));
        valLabel.setJustificationType(juce::Justification::centred);
        valLabel.setColour(juce::Label::textColourId, EditorPalette::textSecondary());
        addAndMakeVisible(valLabel);
    };

    setupKnob(inputGainSlider, inputGainLabel, inputGainValue, "INPUT",   "inputGain");
    setupKnob(driveSlider,     driveLabel,     driveValue,     "DRIVE",   "drive");
    setupKnob(ceilingSlider,   ceilingLabel,   ceilingValue,   "CEILING", "ceiling");
    setupKnob(mixSlider,       mixLabel,       mixValue,       "MIX",     "mix");
    setupKnob(outputGainSlider,outputGainLabel,outputGainValue,"OUTPUT",  "outputGain");

    // ── APVTS attachments ─────────────────────────────────────────────────
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

    // ── Live value labels ─────────────────────────────────────────────────
    auto wireValue = [](juce::Slider& s, juce::Label& lbl, const juce::String& suffix)
    {
        lbl.setEditable(false, true, false);

        auto update = [&s, &lbl, suffix]()
        {
            lbl.setText(juce::String(s.getValue(), 1) + suffix,
                        juce::dontSendNotification);
        };
        update();

        s.onValueChange = update;

        lbl.onTextChange = [&s, &lbl, suffix]()
        {
            const juce::String text = lbl.getText().replace(suffix, "", true).trim();
            const double newVal = text.getDoubleValue();
            if (newVal >= s.getMinimum() && newVal <= s.getMaximum())
                s.setValue(newVal, juce::sendNotificationAsync);
            else
                lbl.setText(juce::String(s.getValue(), 1) + suffix,
                            juce::dontSendNotification);
        };
    };

    wireValue(inputGainSlider, inputGainValue,  " dB");
    wireValue(driveSlider,     driveValue,      " %");
    wireValue(ceilingSlider,   ceilingValue,    " dB");
    wireValue(mixSlider,       mixValue,        " %");
    wireValue(outputGainSlider,outputGainValue, " dB");

    isRestoringState = false;

    startTimerHz(30);
    setSize(860, 500);
}

ViveAudioProcessorEditor::~ViveAudioProcessorEditor()
{
    stopTimer();
    removeChildComponent(&audioProcessor.getVisualizer());
    setLookAndFeel(nullptr);
}

//==============================================================================
void ViveAudioProcessorEditor::timerCallback()
{
    headerCeilingDb = audioProcessor.getAPVTS()
        .getRawParameterValue(Params::IDs::CEILING_ID)->load();
    pulsePhase += 0.04f;
    if (pulsePhase > juce::MathConstants<float>::twoPi)
        pulsePhase -= juce::MathConstants<float>::twoPi;
    repaint();
}

//==============================================================================
void ViveAudioProcessorEditor::applyGenrePreset(int presetIndex)
{
    if (isRestoringState) return;
    if (presetIndex <= 0 || presetIndex >= Params::numGenrePresets) return;

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
            satParam->getNormalisableRange().convertTo0to1(
                static_cast<float>(satIndex)));
    }
    satSelector.setActiveMode(satIndex);
}

//==============================================================================
void ViveAudioProcessorEditor::paint(juce::Graphics& g)
{
    namespace P = EditorPalette;

    const auto  bounds = getLocalBounds().toFloat();
    const float W = bounds.getWidth();
    const float H = bounds.getHeight();
    const float hh = static_cast<float>(headerH);
    const float ch = static_cast<float>(controlsH);

    // ── 1. Base fill ──────────────────────────────────────────────────────
    g.setColour(P::dark);
    g.fillRoundedRectangle(bounds, 12.0f);

    // ── 2. Top ambient glow ───────────────────────────────────────────────
    {
        juce::ColourGradient topGlow(P::pink.withAlpha(0.09f), W * 0.5f, 0.0f,
                                     juce::Colours::transparentBlack, W * 0.5f, 120.0f,
                                     true);
        g.setGradientFill(topGlow);
        g.fillRoundedRectangle(bounds, 12.0f);
    }

    // ── 3. Header band ────────────────────────────────────────────────────
    {
        // Subtle header fill
        g.setColour(P::cream.withAlpha(0.018f));
        g.fillRect(0.0f, 0.0f, W, hh);

        // Left pink accent bar
        {
            juce::ColourGradient bar(P::pink.withAlpha(0.0f),  0.0f, hh * 0.18f,
                                     P::pink.withAlpha(0.95f), 0.0f, hh * 0.55f,
                                     false);
            bar.addColour(1.0, P::pink.withAlpha(0.05f));
            g.setGradientFill(bar);
            g.fillRect(0.0f, hh * 0.18f, 2.0f, hh * 0.72f);
        }

        // Header bottom rule
        {
            juce::ColourGradient rule(juce::Colours::transparentBlack, 0.0f, hh,
                                      P::cream.withAlpha(0.07f), W * 0.5f, hh,
                                      false);
            rule.addColour(1.0, juce::Colours::transparentBlack);
            g.setGradientFill(rule);
            g.fillRect(0.0f, hh - 0.5f, W, 0.5f);
        }

        // Ceiling readout — top right of header
        {
            const float readX = W - static_cast<float>(padX) - 92.0f;

            g.setFont(Fonts::mono(7.5f, true));
            g.setColour(P::textTertiary());
            g.drawText("CEILING",
                       static_cast<int>(readX), 7, 92, 12,
                       juce::Justification::centredRight);

            g.setFont(Fonts::mono(18.0f));
            g.setColour(P::textPrimary());
            g.drawText(juce::String(headerCeilingDb, 1) + " dB",
                       static_cast<int>(readX), 19, 92, 21,
                       juce::Justification::centredRight);

            // Animated underline — sin mapped to [0,1] to keep alpha in range
            const float underAlpha = 0.20f + 0.60f * (0.5f + 0.5f * std::sin(pulsePhase));
            g.setColour(P::pink.withAlpha(underAlpha));
            g.fillRect(readX + 92.0f - 32.0f, hh - 5.0f, 32.0f, 1.0f);

            // Status pips
            const float pipY  = hh * 0.5f - 3.0f;
            const float pip1X = W - static_cast<float>(padX) - 112.0f;
            for (int i = 0; i < 3; ++i)
            {
                const float px = pip1X + static_cast<float>(i) * 11.0f;
                const juce::Rectangle<float> pip(px, pipY, 6.0f, 6.0f);
                if (i == 0)
                {
                    g.setColour(P::pink);
                    g.fillEllipse(pip);
                    g.setColour(P::pink.withAlpha(0.40f));
                    g.drawEllipse(pip, 0.5f);
                }
                else
                {
                    g.setColour(P::cream.withAlpha(0.0f));
                    g.fillEllipse(pip);
                    g.setColour(P::cream.withAlpha(0.14f));
                    g.drawEllipse(pip, 0.5f);
                }
            }
        }
    }

    // ── 4. Controls strip background ─────────────────────────────────────
    {
        const float stripY = H - ch;

        // Top rule for controls strip
        {
            juce::ColourGradient rule(juce::Colours::transparentBlack, 0.0f, stripY,
                                      P::cream.withAlpha(0.07f), W * 0.5f, stripY,
                                      false);
            rule.addColour(1.0, juce::Colours::transparentBlack);
            g.setGradientFill(rule);
            g.fillRect(0.0f, stripY, W, 0.5f);
        }

        // Very subtle strip fill to separate from visualizer
        g.setColour(juce::Colour(0xff0d090d).withAlpha(0.30f));
        g.fillRect(0.0f, stripY, W, ch);

        // Divider between knobs and sat panel
        {
            const float divX = W - static_cast<float>(padX) - static_cast<float>(satPanelW) - 8.0f;
            juce::ColourGradient divGrad(
                juce::Colours::transparentBlack, divX, stripY + 10.0f,
                P::cream.withAlpha(0.09f),       divX, H * 0.5f + (stripY + ch) * 0.5f,
                false);
            divGrad.addColour(1.0, juce::Colours::transparentBlack);
            g.setGradientFill(divGrad);
            g.fillRect(divX, stripY + 12.0f, 0.5f, ch - 24.0f);
        }
    }

    // ── 5. Visualizer frosted border + grid ───────────────────────────────
    {
        const auto vizBounds = audioProcessor.getVisualizer().getBounds().toFloat();

        constexpr int gridCols = 10;
        constexpr int gridRows = 5;
        g.setColour(P::blue.withAlpha(0.035f));
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

        // Centre zero line
        const float midY = vizBounds.getCentreY();
        g.setColour(P::cream.withAlpha(0.055f));
        g.fillRect(vizBounds.getX(), midY - 0.25f, vizBounds.getWidth(), 0.5f);

        // Outer border
        g.setColour(P::cream.withAlpha(0.08f));
        g.drawRoundedRectangle(vizBounds.expanded(0.5f), 6.0f, 0.5f);
    }

    // ── 6. Outer plugin border ────────────────────────────────────────────
    g.setColour(P::cream.withAlpha(0.09f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 12.0f, 0.5f);

    g.setColour(P::cream.withAlpha(0.03f));
    g.drawRoundedRectangle(bounds.reduced(1.5f), 11.0f, 1.0f);

    // ── 7. Corner targeting marks ─────────────────────────────────────────
    {
        constexpr float cL = 9.0f;
        constexpr float cI = 7.0f;
        g.setColour(P::blue.withAlpha(0.25f));
        g.drawLine(cI,       cI,       cI + cL,  cI,       0.75f);
        g.drawLine(cI,       cI,       cI,       cI + cL,  0.75f);
        g.drawLine(W - cI,   cI,       W-cI-cL,  cI,       0.75f);
        g.drawLine(W - cI,   cI,       W-cI,     cI + cL,  0.75f);
        g.drawLine(cI,       H - cI,   cI + cL,  H-cI,     0.75f);
        g.drawLine(cI,       H - cI,   cI,       H-cI-cL,  0.75f);
        g.drawLine(W - cI,   H - cI,   W-cI-cL,  H-cI,     0.75f);
        g.drawLine(W - cI,   H - cI,   W-cI,     H-cI-cL,  0.75f);
    }

    // ── 8. Resize grip ────────────────────────────────────────────────────
    {
        constexpr float gS = 5.0f;
        constexpr float gO = 9.0f;
        g.setColour(P::cream.withAlpha(0.14f));
        for (int i = 0; i < 3; ++i)
        {
            const float offset = gO + (float)i * gS;
            g.drawLine(W - offset, H - gO, W - gO, H - offset, 0.75f);
        }
    }

    // ── 9. Version string ─────────────────────────────────────────────────
    g.setFont(Fonts::mono(8.0f));
    g.setColour(P::textGhost());
    g.drawText("v1.0.0",
               getLocalBounds().reduced(padX).translated(0, -5),
               juce::Justification::bottomRight);
}

//==============================================================================
void ViveAudioProcessorEditor::resized()
{
    const int W = getWidth();
    const int H = getHeight();

    // ── Header ────────────────────────────────────────────────────────────
    // Title and subtitle sit left of header, inset from accent bar
    titleLabel.setBounds(padX + 10, 7, 200, 26);
    subtitleLabel.setBounds(padX + 10, 32, 340, 12);

    // ── Visualizer ────────────────────────────────────────────────────────
    // Fills the space between header and controls strip
    const int vizX = padX;
    const int vizY = headerH + padY;
    const int vizW = W - padX * 2;
    const int vizH = H - vizY - controlsH - padY;
    audioProcessor.getVisualizer().setBounds(vizX, vizY, vizW, vizH);

    // ── Controls strip ────────────────────────────────────────────────────
    const int stripY = H - controlsH;

    // SAT section label + grid on the right
    const int satLabelH  = 13;
    const int satLabelY  = stripY + 8;
    const int satGridY   = satLabelY + satLabelH + 4;
    const int satGridH   = controlsH - satLabelH - 20;
    const int satRight   = W - padX;
    const int satLeft    = satRight - satPanelW;

    satSectionLabel.setBounds(satLeft, satLabelY, satPanelW, satLabelH);
    satSelector.setBounds(satLeft, satGridY, satPanelW, satGridH);

    // ── Knobs — spread evenly in the remaining left area ──────────────────
    constexpr int numKnobs = 5;

    // Knob area: from padX to just before the divider
    const int knobAreaX = padX;
    const int knobAreaW = satLeft - 20 - padX; // 20px gap before divider
    const int slotW     = knobAreaW / numKnobs;

    // Vertical layout within each slot
    const int nameH    = 12;
    const int valueH   = 16;
    const int knobMax  = controlsH - nameH - valueH - 18;
    const int knobSize = juce::jlimit(32, 52, knobMax);

    // Vertically centre the [name + knob + value] block in the strip
    const int blockH   = nameH + knobSize + valueH;
    const int blockY   = stripY + (controlsH - blockH) / 2;

    auto placeKnob = [&](juce::Slider& s, juce::Label& nameLbl,
                         juce::Label& valLbl, int idx)
    {
        const int slotCX = knobAreaX + idx * slotW + slotW / 2;
        const int labelW = slotW - 8;
        const int labelX = slotCX - labelW / 2;

        nameLbl.setBounds(labelX,               blockY,                           labelW, nameH);
        s.setBounds      (slotCX - knobSize / 2, blockY + nameH,                  knobSize, knobSize);
        valLbl.setBounds (labelX,               blockY + nameH + knobSize,        labelW, valueH);
    };

    placeKnob(inputGainSlider, inputGainLabel, inputGainValue, 0);
    placeKnob(driveSlider,     driveLabel,     driveValue,     1);
    placeKnob(ceilingSlider,   ceilingLabel,   ceilingValue,   2);
    placeKnob(mixSlider,       mixLabel,       mixValue,       3);
    placeKnob(outputGainSlider,outputGainLabel,outputGainValue,4);
}
