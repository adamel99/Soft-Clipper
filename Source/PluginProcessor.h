// PluginProcessor.h
#pragma once

#include <JuceHeader.h>
#include "Param.h"
#include "VisualizerComponent.h"

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
    VisualizerComponent& getVisualizer() { return visualizer; }

private:
    juce::AudioProcessorValueTreeState apvts;
    VisualizerComponent                visualizer;
    
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

    // ── Cached parameter pointer ──────────────────────────────────────────
    juce::AudioParameterChoice* satModeParam = nullptr;

    // ── Working buffers ───────────────────────────────────────────────────
    juce::AudioBuffer<float> dryBuffer;
    juce::AudioBuffer<float> inputMeterBuffer;

    // ── Per-block ceiling snapshot ────────────────────────────────────────
    std::vector<float> ceilingPerSample;
    std::vector<float> drivePerSample;
    
    static inline float softClipFSC(float x, float ceiling) noexcept
    {
        const float sign = x >= 0.0f ? 1.0f : -1.0f;
        const float ax   = std::abs(x);

        // Wider knee than before — starts at ~-2 dB instead of ~-1 dB.
        // When the signal hits hard and often, a narrow knee means peaks
        // spend most of their time in the steep part of the curve, which
        // reads as harshness on dense material. Starting earlier gives
        // peaks more room to be shaped gradually.
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
