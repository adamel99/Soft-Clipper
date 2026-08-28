#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "CustomLookAndFeel.h"
#include "VisualizerComponent.h"

//==============================================================================
namespace EditorPalette
{
    static const juce::Colour dark    { 0xff110b11 };
    static const juce::Colour dark2   { 0xff0d090d };
    static const juce::Colour cream   { 0xfff9dec9 };
    static const juce::Colour blue    { 0xff99b2dd };
    static const juce::Colour pink    { 0xffde369d };

    static inline juce::Colour textPrimary()   { return cream; }
    static inline juce::Colour textSecondary() { return cream.withAlpha(0.60f); }
    static inline juce::Colour textTertiary()  { return cream.withAlpha(0.36f); }
    static inline juce::Colour textGhost()     { return cream.withAlpha(0.14f); }
}

//==============================================================================
class SatModeSelector : public juce::Component
{
public:
    std::function<void(int)> onModeChanged;

    SatModeSelector()  = default;
    ~SatModeSelector() = default;

    void setActiveMode(int modeIndex) { activeMode = modeIndex; repaint(); }
    int  getActiveMode() const        { return activeMode; }

    void paint(juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        const float cellW = b.getWidth() / 3.0f;
        const float cellH = b.getHeight() / 2.0f;
        const float pad   = 2.5f;
        const float r     = 4.0f;

        g.setFont(juce::Font(juce::FontOptions()
            .withName(juce::Font::getDefaultMonospacedFontName())
            .withHeight(8.0f)
            .withStyle("Bold")));

        for (int i = 0; i < numModes; ++i)
        {
            const int col = i % 3;
            const int row = i / 3;
            const bool active = (i == activeMode + 1); // mode -1 = NONE = index 0

            const juce::Rectangle<float> cell(
                b.getX() + col * cellW + pad,
                b.getY() + row * cellH + pad,
                cellW - pad * 2.0f,
                cellH - pad * 2.0f);

            if (active)
            {
                g.setColour(EditorPalette::pink.withAlpha(0.13f));
                g.fillRoundedRectangle(cell, r);
                g.setColour(EditorPalette::pink.withAlpha(0.42f));
                g.drawRoundedRectangle(cell.reduced(0.5f), r, 0.5f);
            }
            else
            {
                g.setColour(EditorPalette::cream.withAlpha(0.025f));
                g.fillRoundedRectangle(cell, r);
                g.setColour(EditorPalette::cream.withAlpha(0.08f));
                g.drawRoundedRectangle(cell.reduced(0.5f), r, 0.5f);
            }

            g.setColour(active ? EditorPalette::cream : EditorPalette::cream.withAlpha(0.36f));
            g.drawText(modeLabels[i], cell.toNearestInt(), juce::Justification::centred);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const float cellW = (float)getWidth() / 3.0f;
        const float cellH = (float)getHeight() / 2.0f;
        const int col = juce::jlimit(0, 2, (int)(e.position.x / cellW));
        const int row = juce::jlimit(0, 1, (int)(e.position.y / cellH));
        const int idx = row * 3 + col;
        const int newMode = idx - 1;

        if (newMode == activeMode)
            return;

        activeMode = newMode;
        repaint();

        if (onModeChanged)
            onModeChanged(newMode);
    }

private:
    static constexpr int numModes = 6;
    const char* modeLabels[numModes] = { "NONE", "CLEAN", "TUBE", "TAPE", "SOLID", "ASYM" };
    int activeMode = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SatModeSelector)
};

//==============================================================================
class ViveAudioProcessorEditor : public juce::AudioProcessorEditor,
                                  private juce::Timer
{
public:
    explicit ViveAudioProcessorEditor(ViveAudioProcessor&);
    ~ViveAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void applyGenrePreset(int idx);
    void configureHeader();
    void configureSaturationSection();
    void configureKnob(juce::Slider& slider,
                       juce::Label& nameLabel,
                       juce::Label& valueLabel,
                       const char* name,
                       const char* sliderName,
                       const juce::String& suffix);
    void syncSatModeSelector();

    ViveAudioProcessor& audioProcessor;
    CustomLookAndFeel customLookAndFeel;
    VisualizerComponent visualizer;

    static constexpr int headerH   = 58;
    static constexpr int controlsH = 126;
    static constexpr int satPanelW = 176;
    static constexpr int padX      = 20;
    static constexpr int padY      = 14;

    juce::ComponentBoundsConstrainer boundsConstrainer;

    juce::Label titleLabel;
    juce::Label subtitleLabel;

    juce::Label satSectionLabel;
    SatModeSelector satSelector;

    juce::Slider inputGainSlider, driveSlider, ceilingSlider,
                 mixSlider, outputGainSlider;

    juce::Label inputGainLabel,  inputGainValue;
    juce::Label driveLabel,      driveValue;
    juce::Label ceilingLabel,    ceilingValue;
    juce::Label mixLabel,        mixValue;
    juce::Label outputGainLabel, outputGainValue;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        inputGainAttachment, driveAttachment, ceilingAttachment,
        mixAttachment, outputGainAttachment;

    float headerCeilingDb = 0.0f;
    float pulsePhase      = 0.0f;
    bool isRestoringState = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ViveAudioProcessorEditor)
};
