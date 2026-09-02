// Params.h

#pragma once

#include <JuceHeader.h>

namespace Params
{
    // ============================================================================
    // BRANDING
    // ============================================================================

    namespace Branding
    {
        inline const juce::String productName        = "Vive Saturator";
        inline const juce::String shortName          = "Vive";
        inline const juce::String descriptor         = "Adaptive harmonic shaper";
        inline const juce::String presetButton       = "Presets";
        inline const juce::String factoryPresetTitle = "Factory Presets";
        inline const juce::String userPresetTitle    = "User Presets";
        inline const juce::String savePresetLabel    = "Save User Preset...";
        inline const juce::String deletePresetLabel  = "Delete User Preset";
        inline const juce::String nonePresetLabel    = "None";
        inline const juce::String offLabel           = "Off";
        inline const juce::String stateFileExtension = "vivepreset";
    }

    // ============================================================================
    // SATURATION MODES
    // ============================================================================

    enum class SaturationMode
    {
        Clean      = 0,
        Tube       = 1,
        Tape       = 2,
        SolidState = 3,
        Asymmetric = 4
    };

    // ============================================================================
    // FACTORY PRESET STRUCT
    // ============================================================================

    struct FactoryPreset
    {
        const char*    name;
        float          inputGainDb;
        float          drivePercent;
        float          ceilingDb;
        float          outputGainDb;
        float          mixPercent;
        SaturationMode satMode;
    };

    // ============================================================================
    // FACTORY PRESET DEFINITIONS
    // ============================================================================

    inline constexpr FactoryPreset factoryPresets[] =
    {
        //            name               inGain  drive  ceil   outGain  mix     satMode
        { "Reset to Default",      0.0f,   0.0f, 0.0f,  0.0f,  100.0f, SaturationMode::Clean       },
        { "Mix Bus Gentle",         2.0f,  32.0f, -0.3f,  0.0f,  100.0f, SaturationMode::Clean       },
        { "Drum Edge",              1.0f,  62.0f, -0.5f,  0.0f,  100.0f, SaturationMode::Tube        },
        { "Master Glue",            0.0f,  24.0f, -0.3f, -0.5f,  100.0f, SaturationMode::SolidState  },
        { "Aggressive Tape",        1.0f,  78.0f, -0.5f,  0.0f,  100.0f, SaturationMode::Tape        },
        { "Lo-Fi Crush",            2.0f,  90.0f, -0.3f,  0.0f,  100.0f, SaturationMode::Asymmetric  },
        { "Vocal Warmth",           0.0f,  18.0f, -1.0f, -1.0f,   80.0f, SaturationMode::Clean       },
        { "Podcast Leveler",        0.0f,  40.0f, -1.0f, -1.0f,  100.0f, SaturationMode::Clean       },
    };

    inline constexpr int numFactoryPresets = static_cast<int>(std::size(factoryPresets));
    inline constexpr auto& genrePresets = factoryPresets;
    inline constexpr int numGenrePresets = numFactoryPresets;

    // ============================================================================
    // PARAMETER IDs
    // ============================================================================

    namespace IDs
    {
        inline constexpr const char* INPUT_GAIN_ID  = "inputGain";
        inline constexpr const char* CEILING_ID     = "ceiling";
        inline constexpr const char* OUTPUT_GAIN_ID = "outputGain";
        inline constexpr const char* DRIVE_ID       = "drive";
        inline constexpr const char* MIX_ID         = "mix";
        inline constexpr const char* GENRE_ID       = "genre";
        inline constexpr const char* SAT_MODE_ID    = "satMode";
    }

    // ============================================================================
    // PARAMETER RANGES & DEFAULTS
    // ============================================================================

    namespace Ranges
    {
        // Mix (Dry/Wet)
        constexpr float MIX_MIN     = 0.0f;
        constexpr float MIX_MAX     = 100.0f;
        constexpr float MIX_DEFAULT = 100.0f;
        constexpr float MIX_STEP    = 0.1f;

        // Input Gain
        constexpr float INPUT_GAIN_MIN     = -12.0f;
        constexpr float INPUT_GAIN_MAX     =  12.0f;
        constexpr float INPUT_GAIN_DEFAULT =   0.0f;
        constexpr float INPUT_GAIN_STEP    =   0.1f;

