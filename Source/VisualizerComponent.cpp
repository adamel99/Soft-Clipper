#include "VisualizerComponent.h"
#include "PluginFonts.h"

VisualizerComponent::VisualizerComponent()
{
    startTimerHz (60);
}

VisualizerComponent::~VisualizerComponent()
{
    stopTimer();
}

//==============================================================================
// INTERVIEW NOTE: pushSamples() runs on the AUDIO THREAD (called from
// PluginProcessor::processBlock). Everything in this function must be
// realtime-safe: no locks, no heap allocation, no blocking calls. The only
// thing it does that touches shared state is write into the lock-free FIFO.
void VisualizerComponent::pushSamples (const juce::AudioBuffer<float>& input,
                                        const juce::AudioBuffer<float>& output,
                                        const juce::AudioBuffer<float>& ceilingStageOutput,
                                        int validSampleCount,
                                        double sampleRate)
{
    const int availableSamples = juce::jmin (input.getNumSamples(),
        juce::jmin (output.getNumSamples(), ceilingStageOutput.getNumSamples()));
    const int numSamples = juce::jmin (juce::jmax (0, validSampleCount), availableSamples);
    if (numSamples <= 0) return;
    const double validSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;

    // Downsample the metering data itself: rather than computing/storing a
    // MeterFrame for every single audio sample (wasteful — the UI redraws at
    // 60Hz, far below audio rate), we chunk the block into 128-sample windows
    // and compute one peak/RMS/etc. value per chunk. This is a classic
    // control-rate-vs-audio-rate tradeoff applied to metering instead of parameters.
    constexpr int stride       = 128;
    const int     framesToWrite = (numSamples + stride - 1) / stride;

    // prepareToWrite/finishedWrite is the AbstractFifo contract: ask for N slots,
    // get back up to two index ranges (start1/size1, start2/size2) because the
    // write may need to WRAP AROUND the circular buffer, then write into fifoBuffer
    // at those exact indices, then tell the fifo how many you actually wrote.
    int start1, size1, start2, size2;
    fifo.prepareToWrite (framesToWrite, start1, size1, start2, size2);

    int written = 0;
    for (int s = 0; s < numSamples && written < (size1 + size2); s += stride, ++written)
    {
        const int chunkLen = juce::jmin (stride, numSamples - s);

        MeterFrame frame;
        // Sample peaks are the maximum magnitude across every channel. Do not
        // smooth these values: smoothing can hide a real transient or a ceiling
        // crossing. The UI applies a separate peak hold and release.
        auto maxChannelMagnitude = [s, chunkLen] (const juce::AudioBuffer<float>& buffer)
        {
            float peak = 0.0f;
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                peak = juce::jmax (peak, buffer.getMagnitude (ch, s, chunkLen));
            return peak;
        };
        const float rawInPeak        = maxChannelMagnitude (input);
        const float rawOutPeak       = maxChannelMagnitude (output);
        const float rawCeilingPeak   = maxChannelMagnitude (ceilingStageOutput);
        const float threshold        = clippingThreshold.load (std::memory_order_relaxed);
        const double dtSeconds        = static_cast<double> (chunkLen) / validSampleRate;

        frame.durationSeconds = dtSeconds;
        frame.inputPeak  = rawInPeak;
        frame.outputPeak = rawOutPeak;

        // Smooth mean-square energy with a time-based 300 ms constant. This is
        // an RMS meter, not LUFS: it has no K-weighting or loudness gating.
        constexpr double rmsTimeConstantSeconds = 0.300;
        const double rmsAlpha = 1.0 - std::exp (-dtSeconds / rmsTimeConstantSeconds);
        const double inputChunkRms  = calculateChunkRms (input,  s, chunkLen);
        const double outputChunkRms = calculateChunkRms (output, s, chunkLen);
        smoothedInputMeanSquare += (inputChunkRms * inputChunkRms - smoothedInputMeanSquare) * rmsAlpha;
        smoothedOutputMeanSquare += (outputChunkRms * outputChunkRms - smoothedOutputMeanSquare) * rmsAlpha;
        frame.inputRms  = static_cast<float> (std::sqrt (juce::jmax (0.0, smoothedInputMeanSquare)));
        frame.outputRms = static_cast<float> (std::sqrt (juce::jmax (0.0, smoothedOutputMeanSquare)));

        frame.isClipping      = rawCeilingPeak >= threshold * 0.999f;

        const int idx = (written < size1)
                        ? start1 + written
                        : start2 + (written - size1);

        fifoBuffer[static_cast<size_t> (idx)] = frame;
    }

    fifo.finishedWrite (written);
}

