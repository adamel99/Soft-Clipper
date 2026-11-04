#pragma once
#include <JuceHeader.h>
#include <deque>
#include <array>
#include <vector>

//==============================================================================
struct MeterFrame
{
    double durationSeconds = 0.0;
    float inputPeak      = 0.0f;
    float outputPeak     = 0.0f;
    float inputRms       = 0.0f;
    float outputRms      = 0.0f;
    bool  isClipping     = false;
};


//==============================================================================
// INTERVIEW NOTE: this component is a good example to talk through for
// "how do you get audio-thread data safely onto the UI" — see the FIFO members
// near the bottom and pushSamples()/timerCallback() in the .cpp.
class VisualizerComponent : public juce::Component,
                             private juce::Timer
{
public:
    VisualizerComponent();
    ~VisualizerComponent() override;

    void paint     (juce::Graphics&)            override;
    void resized   ()                           override;

    // Called from the AUDIO THREAD (see PluginProcessor::processBlock).
    // Must be lock-free / non-blocking / non-allocating.
    void pushSamples (const juce::AudioBuffer<float>& input,
                      const juce::AudioBuffer<float>& output,
                      const juce::AudioBuffer<float>& ceilingStageOutput,
                      int validSampleCount,
                      double sampleRateHz);

    void setClippingThreshold (float newThresholdLinear);

private:
    // Called on the UI/message thread at 60Hz (see startTimerHz in the .cpp).
    void timerCallback() override;

    float calculateChunkRms (const juce::AudioBuffer<float>& buffer,
                             int startSample, int numSamples) const;

    // ── Waveform path helpers ─────────────────────────────────────────────
    /** Builds a Catmull-Rom spline stroke path from a sequence of screen points. */
    static void buildSplinePath (juce::Path& strokePath,
                                 const std::vector<juce::Point<float>>& pts,
                                 float tension = 0.4f);

    /** Builds the closed fill path (straight lineTo — avoids spline baseline artefacts). */
    static void buildFillPath   (juce::Path& fillPath,
                                 const std::vector<juce::Point<float>>& pts,
                                 float vizX, float plotBot, float vizW);

    // ── FIFO ──────────────────────────────────────────────────────────────
    // INTERVIEW NOTE: juce::AbstractFifo implements a lock-free circular buffer
    // index scheme suitable for exactly ONE writer thread (audio, via pushSamples)
    // and ONE reader thread (UI, via timerCallback's prepareToRead/finishedRead).
    // It hands out index RANGES for you to read/write into a buffer you own
    // (fifoBuffer below) — it does not move the data itself, and it never blocks
    // or takes a lock, which is exactly what's required on the audio thread.
    static constexpr int fifoSize = 4096;
    juce::AbstractFifo   fifo { fifoSize };
    std::array<MeterFrame, fifoSize> fifoBuffer;

    // History ring for the on-screen waveform trace, owned/mutated only by the
    // UI thread (populated by draining the fifo in timerCallback).
    std::deque<MeterFrame> frameHistory;
    static constexpr int   maxFrames = 4096;
    static constexpr double historyDurationSeconds = 2.0;
    double storedHistoryDurationSeconds = 0.0;

    // ── Threshold ────────────────────────────────────────────────────────
    std::atomic<float> clippingThreshold { 1.0f };

    // ── RMS energy followers (audio-thread side) ─────────────────────────
    // Mean-square values are exponentially averaged with a 300 ms time
    // constant, then square-rooted for a stable RMS display.
    double smoothedInputMeanSquare  = 0.0;
    double smoothedOutputMeanSquare = 0.0;

    // ── Peak-hold (UI-thread side, updated in timerCallback) ─────────────
    float inputPeakHeld   = 0.0f;
    float outputPeakHeld  = 0.0f;
    double inputPeakHoldSeconds  = 0.0;
    double outputPeakHoldSeconds = 0.0;
    double lastTimerTimeMs       = 0.0;

    // ── Clip flash ────────────────────────────────────────────────────────
    float clipFlashAlpha = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VisualizerComponent)
};
