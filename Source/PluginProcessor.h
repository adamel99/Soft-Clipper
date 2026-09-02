// PluginProcessor.h
#pragma once

#include <JuceHeader.h>
#include "Param.h"

class VisualizerComponent;

class ViveAudioProcessor : public juce::AudioProcessor
{
public:
    ViveAudioProcessor();
    ~ViveAudioProcessor() override;

    //==========================================================================
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
#endif

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==========================================================================
    const juce::String getName()        const override;
    bool  acceptsMidi()                 const override;
    bool  producesMidi()                const override;
    bool  isMidiEffect()                const override;
    double getTailLengthSeconds()       const override;

    //==========================================================================
    int  getNumPrograms()                              override;
    int  getCurrentProgram()                           override;
    void setCurrentProgram(int index)                 override;
    const juce::String getProgramName(int index)      override;
    void changeProgramName(int index,
        const juce::String& newName) override;

    //==========================================================================
    void getStateInformation(juce::MemoryBlock& destData)              override;
    void setStateInformation(const void* data, int sizeInBytes)        override;

    //==========================================================================
    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }
    void setVisualizer(VisualizerComponent* newVisualizer) { visualizer = newVisualizer; }
    VisualizerComponent& getVisualizer() { jassert(visualizer != nullptr); return *visualizer; }

    juce::StringArray getFactoryPresetNames() const;
    juce::StringArray getUserPresetNames() const;
    bool loadFactoryPreset(int presetIndex);
    bool saveUserPreset(const juce::String& presetName);
    bool loadUserPreset(const juce::String& presetName);
    bool deleteUserPreset(const juce::String& presetName);
    juce::File getUserPresetDirectory() const;

private:
    juce::AudioProcessorValueTreeState apvts;
    VisualizerComponent*               visualizer = nullptr;
    int                                preparedBlockSize = 0;

    juce::dsp::Oversampling<float> oversampler{
        2,
        2,   // 2^2 = 4x
        juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
        true,
        false
    };
    static constexpr int oversampleFactor = 4;

    // ── Parameter smoothers ───────────────────────────────────────────────
    juce::SmoothedValue<float> inputGainSmoother;
    juce::SmoothedValue<float> driveSmoother;
    juce::SmoothedValue<float> ceilingSmoother;
    juce::SmoothedValue<float> mixSmoother;
    juce::SmoothedValue<float> outputGainSmoother;

    // ── Cached parameter access ────────────────────────────────────────────
    std::atomic<float>* inputGainParam = nullptr;
    std::atomic<float>* driveParam = nullptr;
    std::atomic<float>* ceilingParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* outputGainParam = nullptr;
    juce::AudioParameterChoice* satModeParam = nullptr;
    juce::AudioParameterChoice* genreParam = nullptr;

    // ── Working buffers ───────────────────────────────────────────────────
    juce::AudioBuffer<float> dryBuffer;

    // ── Per-block ceiling snapshot ────────────────────────────────────────
    std::vector<float> ceilingPerSample;
    std::vector<float> drivePerSample;

    void processBlockChunk(juce::AudioBuffer<float>& buffer);
    void cacheParameterPointers();
    void applyPresetValues(const Params::FactoryPreset& preset);
    bool restoreStateFromXml(const juce::XmlElement& xmlState);
    void updateGenreParameter(int presetIndex);

    struct SaturationResult
    {
        float sample = 0.0f;
        float makeupGain = 1.0f;
    };

    static SaturationResult shapeSaturation(float sample, float driveAmount, int satMode, float ceiling) noexcept;
    static float clampToCeiling(float sample, float ceiling) noexcept;

    // INTERVIEW NOTE: "FSC" = Foldback/Full-Scale Clipper — a knee-based soft clipper.
    // Design goals worth being able to explain out loud:
    //   1. Below the knee, signal passes untouched (transparent, no coloration on quiet material).
    //   2. Above the knee, a tanh curve bends the signal smoothly toward `ceiling` instead of
    //      hard-cutting it — hard clipping creates a discontinuous derivative (a "corner"),
    //      which is what generates the harsh, buzzy high-order harmonics people hear as digital clipping.
    //      tanh has a continuous, smoothly-decreasing derivative, so the added harmonics roll off
    //      faster (mostly low-order odd harmonics: 3rd, 5th, ...), which reads as "warm" instead of "harsh".
    //   3. tanh(x) -> 1 as x -> inf, so the output is asymptotic: it approaches `ceiling` but the math
    //      alone never guarantees x < ceiling for all floating-point inputs, hence the jmin() safety clamp
    //      at the end — belt-and-suspenders so a downstream hard limiter/DAC never sees an overshoot.
    static inline float softClipFSC(float x, float ceiling) noexcept
    {
        // Work in sign/magnitude form so ONE formula handles both polarities identically —
        // this guarantees the curve is odd-symmetric (f(-x) = -f(x)), which matters:
        // odd symmetry means NO even-order harmonics are introduced by this stage alone
        // (even harmonics come from asymmetric processes, e.g. tube bias). Keeps this stage
        // "clean" saturation vs the asymmetric mode below which deliberately breaks this.
        const float sign = x >= 0.0f ? 1.0f : -1.0f;
        const float ax   = std::abs(x);

        // Wider knee than before — starts at ~-2 dB instead of ~-1 dB.
        // When the signal hits hard and often, a narrow knee means peaks
        // spend most of their time in the steep part of the curve, which
        // reads as harshness on dense material. Starting earlier gives
        // peaks more room to be shaped gradually.
        // 0.794 ≈ 10^(-2/20) — the linear-gain equivalent of -2 dBFS relative to `ceiling`.
        // (dB-to-linear conversion: gain = 10^(dB/20); here dB = -2 -> 10^(-0.1) ≈ 0.794.)
        // So the knee sits 2 dB below the ceiling regardless of what the ceiling parameter is set to —
        // this makes the knee proportional/scale-invariant rather than a fixed absolute value.
        const float kneeStart = ceiling * 0.794f;  // ~-2 dB

        if (ax <= kneeStart)
            return x;

        const float excess = ax - kneeStart;
        const float range  = ceiling - kneeStart;

        // Softer tanh slope — dividing by (range * 1.5f) instead of range
        // makes the curve less steep in the knee, so heavy signals get
        // rounded more gently rather than compressed hard at the top.
        // The asymptote still approaches ceiling, never flattens to a shelf.
        const float shaped = kneeStart + range * std::tanh(excess / (range * 1.5f));

        return sign * juce::jmin(shaped, ceiling);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ViveAudioProcessor)
};