//==============================================================================
// timerCallback runs on the UI/MESSAGE THREAD at 60Hz (see startTimerHz in the
// constructor). This is the "consumer" side of the lock-free FIFO — safe to
// allocate, safe to touch juce::Component state, safe to call repaint() here.
void VisualizerComponent::timerCallback()
{
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    const double dtSeconds = lastTimerTimeMs > 0.0
        ? juce::jlimit (0.0, 0.25, (nowMs - lastTimerTimeMs) / 1000.0)
        : 1.0 / 60.0;
    lastTimerTimeMs = nowMs;

    // ── Drain FIFO into frameHistory ──────────────────────────────────────
    // prepareToRead/finishedRead is the reader half of the AbstractFifo contract,
    // mirroring prepareToWrite/finishedWrite above. Passing fifoSize just means
    // "give me as much as is currently available, up to the full buffer."
    int start1, size1, start2, size2;
    fifo.prepareToRead (fifoSize, start1, size1, start2, size2);

    float newestInputPeak = 0.0f;
    float newestOutputPeak = 0.0f;
    bool hadNewClip = false;
    auto appendFrame = [&] (const MeterFrame& frame)
    {
        frameHistory.push_back (frame);
        storedHistoryDurationSeconds += frame.durationSeconds;
        newestInputPeak = juce::jmax (newestInputPeak, frame.inputPeak);
        newestOutputPeak = juce::jmax (newestOutputPeak, frame.outputPeak);
        hadNewClip = hadNewClip || frame.isClipping;
    };
    for (int i = 0; i < size1; ++i)
        appendFrame (fifoBuffer[static_cast<size_t> (start1 + i)]);
    for (int i = 0; i < size2; ++i)
        appendFrame (fifoBuffer[static_cast<size_t> (start2 + i)]);

    fifo.finishedRead (size1 + size2);

    // Keep a fixed time window in history so the trace duration is consistent
    // across sample rates and the deque remains bounded.
    while (! frameHistory.empty()
           && (static_cast<int> (frameHistory.size()) > maxFrames
               || storedHistoryDurationSeconds > historyDurationSeconds))
    {
        storedHistoryDurationSeconds -= frameHistory.front().durationSeconds;
        frameHistory.pop_front();
    }

    // ── Peak hold (UI thread) ─────────────────────────────────────────────
    // Hold each observed peak for 1.5 seconds, then release at 20 dB per
    // second so the marker behaves consistently if the UI timer jitters.
    {
        auto updateHeld = [dtSeconds] (float current, float& held, double& holdSeconds)
        {
            if (current >= held)
            {
                held       = current;
                holdSeconds = 1.5;
            }
            else if (holdSeconds > 0.0)
            {
                holdSeconds = juce::jmax (0.0, holdSeconds - dtSeconds);
            }
            else
            {
                const float heldDb = juce::Decibels::gainToDecibels (juce::jmax (held, 1e-7f), -140.0f);
                held = juce::Decibels::decibelsToGain (heldDb - static_cast<float> (20.0 * dtSeconds));
            }
        };

        updateHeld (newestInputPeak,  inputPeakHeld,  inputPeakHoldSeconds);
        updateHeld (newestOutputPeak, outputPeakHeld, outputPeakHoldSeconds);
    }

    // ── Clip flash ────────────────────────────────────────────────────────
    if (hadNewClip)
        clipFlashAlpha = juce::jmin (1.0f, clipFlashAlpha + 0.25f);
    else
        clipFlashAlpha = juce::jmax (0.0f, clipFlashAlpha - 0.04f);

    repaint();
}

//==============================================================================
//==============================================================================
// Convert linear sample amplitude to the visualizer's uniform dB ruler.
static inline float peakToNorm (float linearPeak) noexcept
{
    const float db = juce::Decibels::gainToDecibels (juce::jmax (linearPeak, 1e-6f), -144.0f);
    constexpr float minimumDb = -48.0f;
    constexpr float maximumDb = 6.0f;
    return juce::jlimit (0.0f, 1.0f, (db - minimumDb) / (maximumDb - minimumDb));
}

//==============================================================================
namespace VizPalette
{
    static const juce::Colour dark    { 0xff090b12 };
    static const juce::Colour blue    { 0xff79e9f3 };
    static const juce::Colour pink    { 0xffff62b4 };

    static const juce::Colour inputWave  = blue;
    static const juce::Colour outputWave = pink;

