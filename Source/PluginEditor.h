#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "CustomLookAndFeel.h"
#include "VisualizerComponent.h"
#include "PluginFonts.h"

//==============================================================================
namespace EditorPalette
{
    static const juce::Colour cream   { 0xfff9f2ee };
    static const juce::Colour pink    { 0xffed79b6 };

}

//==============================================================================
class SatModeSelector : public juce::Component
{
public:
    std::function<void(int)> onModeChanged;

    SatModeSelector() = default;
    ~SatModeSelector() = default;

    void setActiveMode(int modeIndex)
    {
        activeMode = juce::jlimit(0, numModes - 1, modeIndex);
        repaint();
    }

    int getActiveMode() const { return activeMode; }

    void paint(juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        const float cellW = b.getWidth() / (float) numModes;
        const float cellH = b.getHeight();
        const float pad   = 3.0f;
        const float r     = 7.0f;

        g.setFont(ViveFonts::bold(10.5f));

        for (int i = 0; i < numModes; ++i)
        {
            const int col = i;
            const int row = 0;
            const bool active = (i == activeMode);

            const juce::Rectangle<float> cell(
                b.getX() + col * cellW + pad,
                b.getY() + row * cellH + pad,
                cellW - pad * 2.0f,
                cellH - pad * 2.0f);

            if (active)
            {
                g.setColour(juce::Colour(0xff321c31));
                g.fillRoundedRectangle(cell, r);
                g.setColour(EditorPalette::pink);
                g.drawRoundedRectangle(cell.reduced(0.5f), r, 0.5f);
            }
            else
            {
                g.setColour(juce::Colour(0xfff1c4d8));
                g.fillRoundedRectangle(cell, r);
                g.setColour(juce::Colour(0xffcb8fae));
                g.drawRoundedRectangle(cell.reduced(0.5f), r, 0.5f);
            }

            g.setColour(active ? EditorPalette::cream : juce::Colour(0xff52374c));
            g.drawText(modeLabels[i], cell.toNearestInt(), juce::Justification::centred);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const float cellW = (float)getWidth() / (float) numModes;
        const int idx = juce::jlimit(0, numModes - 1, (int)(e.position.x / cellW));

        if (idx == activeMode)
            return;

        activeMode = idx;
        repaint();

        if (onModeChanged)
            onModeChanged(idx);
    }

private:
    static constexpr int numModes = 6;
    const char* modeLabels[numModes] = { "OFF", "CLEAN", "TUBE", "TAPE", "SOLID", "ASYM" };
    int activeMode = 0;

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
    void configureHeader();
    void configureSaturationSection();
    void configureKnob(juce::Slider& slider,
                       juce::Label& nameLabel,
                       juce::Label& valueLabel,
                       const char* name,
                       const char* sliderName,
                       const juce::String& suffix);
    void syncSatModeSelector();
    void rebuildBackgroundCache();
    void updatePresetStatus(const juce::String& presetName, bool isUserPreset);
    void showPresetMenu();
    void handlePresetMenuResult(int result);
    void saveCurrentAsUserPreset();
    void deleteCurrentUserPreset();
    void loadFactoryPreset(int presetIndex);
    void loadUserPreset(const juce::String& presetName);

    ViveAudioProcessor& audioProcessor;
    CustomLookAndFeel customLookAndFeel;
    VisualizerComponent visualizer;

    static constexpr int padX = 32;

    juce::ComponentBoundsConstrainer boundsConstrainer;

    juce::Label titleLabel;
    juce::Label subtitleLabel;
    juce::Label presetStatusLabel;
    juce::TextButton presetButton;

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

    juce::Image backgroundCache;
    bool backgroundCacheDirty = true;
    juce::String activePresetName = Params::Branding::nonePresetLabel;
    bool activePresetIsUser = false;

    float headerCeilingDb = 0.0f;
    bool isRestoringState = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ViveAudioProcessorEditor)
};