        // Ceiling
        constexpr float CEILING_MIN     = -15.0f;
        constexpr float CEILING_MAX     =   0.0f;
        constexpr float CEILING_DEFAULT =  0.f;
        constexpr float CEILING_STEP    =   0.1f;

        // Output Gain
        constexpr float OUTPUT_GAIN_MIN     = -12.0f;
        constexpr float OUTPUT_GAIN_MAX     =   0.0f;
        constexpr float OUTPUT_GAIN_DEFAULT =   0.0f;
        constexpr float OUTPUT_GAIN_STEP    =   0.1f;

        // Drive
        constexpr float DRIVE_MIN     =   0.0f;
        constexpr float DRIVE_MAX     = 100.0f;
        constexpr float DRIVE_DEFAULT =   0.0f;
        constexpr float DRIVE_STEP    =   0.1f;
    }

    // ============================================================================
    // PARAMETER NAMES
    // ============================================================================

    namespace Names
    {
        inline const juce::String INPUT_GAIN  = "Input Gain";
        inline const juce::String CEILING     = "Ceiling";
        inline const juce::String OUTPUT_GAIN = "Output Gain";
        inline const juce::String DRIVE       = "Drive";
        inline const juce::String MIX         = "Mix";
        inline const juce::String GENRE       = "Factory Preset";
        inline const juce::String SAT_MODE    = "Harmonic Model";
    }

    // ============================================================================
    // PARAMETER UNITS
    // ============================================================================

    namespace Units
    {
        inline const juce::String DECIBELS = "dB";
        inline const juce::String PERCENT  = "%";
    }

    // ============================================================================
    // PARAMETER LAYOUT
    // ============================================================================

    inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        // Input Gain
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(IDs::INPUT_GAIN_ID, 1),
            Names::INPUT_GAIN,
            juce::NormalisableRange<float>(
                Ranges::INPUT_GAIN_MIN,
                Ranges::INPUT_GAIN_MAX,
                Ranges::INPUT_GAIN_STEP),
            Ranges::INPUT_GAIN_DEFAULT,
            Units::DECIBELS));

        // Ceiling
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(IDs::CEILING_ID, 1),
            Names::CEILING,
            juce::NormalisableRange<float>(
                Ranges::CEILING_MIN,
                Ranges::CEILING_MAX,
                Ranges::CEILING_STEP),
            Ranges::CEILING_DEFAULT,
            Units::DECIBELS));

        // Output Gain
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(IDs::OUTPUT_GAIN_ID, 1),
            Names::OUTPUT_GAIN,
            juce::NormalisableRange<float>(
                Ranges::OUTPUT_GAIN_MIN,
                Ranges::OUTPUT_GAIN_MAX,
                Ranges::OUTPUT_GAIN_STEP),
            Ranges::OUTPUT_GAIN_DEFAULT,
            Units::DECIBELS));

        // Drive
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(IDs::DRIVE_ID, 1),
            Names::DRIVE,
            juce::NormalisableRange<float>(
                Ranges::DRIVE_MIN,
                Ranges::DRIVE_MAX,
                Ranges::DRIVE_STEP),
            Ranges::DRIVE_DEFAULT,
            Units::PERCENT));

        // Mix
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(IDs::MIX_ID, 1),
            Names::MIX,
            juce::NormalisableRange<float>(
                Ranges::MIX_MIN,
                Ranges::MIX_MAX,
                Ranges::MIX_STEP),
            Ranges::MIX_DEFAULT,
            Units::PERCENT));

        // Genre
        juce::StringArray genreNames;
        for (int i = 0; i < numFactoryPresets; ++i)
            genreNames.add(factoryPresets[i].name);

        layout.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID(IDs::GENRE_ID, 1),
            Names::GENRE,
            genreNames,
            0));

        // Saturation Mode
        layout.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID(IDs::SAT_MODE_ID, 1),
            Names::SAT_MODE,
            juce::StringArray { "Off", "Clean", "Tube", "Tape", "Solid", "Asym" },
            static_cast<int>(SaturationMode::Clean)));

        return layout;
    }

} // namespace Params