    static const juce::Colour textRuler    { 0xff9995aa };
    static const juce::Colour textRulerDim { 0xff625b74 };
    static const juce::Colour textPinkMid  { 0xffd24c98 };
    static const juce::Colour textPinkFull { 0xffff62b4 };
}

//==============================================================================
void VisualizerComponent::buildSplinePath (juce::Path& strokePath,
                                           const std::vector<juce::Point<float>>& pts,
                                           float tension)
{
    if (pts.size() < 2) return;

    strokePath.startNewSubPath (pts[0]);

    const int n = static_cast<int> (pts.size());
    for (int i = 0; i < n - 1; ++i)
    {
        // Clamp neighbour indices so we never go out of bounds
        const auto& p0 = pts[static_cast<size_t> (juce::jmax (0, i - 1))];
        const auto& p1 = pts[static_cast<size_t> (i)];
        const auto& p2 = pts[static_cast<size_t> (i + 1)];
        const auto& p3 = pts[static_cast<size_t> (juce::jmin (n - 1, i + 2))];

        // Catmull-Rom → cubic Bézier control points
        const juce::Point<float> cp1 = p1 + (p2 - p0) * tension;
        const juce::Point<float> cp2 = p2 - (p3 - p1) * tension;

        strokePath.cubicTo (cp1, cp2, p2);
    }
}

void VisualizerComponent::buildFillPath (juce::Path& fillPath,
                                          const std::vector<juce::Point<float>>& pts,
                                          float vizX, float plotBot, float /*vizW*/)
{
    if (pts.empty()) return;

    fillPath.startNewSubPath (pts[0].x, plotBot);
    for (const auto& p : pts)
        fillPath.lineTo (p);
    fillPath.lineTo (pts.back().x, plotBot);
    fillPath.closeSubPath();
}

