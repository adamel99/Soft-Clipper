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

    static const juce::String& roundedName()
    {
        static const juce::String name = []()
        {
            const auto available = juce::Font::findAllTypefaceNames();
            if (available.contains("Arial Rounded MT Bold")) return juce::String("Arial Rounded MT Bold");
            if (available.contains("Avenir Next"))           return juce::String("Avenir Next");
            return juce::String(juce::Font::getDefaultSansSerifFontName());
        }();
        return name;
    }

    static juce::Font rounded(float height, bool bold = true)
    {
        return juce::Font(juce::FontOptions()
            .withName(roundedName())
            .withHeight(height)
            .withStyle(bold ? "Bold" : "Regular"));
    }
}

namespace
{
    struct EditorLayout
    {
        juce::Rectangle<float> headerBand;
        juce::Rectangle<float> visualizerFrame;
        juce::Rectangle<float> controlStrip;
        juce::Rectangle<float> inputCard;
        juce::Rectangle<float> mainCard;
        juce::Rectangle<float> outputCard;
        juce::Rectangle<float> modeStrip;
    };

    static EditorLayout makeEditorLayout(float W, float H)
    {
        constexpr float side = 32.0f;
        constexpr float headerH = 74.0f;
        constexpr float controlsH = 286.0f;
        constexpr float gap = 44.0f;

        EditorLayout layout;
        layout.headerBand = { 0.0f, 0.0f, W, headerH };
        layout.controlStrip = { side, H - controlsH - 26.0f, W - side * 2.0f, controlsH };
        const float visualizerY = headerH + 16.0f;
        const float visualizerBottom = layout.controlStrip.getY() - gap;
        layout.visualizerFrame = { side, visualizerY, W - side * 2.0f,
                                   juce::jmax(150.0f, visualizerBottom - visualizerY) };
        const float sideW = layout.controlStrip.getWidth() * 0.255f;
        const float overlap = 16.0f;
        const float mainW = layout.controlStrip.getWidth() - sideW * 2.0f + overlap * 2.0f;
        layout.inputCard = { layout.controlStrip.getX(), layout.controlStrip.getY(), sideW, controlsH };
        layout.mainCard = { layout.inputCard.getRight() - overlap, layout.controlStrip.getY(), mainW, controlsH };
        layout.outputCard = { layout.mainCard.getRight() - overlap, layout.controlStrip.getY(), sideW, controlsH };
        layout.modeStrip = { layout.mainCard.getX() + 18.0f, layout.controlStrip.getBottom() - 48.0f,
                             layout.mainCard.getWidth() - 36.0f, 34.0f };
        return layout;
    }


}

//==============================================================================
void ViveAudioProcessorEditor::configureHeader()
{
    titleLabel.setText("VIVE", juce::dontSendNotification);
    titleLabel.setFont(Fonts::rounded(30.0f));
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xff342337));
    addAndMakeVisible(titleLabel);

    subtitleLabel.setText(Params::Branding::descriptor.toUpperCase(), juce::dontSendNotification);
    subtitleLabel.setFont(Fonts::rounded(10.0f));
    subtitleLabel.setJustificationType(juce::Justification::centredLeft);
    subtitleLabel.setColour(juce::Label::textColourId, juce::Colour(0xff867381));
    addAndMakeVisible(subtitleLabel);

    presetStatusLabel.setFont(Fonts::rounded(9.5f));
    presetStatusLabel.setJustificationType(juce::Justification::centredLeft);
    presetStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff71576a));
    addAndMakeVisible(presetStatusLabel);

    presetButton.setButtonText(Params::Branding::presetButton.toUpperCase());
    presetButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffe8c6d5));
    presetButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff43223d));
    presetButton.setColour(juce::TextButton::textColourOnId, juce::Colour(0xff43223d));
    presetButton.onClick = [this]
    {
        showPresetMenu();
    };
    addAndMakeVisible(presetButton);
}

