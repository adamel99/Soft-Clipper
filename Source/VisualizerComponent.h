#pragma once
#include <JuceHeader.h>
#include <deque>
#include <array>
#include <vector>

//==============================================================================
struct MeterFrame
{
    float inputPeak      = 0.0f;
    float outputPeak     = 0.0f;
    float inputRms       = 0.0f;
    float outputRms      = 0.0f;
    float gainReductionDb = 0.0f;
    float overageDb      = 0.0f;
    bool  isClipping     = false;
    bool  isSoftClipping = false;
};

//==============================================================================
class VisualizerComponent : public juce::Component,
                             private juce::Timer
{
public:
    VisualizerComponent();
    ~VisualizerComponent() override;

    void paint     (juce::Graphics&)            override;
    void resized   ()                           override;
    void mouseDown (const juce::MouseEvent&)    override;

    void pushSamples (const juce::AudioBuffer<float>& input,
                      const juce::AudioBuffer<float>& output);

    void setClippingThreshold (float newThresholdLinear);

private:
    void timerCallback() override;

    float calculateRms      (const juce::AudioBuffer<float>& buffer) const;
    float calculateChunkRms (const juce::AudioBuffer<float>& buffer,
                             int startSample, int numSamples) const;

    juce::Colour getGRColour      (float grDb)      const;
    juce::Colour getOverageColour (float overageDb) const;

    // ── Waveform path helpers ─────────────────────────────────────────────
    /** Builds a Catmull-Rom spline stroke path from a sequence of screen points. */
    static void buildSplinePath (juce::Path& strokePath,
                                 const std::vector<juce::Point<float>>& pts,
                                 float tension = 0.4f);

    /** Builds the closed fill path (straight lineTo — avoids spline baseline artefacts). */
    static void buildFillPath   (juce::Path& fillPath,
                                 const std::vector<juce::Point<float>>& pts,
                                 float vizX, float plotBot, float vizW);

    // ── Cat mascot ────────────────────────────────────────────────────────
    enum class CatState { Idle, Active, Peak, Clipping, Nervous };
    CatState catState    = CatState::Idle;
    float    catAnimPhase  = 0.0f;   // 0..1, advances each timerCallback tick
    float    catBlinkPhase = 0.0f;   // 0..1, separate blink rhythm
    bool     catBlinking   = false;
    
    float catShakeDecay = 0.0f;
    float catMouthOpen = 0.0f;

    void paintCat (juce::Graphics& g, float cx, float cy, float scale) const;

    // ── FIFO ──────────────────────────────────────────────────────────────
    static constexpr int fifoSize = 4096;
    juce::AbstractFifo   fifo { fifoSize };
    std::array<MeterFrame, fifoSize> fifoBuffer;

    std::deque<MeterFrame> frameHistory;
    static constexpr int   maxFrames = 800;

    // ── Thresholds ────────────────────────────────────────────────────────
    float clippingThreshold     = 1.0f;
    float softClippingThreshold = 0.95f;

    // ── Overage bar smoother ──────────────────────────────────────────────
    float smoothedOverageDb = 0.0f;

    // ── Toggle state ──────────────────────────────────────────────────────
    bool showInput  = true;
    bool showOutput = true;

    // ── Peak smoothers (audio-thread side, written in pushSamples) ────────
    float smoothedInputPeak  = 0.0f;
    float smoothedOutputPeak = 0.0f;

    // ── Peak-hold (UI-thread side, updated in timerCallback) ─────────────
    float inputPeakHeld   = 0.0f;
    float outputPeakHeld  = 0.0f;
    int   inputPeakHoldFrames  = 0;
    int   outputPeakHoldFrames = 0;

    static constexpr int   peakHoldDuration = 90;    // ~1.5 s @ 60 Hz
    static constexpr float peakDecayRate    = 0.008f; // linear per tick

    // ── Clip flash ────────────────────────────────────────────────────────
    float clipFlashAlpha = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VisualizerComponent)
};