//==============================================================================
void VisualizerComponent::paint (juce::Graphics& g)
{
    namespace P = VizPalette;

    const auto bounds = getLocalBounds().toFloat();
    const float w = bounds.getWidth();
    const float h = bounds.getHeight();
    constexpr float rulerW = 36.0f;
    constexpr float plotTop = 14.0f;
    const float plotBot = h - 12.0f;
    const float plotH = juce::jmax (1.0f, plotBot - plotTop);
    const float vizX = rulerW;
    const float vizW = juce::jmax (0.0f, w - rulerW - 2.0f);

    auto normToY = [&] (float n) noexcept
    {
        return plotBot - juce::jlimit (0.0f, 1.0f, n) * plotH;
    };
    auto dbToY = [&] (float db) noexcept
    {
        return normToY (peakToNorm (juce::Decibels::decibelsToGain (db)));
    };

    juce::ColourGradient background (juce::Colour (0xff21162d), bounds.getX(), bounds.getY(),
                                     P::dark, bounds.getX(), bounds.getBottom(), false);
    g.setGradientFill (background);
    g.fillRoundedRectangle (bounds, 8.0f);

    juce::Path glassSweep;
    glassSweep.startNewSubPath (bounds.getX() + 2.0f, bounds.getY() + 2.0f);
    glassSweep.lineTo (bounds.getX() + bounds.getWidth() * 0.42f, bounds.getY() + 2.0f);
    glassSweep.cubicTo (bounds.getX() + bounds.getWidth() * 0.34f, bounds.getY() + 10.0f,
                        bounds.getX() + bounds.getWidth() * 0.22f, bounds.getY() + 22.0f,
                        bounds.getX() + bounds.getWidth() * 0.17f, bounds.getY() + bounds.getHeight() * 0.22f);
    glassSweep.lineTo (bounds.getX() + 2.0f, bounds.getY() + bounds.getHeight() * 0.30f);
    glassSweep.closeSubPath();
    juce::ColourGradient glassReflection (juce::Colour (0x18fff7ef), bounds.getX() + 14.0f, bounds.getY() + 2.0f,
                                          juce::Colours::transparentBlack,
                                          bounds.getX() + bounds.getWidth() * 0.34f,
                                          bounds.getY() + bounds.getHeight() * 0.22f, false);
    g.setGradientFill (glassReflection);
    g.fillPath (glassSweep);
    g.setColour (juce::Colour (0xff07080d).withAlpha (0.8f));
    g.drawRoundedRectangle (bounds.reduced (1.0f), 7.0f, 1.2f);

    const float headroomTop = dbToY (5.0f);
    const float headroomBottom = dbToY (0.0f);
    g.setColour (P::pink.withAlpha (0.07f));
    g.fillRect (vizX, headroomTop, vizW, headroomBottom - headroomTop);

    struct GridLine { float db; juce::Colour colour; };
    const GridLine grid[] =
    {
        {  6.0f, juce::Colour (0xff382238) },
        {  3.0f, juce::Colour (0xff2e1d32) }, {  0.0f, juce::Colour (0xff66304f) },
        { -3.0f, juce::Colour (0xff30283b) }, { -6.0f, juce::Colour (0xff2a2436) },
        {-12.0f, juce::Colour (0xff252130) }, {-24.0f, juce::Colour (0xff211d29) },
        {-48.0f, juce::Colour (0xff191720) }
    };
    for (const auto& line : grid)
    {
        g.setColour (line.colour);
        g.drawHorizontalLine (static_cast<int> (dbToY (line.db)), vizX, vizX + vizW);
    }

    g.setFont (ViveFonts::semiBold (9.5f));
    struct RulerMark { float db; juce::Colour text; juce::Colour tick; };
    const RulerMark marks[] =
    {
        {  6.0f, P::textPinkMid,  juce::Colour (0xff4a2040) },
        {  3.0f, P::textRulerDim, juce::Colour (0xff301828) },
        {  0.0f, P::textPinkFull, juce::Colour (0xff5a2848) },
        { -3.0f, P::textRuler,    juce::Colour (0xff2e2840) },
        { -6.0f, P::textRuler,    juce::Colour (0xff2a243a) },
        {-12.0f, P::textRulerDim, juce::Colour (0xff221e30) },
        {-24.0f, P::textRulerDim, juce::Colour (0xff1c1a28) },
        {-48.0f, juce::Colour (0xff383048), juce::Colour (0xff181520) }
    };
    for (const auto& mark : marks)
    {
        const float y = dbToY (mark.db);
        g.setColour (mark.tick);
        g.drawHorizontalLine (static_cast<int> (y), rulerW - 5.0f, rulerW);
        g.setColour (mark.text);
        const juce::String label = mark.db > 0.0f
            ? ("+" + juce::String (static_cast<int> (mark.db)))
            : juce::String (static_cast<int> (mark.db));
        g.drawText (label, 0, static_cast<int> (y) - 6,
                    static_cast<int> (rulerW) - 7, 12,
                    juce::Justification::centredRight, false);
    }

    g.setColour (juce::Colour (0xff221a22));
    g.drawHorizontalLine (static_cast<int> (plotBot), vizX, vizX + vizW);
    if (frameHistory.empty() || vizW <= 0.0f)
        return;

    const int numFrames = static_cast<int> (frameHistory.size());
    std::vector<float> frameXs (static_cast<size_t> (numFrames));
    float elapsedFromRight = 0.0f;
    for (int i = numFrames - 1; i >= 0; --i)
    {
        const float frameDuration = static_cast<float> (frameHistory[static_cast<size_t> (i)].durationSeconds);
        frameXs[static_cast<size_t> (i)] = vizX + vizW
            - (elapsedFromRight + frameDuration * 0.5f) / static_cast<float> (historyDurationSeconds) * vizW;
        elapsedFromRight += frameDuration;
    }

    std::vector<juce::Point<float>> inPeakPts, inRmsPts, outPeakPts, outRmsPts;
    inPeakPts.reserve (static_cast<size_t> (numFrames));
    inRmsPts.reserve (static_cast<size_t> (numFrames));
    outPeakPts.reserve (static_cast<size_t> (numFrames));
    outRmsPts.reserve (static_cast<size_t> (numFrames));
    int index = 0;
    for (const auto& frame : frameHistory)
    {
        const float x = frameXs[static_cast<size_t> (index++)];
        inPeakPts.push_back ({ x, normToY (peakToNorm (frame.inputPeak)) });
        inRmsPts.push_back ({ x, normToY (peakToNorm (frame.inputRms)) });
        outPeakPts.push_back ({ x, normToY (peakToNorm (frame.outputPeak)) });
        outRmsPts.push_back ({ x, normToY (peakToNorm (frame.outputRms)) });
    }

    juce::Path inputRmsFill;
    buildFillPath (inputRmsFill, inRmsPts, vizX, plotBot, vizW);
    g.setColour (P::inputWave.withAlpha (0.05f));
    g.fillPath (inputRmsFill);
    juce::Path inputPeakFill;
    buildFillPath (inputPeakFill, inPeakPts, vizX, plotBot, vizW);
    juce::ColourGradient inputGradient (P::inputWave.withAlpha (0.14f), vizX, plotTop,
                                        P::inputWave.withAlpha (0.0f), vizX, plotBot, false);
    g.setGradientFill (inputGradient);
    g.fillPath (inputPeakFill);
    juce::Path inputStroke;
    buildSplinePath (inputStroke, inPeakPts);
    g.setColour (P::inputWave.withAlpha (0.55f));
    g.strokePath (inputStroke, juce::PathStrokeType (1.0f,
        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (inputPeakHeld > 0.001f)
    {
        const float holdY = normToY (peakToNorm (inputPeakHeld));
        g.setColour (P::inputWave.withAlpha (0.75f));
        g.drawLine (juce::jmax (vizX, inPeakPts.back().x - 12.0f), holdY,
                    inPeakPts.back().x, holdY, 1.5f);
    }

    juce::Path outputRmsFill;
    buildFillPath (outputRmsFill, outRmsPts, vizX, plotBot, vizW);
    g.setColour (P::outputWave.withAlpha (0.06f));
    g.fillPath (outputRmsFill);
    juce::Path outputPeakFill;
    buildFillPath (outputPeakFill, outPeakPts, vizX, plotBot, vizW);
    juce::ColourGradient outputGradient (P::outputWave.withAlpha (0.18f), vizX, plotTop,
                                         P::outputWave.withAlpha (0.0f), vizX, plotBot, false);
    g.setGradientFill (outputGradient);
    g.fillPath (outputPeakFill);
    juce::Path outputStroke;
    buildSplinePath (outputStroke, outPeakPts);
    g.setColour (P::outputWave.withAlpha (0.90f));
    g.strokePath (outputStroke, juce::PathStrokeType (1.4f,
        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (outputPeakHeld > 0.001f)
    {
        const float holdY = normToY (peakToNorm (outputPeakHeld));
        g.setColour (P::outputWave.withAlpha (0.80f));
        g.drawLine (juce::jmax (vizX, outPeakPts.back().x - 12.0f), holdY,
                    outPeakPts.back().x, holdY, 1.5f);
    }

    if (clipFlashAlpha > 0.001f)
    {
        g.setColour (P::pink.withAlpha (clipFlashAlpha * 0.12f));
        g.fillRect (vizX, plotTop, vizW, plotH);
    }

    juce::Path glassReflectionPath;
    glassReflectionPath.startNewSubPath (bounds.getX() + 2.0f, bounds.getY() + 2.0f);
    glassReflectionPath.lineTo (bounds.getX() + bounds.getWidth() * 0.48f, bounds.getY() + 2.0f);
    glassReflectionPath.cubicTo (bounds.getX() + bounds.getWidth() * 0.41f, bounds.getY() + 15.0f,
                                 bounds.getX() + bounds.getWidth() * 0.30f, bounds.getY() + 34.0f,
                                 bounds.getX() + bounds.getWidth() * 0.22f, bounds.getY() + bounds.getHeight() * 0.28f);
    glassReflectionPath.lineTo (bounds.getX() + 2.0f, bounds.getY() + bounds.getHeight() * 0.38f);
    glassReflectionPath.closeSubPath();
    juce::ColourGradient reflection (juce::Colour (0x24fff7ee), bounds.getX() + 12.0f, bounds.getY() + 2.0f,
                                     juce::Colours::transparentBlack,
                                     bounds.getX() + bounds.getWidth() * 0.32f,
                                     bounds.getY() + bounds.getHeight() * 0.30f, false);
    g.setGradientFill (reflection);
    g.fillPath (glassReflectionPath);
}

float VisualizerComponent::calculateChunkRms (const juce::AudioBuffer<float>& buffer,
                                               int startSample, int numSamples) const
{
    double sum   = 0.0;  // accumulate in double even though samples are float —
                         // avoids precision loss when summing many small squared
                         // values over a long buffer (classic accumulator pattern).
    int    count = 0;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const float* data = buffer.getReadPointer (ch) + startSample;
        for (int i = 0; i < numSamples; ++i)
        {
            const double sv = data[i];
            sum += sv * sv;   // mean-SQUARE step
        }
        count += numSamples;
    }

    return count > 0 ? static_cast<float> (std::sqrt (sum / count)) : 0.0f;  // ROOT of the mean
}

void VisualizerComponent::resized() {}

void VisualizerComponent::setClippingThreshold (float newThresholdLinear)
{
    clippingThreshold.store (juce::jlimit (0.1f, 2.0f, newThresholdLinear),
                             std::memory_order_relaxed);
}