void ViveAudioProcessorEditor::configureSaturationSection()
{
    satSectionLabel.setText("HARMONIC CHARACTER", juce::dontSendNotification);
    satSectionLabel.setFont(Fonts::rounded(9.5f));
    satSectionLabel.setColour(juce::Label::textColourId, juce::Colour(0xff573047));
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
    nameLabel.setFont(Fonts::rounded(11.5f));
    nameLabel.setJustificationType(juce::Justification::centred);
    nameLabel.setColour(juce::Label::textColourId, juce::Colour(0xfffff6fb));
    nameLabel.setColour(juce::Label::backgroundColourId, juce::Colour(0xff38223b));
    nameLabel.setColour(juce::Label::outlineColourId, juce::Colour(0xff8e5277));
    nameLabel.setOpaque(true);
    addAndMakeVisible(nameLabel);

    valueLabel.setText("-", juce::dontSendNotification);
    valueLabel.setFont(Fonts::mono(11.5f, true));
    valueLabel.setJustificationType(juce::Justification::centred);
    valueLabel.setColour(juce::Label::textColourId, juce::Colour(0xff38223b));
    valueLabel.setColour(juce::Label::backgroundColourId, juce::Colour(0xfffff4f8));
    valueLabel.setColour(juce::Label::outlineColourId, juce::Colour(0xffc780a1));
    valueLabel.setOpaque(true);
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
    if (bounds.isEmpty()) { backgroundCache = {}; return; }

    backgroundCache = juce::Image(juce::Image::ARGB, bounds.getWidth(), bounds.getHeight(), true);
    juce::Graphics g(backgroundCache);
    const float W = (float) bounds.getWidth();
    const float H = (float) bounds.getHeight();
    const auto layout = makeEditorLayout(W, H);

    juce::ColourGradient paper(juce::Colour(0xfff7f3f5), 0.0f, 0.0f,
                               juce::Colour(0xffe9e1e7), W, H, false);
    g.setGradientFill(paper);
    g.fillAll();

    const auto readout = juce::Rectangle<float>(W - padX - 122.0f, 14.0f, 122.0f, 46.0f);
    g.setColour(juce::Colour(0xffd7cad4));
    g.fillRoundedRectangle(readout.translated(0.0f, 2.0f), 9.0f);
    g.setColour(juce::Colour(0xfffaf6f8));
    g.fillRoundedRectangle(readout, 9.0f);
    g.setColour(juce::Colour(0xffd6c6d2));
    g.drawRoundedRectangle(readout.reduced(0.5f), 9.0f, 1.0f);

    // Raised graphite case with a deep side wall; the glass stays dark and inset.
    const auto meterHousing = layout.visualizerFrame.expanded(5.0f);
    g.setColour(juce::Colour(0x8806040a));
    g.fillRoundedRectangle(meterHousing.translated(0.0f, 14.0f).expanded(4.0f, 2.0f), 21.0f);
    g.setColour(juce::Colour(0xff09080d));
    g.fillRoundedRectangle(meterHousing.translated(0.0f, 9.0f), 20.0f);
    juce::ColourGradient housing(juce::Colour(0xff4d4852), meterHousing.getX(), meterHousing.getY(),
                                 juce::Colour(0xff19171d), meterHousing.getX(), meterHousing.getBottom(), false);
    g.setGradientFill(housing);
    g.fillRoundedRectangle(meterHousing, 19.0f);
    g.setColour(juce::Colour(0xff77717a).withAlpha(0.48f));
    g.drawRoundedRectangle(meterHousing.reduced(0.7f), 18.3f, 1.0f);

    g.setColour(juce::Colour(0xff0b0a10));
    g.fillRoundedRectangle(layout.visualizerFrame, 14.0f);
    g.setColour(juce::Colour(0xff756d78).withAlpha(0.42f));
    g.drawRoundedRectangle(layout.visualizerFrame.reduced(0.6f), 13.5f, 1.0f);

    // Small flush retainers make the glass read as a panel seated in a case.
    auto drawCaseRivet = [&g](float cx, float cy)
    {
        constexpr float r = 3.2f;
        g.setColour(juce::Colour(0xff09080d));
        g.fillEllipse(cx - r, cy - r + 1.3f, r * 2.0f, r * 2.0f);
        juce::ColourGradient steel(juce::Colour(0xffc5bec6), cx - r, cy - r,
                                   juce::Colour(0xff514c56), cx + r, cy + r, false);
        g.setGradientFill(steel);
        g.fillEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f);
        g.setColour(juce::Colour(0xff221f27));
        g.drawEllipse(cx - r + 0.7f, cy - r + 0.7f, r * 1.6f, r * 1.6f, 0.65f);
        g.setColour(juce::Colour(0xffeeeaf0).withAlpha(0.76f));
        g.drawLine(cx - 1.5f, cy + 0.7f, cx + 1.5f, cy - 0.7f, 0.8f);
    };
    drawCaseRivet(layout.visualizerFrame.getX() + 4.0f, layout.visualizerFrame.getY() + 7.0f);
    drawCaseRivet(layout.visualizerFrame.getRight() - 4.0f, layout.visualizerFrame.getY() + 7.0f);
    drawCaseRivet(layout.visualizerFrame.getX() + 4.0f, layout.visualizerFrame.getBottom() - 7.0f);
    drawCaseRivet(layout.visualizerFrame.getRight() - 4.0f, layout.visualizerFrame.getBottom() - 7.0f);

    const auto input = layout.inputCard;
    const auto output = layout.outputCard;
    auto drawScrew = [&g](float cx, float cy, float r)
    {
        g.setColour(juce::Colour(0x880d0812));
        g.fillEllipse(cx - r, cy - r + 2.0f, r * 2.0f, r * 2.0f);
        g.setColour(juce::Colour(0xff4a354c));
        g.fillEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f);
        juce::ColourGradient metal(juce::Colour(0xfffbffff), cx - r * 0.55f, cy - r * 0.7f,
                                   juce::Colour(0xff716878), cx + r * 0.7f, cy + r * 0.8f, false);
        g.setGradientFill(metal);
        g.fillEllipse(cx - r * 0.78f, cy - r * 0.78f, r * 1.56f, r * 1.56f);
        g.setColour(juce::Colour(0xff5a4d61));
        g.drawEllipse(cx - r * 0.78f, cy - r * 0.78f, r * 1.56f, r * 1.56f, 0.8f);
        g.setColour(juce::Colour(0xff493d4d));
        g.drawLine(cx - r * 0.36f, cy + r * 0.28f, cx + r * 0.36f, cy - r * 0.28f, 1.2f);
        g.setColour(juce::Colour(0xfffaffff).withAlpha(0.84f));
        g.drawLine(cx - r * 0.32f, cy + r * 0.20f, cx + r * 0.30f, cy - r * 0.30f, 0.55f);
    };

    // Cool glass input wing, partially tucked under the center pedal.
    g.setColour(juce::Colour(0xff45569c));
    g.fillRoundedRectangle(input.translated(0.0f, 11.0f).expanded(2.0f, 0.0f), 20.0f);
    juce::ColourGradient glass(juce::Colour(0xffa9ffff), input.getX(), input.getY(),
                               juce::Colour(0xff6953c6), input.getRight(), input.getBottom(), false);
    g.setGradientFill(glass);
    g.fillRoundedRectangle(input, 18.0f);
    g.setColour(juce::Colour(0xffe6ffff).withAlpha(0.96f));
    g.drawRoundedRectangle(input.reduced(0.7f), 17.5f, 1.5f);
    g.setColour(juce::Colour(0xff2c3779).withAlpha(0.68f));
    g.drawRoundedRectangle(input.reduced(3.0f), 15.5f, 1.0f);
    g.setColour(juce::Colour(0xfff5ffff).withAlpha(0.68f));
    g.drawLine(input.getX() + 24.0f, input.getY() + 2.0f,
               input.getRight() - 26.0f, input.getY() + 2.0f, 1.2f);
    juce::Random bubbles(9041);
    for (int i = 0; i < 32; ++i)
    {
        const float d = 12.0f + bubbles.nextFloat() * 23.0f;
        const float x = input.getX() + 6.0f + bubbles.nextFloat() * (input.getWidth() - d - 12.0f);
        const float y = input.getY() + 8.0f + bubbles.nextFloat() * (input.getHeight() - d - 16.0f);
        g.setColour(juce::Colour(0x99302583));
        g.fillEllipse(x + 1.5f, y + 3.0f, d, d);
        juce::ColourGradient dome(juce::Colour(0xffeaffff), x + d * 0.28f, y + d * 0.20f,
                                  (i % 2 == 0) ? juce::Colour(0xff29d4e7) : juce::Colour(0xff8f79f4),
                                  x + d * 0.62f, y + d * 0.82f, true);
        g.setGradientFill(dome);
        g.fillEllipse(x, y, d, d);
        g.setColour(juce::Colour(0xbaffffff));
        g.drawEllipse(x + 0.6f, y + 0.6f, d - 1.2f, d - 1.2f, 1.1f);
        g.setColour(juce::Colour(0xffffffff).withAlpha(0.9f));
        g.fillEllipse(x + d * 0.20f, y + d * 0.16f, d * 0.30f, d * 0.14f);
    }

    // Deep moss output wing with a small branching signal motif.
    g.setColour(juce::Colour(0xff14291f));
    g.fillRoundedRectangle(output.translated(0.0f, 11.0f).expanded(2.0f, 0.0f), 20.0f);
    juce::ColourGradient moss(juce::Colour(0xff80a940), output.getX(), output.getY(),
                              juce::Colour(0xff173a2b), output.getRight(), output.getBottom(), false);
    g.setGradientFill(moss);
    g.fillRoundedRectangle(output, 18.0f);
    g.setColour(juce::Colour(0xffd2f29b).withAlpha(0.92f));
    g.drawRoundedRectangle(output.reduced(0.7f), 17.5f, 1.5f);
    g.setColour(juce::Colour(0xff26442d).withAlpha(0.9f));
    g.drawRoundedRectangle(output.reduced(3.0f), 15.5f, 1.0f);
    g.setColour(juce::Colour(0xffefffc2).withAlpha(0.70f));
    g.drawLine(output.getX() + 24.0f, output.getY() + 2.0f,
               output.getRight() - 26.0f, output.getY() + 2.0f, 1.2f);

    juce::Random mossSpeckle(5183);
    for (int i = 0; i < 150; ++i)
    {
        const float d = 3.5f + mossSpeckle.nextFloat() * 7.0f;
        const float x = output.getX() + 6.0f + mossSpeckle.nextFloat() * (output.getWidth() - d - 12.0f);
        const float y = output.getY() + 142.0f + mossSpeckle.nextFloat() * (94.0f - d);
        g.setColour(juce::Colour(0xff10291e).withAlpha(0.72f));
        g.fillEllipse(x + 1.0f, y + 2.0f, d, d);
        juce::ColourGradient tuft(juce::Colour(0xffc4ed73), x + d * 0.25f, y + d * 0.12f,
                                  juce::Colour(0xff578b37), x + d * 0.72f, y + d * 0.88f, true);
        g.setGradientFill(tuft);
        g.fillEllipse(x, y, d, d);
        g.setColour(juce::Colour(0xfff0ffb2).withAlpha(0.78f));
        g.fillEllipse(x + d * 0.23f, y + d * 0.16f, d * 0.30f, d * 0.17f);
    }
    {
        juce::Path branch;
        branch.startNewSubPath(output.getX() + 20.0f, output.getY() + 247.0f);
        branch.cubicTo(output.getX() + 42.0f, output.getY() + 210.0f,
                       output.getX() + 54.0f, output.getY() + 193.0f,
                       output.getX() + 91.0f, output.getY() + 171.0f);
        branch.cubicTo(output.getX() + 118.0f, output.getY() + 155.0f,
                       output.getX() + 130.0f, output.getY() + 162.0f,
                       output.getX() + 152.0f, output.getY() + 146.0f);
        g.setColour(juce::Colour(0xaa0b2618));
        g.strokePath(branch, juce::PathStrokeType(5.0f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
        g.setColour(juce::Colour(0xffd9ff87));
        g.strokePath(branch, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
        for (const auto point : { juce::Point<float>(output.getX() + 91.0f, output.getY() + 171.0f),
                                  juce::Point<float>(output.getX() + 152.0f, output.getY() + 146.0f) })
        {
            g.setColour(juce::Colour(0x66d9ff82));
            g.fillEllipse(point.x - 5.0f, point.y - 5.0f, 10.0f, 10.0f);
            g.setColour(juce::Colour(0xffeaffb5));
            g.fillEllipse(point.x - 2.5f, point.y - 2.5f, 5.0f, 5.0f);
        }
    }

    const auto plate = layout.mainCard;
    g.setColour(juce::Colour(0xff7e285e));
    g.fillRoundedRectangle(plate.translated(0.0f, 12.0f).expanded(3.0f, 1.0f), 25.0f);
    g.setColour(juce::Colour(0xfffbc2dc));
    g.fillRoundedRectangle(plate.translated(0.0f, 5.0f), 23.0f);

    // A scalloped, soft edge suggests the plush texture of the reference pedal.
    juce::Random fibers(4019);
    auto drawTuft = [&g, &fibers](float x, float y, int seed)
    {
        const float r = 3.0f + fibers.nextFloat() * 4.2f;
        const float offsetX = (fibers.nextFloat() - 0.5f) * 5.0f;
        const float offsetY = (fibers.nextFloat() - 0.5f) * 5.0f;
        const juce::Colour shades[] = { juce::Colour(0xfff69ac2), juce::Colour(0xffed72aa),
                                        juce::Colour(0xffffb0cf), juce::Colour(0xffd95591) };
        g.setColour(shades[seed % 4]);
        g.fillEllipse(x + offsetX - r, y + offsetY - r, r * 2.0f, r * 2.0f);
        g.setColour(juce::Colour(0xffffd5e5).withAlpha(0.62f));
        g.fillEllipse(x + offsetX - r * 0.46f, y + offsetY - r * 0.60f, r * 0.8f, r * 0.58f);
    };
    int tuftIndex = 0;
    for (float x = plate.getX() + 19.0f; x < plate.getRight() - 18.0f; x += 4.5f)
    {
        drawTuft(x, plate.getY() + 1.0f, tuftIndex++);
        drawTuft(x, plate.getBottom() - 1.0f, tuftIndex++);
    }
    for (float y = plate.getY() + 18.0f; y < plate.getBottom() - 17.0f; y += 4.5f)
    {
        drawTuft(plate.getX() + 1.0f, y, tuftIndex++);
        drawTuft(plate.getRight() - 1.0f, y, tuftIndex++);
    }

    juce::ColourGradient enamel(juce::Colour(0xffffc5dc), plate.getX(), plate.getY(),
                                juce::Colour(0xffdf438b), plate.getX(), plate.getBottom(), false);
    g.setGradientFill(enamel);
    g.fillRoundedRectangle(plate, 22.0f);
    g.setColour(juce::Colour(0xffffe5ef).withAlpha(0.98f));
    g.drawRoundedRectangle(plate.reduced(1.0f), 21.0f, 1.2f);
    g.setColour(juce::Colour(0xffa4316f).withAlpha(0.56f));
    g.drawRoundedRectangle(plate.reduced(3.2f), 18.8f, 1.0f);
    g.setColour(juce::Colour(0xffffffff).withAlpha(0.55f));
    g.drawLine(plate.getX() + 26.0f, plate.getY() + 3.0f,
               plate.getRight() - 26.0f, plate.getY() + 3.0f, 1.2f);

    // Fine, hand-spaced flecks give the plate a soft coated texture without competing with controls.
    juce::Random flecks(7319);
    const auto textureBounds = plate.reduced(9.0f, 8.0f);
    for (int i = 0; i < 120; ++i)
    {
        const float x = textureBounds.getX() + flecks.nextFloat() * textureBounds.getWidth();
        const float y = textureBounds.getY() + flecks.nextFloat() * textureBounds.getHeight();
        if (! plate.reduced(4.0f).contains(x, y) || y > layout.modeStrip.getY() - 4.0f)
            continue;
        g.setColour((i % 3 == 0) ? juce::Colour(0x55fff3fb) : juce::Colour(0x35a92a70));
        const float d = 0.9f + flecks.nextFloat() * 1.8f;
        g.fillEllipse(x, y, d, d);
    }

    g.setColour(juce::Colour(0xffffd6e7).withAlpha(0.96f));
    g.drawRoundedRectangle(plate.reduced(0.5f), 21.0f, 1.5f);
    g.setColour(juce::Colour(0xffa63370).withAlpha(0.56f));
    g.drawLine(plate.getX() + 20.0f, layout.modeStrip.getY() - 12.0f,
               plate.getRight() - 20.0f, layout.modeStrip.getY() - 12.0f, 1.0f);

    // Recessed metal fasteners catch the same top-left studio light across all three shells.
    drawScrew(input.getX() + 13.0f, input.getY() + 14.0f, 5.0f);
    drawScrew(input.getX() + 13.0f, input.getBottom() - 14.0f, 5.0f);
    drawScrew(output.getRight() - 13.0f, output.getY() + 14.0f, 5.0f);
    drawScrew(output.getRight() - 13.0f, output.getBottom() - 14.0f, 5.0f);
    for (float x : { plate.getX() + 15.0f, plate.getRight() - 15.0f })
        for (float y : { plate.getY() + 15.0f, plate.getBottom() - 15.0f })
            drawScrew(x, y, 5.5f);

    g.setFont(Fonts::mono(8.0f));
    g.setColour(juce::Colour(0xff877b88));
    g.drawText(JucePlugin_VersionString, bounds.reduced(padX).translated(0, -4),
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

    boundsConstrainer.setMinimumSize(720, 560);
    boundsConstrainer.setMaximumSize(1400, 1000);
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
    setSize(860, 680);
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

    repaint();
}

//==============================================================================
void ViveAudioProcessorEditor::paint(juce::Graphics& g)
{
    if (backgroundCacheDirty || backgroundCache.isNull())
        rebuildBackgroundCache();

    g.drawImageAt(backgroundCache, 0, 0);

    const auto bounds = getLocalBounds().toFloat();
    const float W = bounds.getWidth();
    const float readX = W - static_cast<float>(padX) - 104.0f;
    const auto ceilingCard = juce::Rectangle<float>(readX - 18.0f, 14.0f, 122.0f, 46.0f);

    g.setFont(Fonts::rounded(8.5f));
    g.setColour(juce::Colour(0xff9e8295));
    g.drawText("CEILING", (int)ceilingCard.getX() + 11, (int)ceilingCard.getY() + 5,
               (int)ceilingCard.getWidth() - 22, 10,
               juce::Justification::centredLeft);

    g.setFont(Fonts::rounded(17.0f));
    g.setColour(juce::Colour(0xff382237));
    g.drawText(juce::String(headerCeilingDb, 1) + " dB",
               (int)ceilingCard.getX() + 11, (int)ceilingCard.getY() + 21,
               (int)ceilingCard.getWidth() - 22, 20,
               juce::Justification::centredLeft);

    const auto layout = makeEditorLayout(W, bounds.getHeight());
    const float wordmarkY = layout.mainCard.getY() + 151.0f;
    const float center = layout.mainCard.getCentreX();
    const float wingSpan = layout.mainCard.getWidth();
    g.setFont(Fonts::rounded(27.0f));
    g.setColour(juce::Colour(0xff612345).withAlpha(0.82f));
    g.drawText("SATURATOR", (int)layout.mainCard.getX(), (int)wordmarkY,
               (int)layout.mainCard.getWidth(), 34,
               juce::Justification::centred);

    const float ruleY = wordmarkY + 20.0f;
    g.setColour(juce::Colour(0xffb63876).withAlpha(0.56f));
    g.drawLine(center - wingSpan * 0.49f, ruleY, center - wingSpan * 0.25f, ruleY, 1.2f);
    g.drawLine(center + wingSpan * 0.25f, ruleY, center + wingSpan * 0.49f, ruleY, 1.2f);
    g.setColour(juce::Colour(0xffffe6f0));
    g.fillEllipse(center - wingSpan * 0.50f + 6.0f, ruleY - 2.0f, 4.0f, 4.0f);
    g.fillEllipse(center + wingSpan * 0.50f - 10.0f, ruleY - 2.0f, 4.0f, 4.0f);
}

//==============================================================================
void ViveAudioProcessorEditor::resized()
{
    const auto layout = makeEditorLayout((float)getWidth(), (float)getHeight());

    titleLabel.setBounds(padX, 10, 240, 34);
    subtitleLabel.setBounds(padX + 2, 43, 280, 14);

    const int presetButtonW = 94;
    const int presetStatusW = 165;
    const int ceilingCardW = 122;
    const int ceilingCardX = getWidth() - padX - ceilingCardW;
    const int presetButtonX = ceilingCardX - 12 - presetButtonW;
    const int presetStatusX = presetButtonX - presetStatusW - 10;

    presetStatusLabel.setBounds(presetStatusX, 30, presetStatusW, 16);
    presetButton.setBounds(presetButtonX, 25, presetButtonW, 28);

    visualizer.setBounds(layout.visualizerFrame.reduced(8.0f, 16.0f).toNearestInt());

    satSectionLabel.setBounds((int)layout.mainCard.getX() + 22,
                              (int)layout.modeStrip.getY() - 18, 190, 12);
    satSelector.setBounds(layout.modeStrip.toNearestInt());

    auto placeKnob = [] (juce::Slider& slider,
                         juce::Label& nameLbl,
                         juce::Label& valueLbl,
                         const juce::Rectangle<float>& card,
                         int index,
                         int count)
    {
        const float insetX = 24.0f;
        const float usableW = card.getWidth() - insetX * 2.0f;
        const float slotW = usableW / static_cast<float>(count);
        const float centerX = card.getX() + insetX + slotW * (static_cast<float>(index) + 0.5f);
        const float knobSize = juce::jlimit(68.0f, 86.0f, card.getHeight() - 164.0f);
        const float blockY = card.getY() + 25.0f;
        const float labelW = slotW - 4.0f;
        const float labelX = centerX - labelW * 0.5f;

        // Put the live value above the dial and its name directly beneath it,
        // matching the compact hardware labels on the pedal reference.
        valueLbl.setBounds((int)std::round(labelX + 8.0f),
                           (int)std::round(blockY),
                           (int)std::round(labelW - 16.0f), 20);
        slider.setBounds((int)std::round(centerX - knobSize * 0.5f),
                         (int)std::round(blockY + 17.0f),
                         (int)std::round(knobSize),
                         (int)std::round(knobSize));
        nameLbl.setBounds((int)std::round(labelX),
                          (int)std::round(blockY + 19.0f + knobSize),
                          (int)std::round(labelW), 18);
    };

    placeKnob(inputGainSlider,  inputGainLabel,  inputGainValue,  layout.inputCard,  0, 1);
    placeKnob(driveSlider,      driveLabel,      driveValue,      layout.mainCard,   0, 3);
    placeKnob(ceilingSlider,    ceilingLabel,    ceilingValue,    layout.mainCard,   1, 3);
    placeKnob(mixSlider,        mixLabel,        mixValue,        layout.mainCard,   2, 3);
    placeKnob(outputGainSlider, outputGainLabel, outputGainValue, layout.outputCard, 0, 1);

    backgroundCacheDirty = true;
}
