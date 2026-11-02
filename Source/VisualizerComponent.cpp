#include "VisualizerComponent.h"

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
                                        const juce::AudioBuffer<float>& ceilingStageOutput)
{
    const int numSamples = input.getNumSamples();
    if (numSamples <= 0) return;

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
        // getMagnitude = true PEAK detection: max absolute sample value in the
        // chunk. Peak answers "did/will this clip" — it reacts instantly to the
        // single loudest sample, with no averaging.
        const float rawInPeak  = input .getMagnitude (0, s, chunkLen);
        const float rawOutPeak = output.getMagnitude (0, s, chunkLen);
        const float rawCeilingPeak = ceilingStageOutput.getMagnitude (0, s, chunkLen);

        // Classic asymmetric one-pole envelope follower ("ballistics"):
        // attackCoeff controls how fast the meter jumps UP toward a new peak,
        // releaseCoeff controls how slowly it decays back down. Using a much
        // faster attack than release (0.70 vs a *multiplicative* decay of 0.92,
        // i.e. ~8% loss per chunk) is what gives a peak meter its familiar
        // "snap up instantly, fall gently" visual behavior, rather than jittering
        // with every sample.
        constexpr float attackCoeff  = 0.70f;
        constexpr float releaseCoeff = 0.92f;

        auto follow = [&] (float raw, float& state) -> float
        {
            if (raw > state)  state += (raw - state) * attackCoeff;  // fast attack toward new peak
            else              state *= releaseCoeff;                 // slow exponential release
            return state;
        };

        frame.inputPeak  = follow (rawInPeak,  smoothedInputPeak);
        frame.outputPeak = follow (rawOutPeak, smoothedOutputPeak);

        // RMS (root-mean-square) is the complementary measurement to peak:
        // it reflects perceived LOUDNESS/energy over the window rather than
        // the single hottest sample — see calculateChunkRms below for the math.
        frame.inputRms  = calculateChunkRms (input,  s, chunkLen);
        frame.outputRms = calculateChunkRms (output, s, chunkLen);

        // Convert linear peak amplitude to dB: dB = 20*log10(amplitude).
        // gainToDecibels(x, floorDb) clamps the result at floorDb (-144 here,
        // i.e. below the practical noise floor of a 24-bit signal) to avoid
        // -inf when the signal is exactly silent (log10(0) is undefined).
        const float preDb  = juce::Decibels::gainToDecibels (juce::jmax (rawInPeak,  1e-6f), -144.0f);
        const float postDb = juce::Decibels::gainToDecibels (juce::jmax (rawOutPeak, 1e-6f), -144.0f);

        // Gain reduction = how many dB quieter the output got vs the input at
        // this instant. jmin(0, ...) clamps to "reduction only" — if output is
        // somehow louder than input (e.g. makeup gain in the saturator), that's
        // not "gain reduction" in the classic compressor-meter sense, so it's
        // floored at 0 dB rather than showing a positive number.
        frame.gainReductionDb = juce::jmin (0.0f, postDb - preDb);
        frame.isClipping      = rawCeilingPeak >= clippingThreshold * 0.999f;
        frame.isSoftClipping  = !frame.isClipping && rawCeilingPeak > softClippingThreshold;

        // "Overage" = how far the INPUT peak (pre-clip) exceeds the ceiling,
        // in dB — i.e. how hard the clipper is having to work right now, useful
        // as a "you're pushing this X dB into the ceiling" readout distinct from
        // the boolean isClipping flag.
        const float ceilingDb = juce::Decibels::gainToDecibels (clippingThreshold, -144.0f);
        frame.overageDb = juce::jmax (0.0f, preDb - ceilingDb);

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
    // ── Drain FIFO into frameHistory ──────────────────────────────────────
    // prepareToRead/finishedRead is the reader half of the AbstractFifo contract,
    // mirroring prepareToWrite/finishedWrite above. Passing fifoSize just means
    // "give me as much as is currently available, up to the full buffer."
    int start1, size1, start2, size2;
    fifo.prepareToRead (fifoSize, start1, size1, start2, size2);

    for (int i = 0; i < size1; ++i)
        frameHistory.push_back (fifoBuffer[static_cast<size_t> (start1 + i)]);
    for (int i = 0; i < size2; ++i)
        frameHistory.push_back (fifoBuffer[static_cast<size_t> (start2 + i)]);

    fifo.finishedRead (size1 + size2);

    // Ring-buffer trim: cap how much waveform history is kept on screen so
    // frameHistory doesn't grow unbounded and paint() stays O(maxFrames) instead
    // of O(session length).
    while (static_cast<int> (frameHistory.size()) > maxFrames)
        frameHistory.pop_front();

    // ── Overage smoother ──────────────────────────────────────────────────
    // Same fast-attack/slow-release ballistics idea as the peak follower in
    // pushSamples, just applied at UI-frame rate (60Hz) instead of audio-chunk
    // rate, and with additive rather than multiplicative release (linear decay).
    if (!frameHistory.empty())
    {
        float peakOverage = 0.0f;
        for (const auto& f : frameHistory)
            peakOverage = juce::jmax (peakOverage, f.overageDb);

        if (peakOverage > smoothedOverageDb)
            smoothedOverageDb += (peakOverage - smoothedOverageDb) * 0.25f;
        else
            smoothedOverageDb = juce::jmax (0.0f, smoothedOverageDb - 0.10f);
    }
    else
    {
        smoothedOverageDb = juce::jmax (0.0f, smoothedOverageDb - 0.10f);
    }

    // ── Peak hold (UI thread) ─────────────────────────────────────────────
    // Classic "peak hold" meter ballistics: when a new peak arrives, latch it
    // and hold it visually for peakHoldDuration frames (~1.5s @ 60Hz) before
    // letting it decay linearly. This is why hardware/software meters show a
    // little static line that "remembers" the last loud peak instead of the
    // needle immediately snapping back down.
    {
        const float latestIn  = frameHistory.empty() ? 0.0f : frameHistory.back().inputPeak;
        const float latestOut = frameHistory.empty() ? 0.0f : frameHistory.back().outputPeak;

        auto updateHeld = [] (float current, float& held, int& holdFrames)
        {
            if (current >= held)
            {
                held       = current;
                holdFrames = peakHoldDuration;
            }
            else if (holdFrames > 0)
            {
                --holdFrames;
            }
            else
            {
                held = juce::jmax (0.0f, held - peakDecayRate);
            }
        };

        updateHeld (latestIn,  inputPeakHeld,  inputPeakHoldFrames);
        updateHeld (latestOut, outputPeakHeld, outputPeakHoldFrames);
    }

    // ── Clip flash ────────────────────────────────────────────────────────
    if (!frameHistory.empty() && frameHistory.back().isClipping)
        clipFlashAlpha = juce::jmin (1.0f, clipFlashAlpha + 0.25f);
    else
        clipFlashAlpha = juce::jmax (0.0f, clipFlashAlpha - 0.04f);

    repaint();
}

//==============================================================================
void VisualizerComponent::mouseDown (const juce::MouseEvent& e)
{
    const auto  bounds = getLocalBounds().toFloat();
    const float w = bounds.getWidth();
    const float h = bounds.getHeight();

    constexpr float rulerW = 36.0f;
    constexpr float grBarW =  4.0f;
    constexpr float grPad  =  6.0f;
    const     float plotBot = h - 12.0f;
    const     float vizW   = w - rulerW - grBarW - grPad - 2.0f;
    const     float py     = plotBot - 13.0f;
    const     float inX    = rulerW + vizW - 76.0f;
    const     float ouX    = inX + 40.0f;

    const juce::Rectangle<float> inPill  { inX, py, 33.0f, 11.0f };
    const juce::Rectangle<float> outPill { ouX, py, 36.0f, 11.0f };

    if (inPill .contains (e.position)) { showInput  = !showInput;  repaint(); }
    if (outPill.contains (e.position)) { showOutput = !showOutput; repaint(); }
}

//==============================================================================
// INTERVIEW NOTE: this remaps a linear 0..1 peak value onto a non-linear (roughly
// logarithmic/dB-scaled) 0..1 vertical position for the meter display, using
// PIECEWISE-LINEAR segments in dB space with different slopes per range. This
// mimics how real hardware/analog VU-style meters allocate MORE physical screen
// space to the loud end (near 0dB, where clipping decisions matter) and
// compress the quiet end (below -24dB) into a small sliver — because the human
// ear and the engineering decisions that matter both care disproportionately
// about levels near the ceiling, not small differences at -50dB.
static inline float peakToNorm (float linearPeak) noexcept
{
    const float db = juce::Decibels::gainToDecibels (juce::jmax (linearPeak, 1e-6f), -144.0f);

    // Each branch is a linear interpolation (lerp) within one dB range, mapped
    // onto a hand-tuned slice of the 0..1 output range — the breakpoints
    // (0, -6, -12, -24 dB) are the classic "meter ballistics" dB markers you'll
    // see on hardware meters, each getting proportionally more screen real estate
    // the closer it is to 0dB/clipping.
    if (db >=   0.0f) return juce::jlimit (0.0f, 1.0f, 0.82f + (db / 5.0f) * 0.18f);
    if (db >=  -6.0f) return juce::jlimit (0.0f, 1.0f, 0.62f + (db +  6.0f) /  6.0f * 0.20f);
    if (db >= -12.0f) return juce::jlimit (0.0f, 1.0f, 0.44f + (db + 12.0f) /  6.0f * 0.18f);
    if (db >= -24.0f) return juce::jlimit (0.0f, 1.0f, 0.22f + (db + 24.0f) / 12.0f * 0.22f);
    return juce::jlimit (0.0f, 1.0f, juce::jmax (0.0f, (db + 48.0f) / 24.0f * 0.22f));
}

//==============================================================================
namespace VizPalette
{
    static const juce::Colour dark    { 0xff090b12 };
    static const juce::Colour cream   { 0xfff5eaf4 };
    static const juce::Colour blue    { 0xff79e9f3 };
    static const juce::Colour pink    { 0xffff62b4 };

    static const juce::Colour inputWave  = blue;
    static const juce::Colour outputWave = pink;

    static const juce::Colour textRuler    { 0xff9995aa };
    static const juce::Colour textRulerDim { 0xff625b74 };
    static const juce::Colour textPinkMid  { 0xffd24c98 };
    static const juce::Colour textPinkFull { 0xffff62b4 };
    static const juce::Colour textBlueMid  { 0xff79e9f3 };
    static const juce::Colour borderViz    { 0xff58465f };
}

//==============================================================================
juce::Colour VisualizerComponent::getGRColour (float grDb) const
{
    const float t = juce::jlimit (0.0f, 1.0f, -grDb / 24.0f);
    return VizPalette::blue.interpolatedWith (VizPalette::pink, t);
}

juce::Colour VisualizerComponent::getOverageColour (float overageDb) const
{
    const float t = juce::jlimit (0.0f, 1.0f, overageDb / 12.0f);
    return VizPalette::blue.interpolatedWith (VizPalette::pink, t).brighter (t * 0.4f);
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

    const auto  bounds  = getLocalBounds().toFloat();
    const float w = bounds.getWidth();
    const float h = bounds.getHeight();

    constexpr float rulerW   = 36.0f;
    constexpr float grBarW   =  4.0f;
    constexpr float grPad    =  6.0f;
    constexpr float plotTop  = 14.0f;
    const     float plotBot  = h - 12.0f;
    const     float plotH    = plotBot - plotTop;
    const     float vizX     = rulerW;
    const     float vizW     = w - rulerW - grBarW - grPad - 2.0f;

    auto normToY = [&] (float n) noexcept -> float
    {
        return plotBot - juce::jlimit (0.0f, 1.0f, n) * plotH;
    };

    auto dbToY = [&] (float db) noexcept -> float
    {
        return normToY (peakToNorm (juce::Decibels::decibelsToGain (db)));
    };

    // ── 1. Background ─────────────────────────────────────────────────────
    juce::ColourGradient smokedGlass (juce::Colour (0xff21162d), bounds.getX(), bounds.getY(),
                                      P::dark, bounds.getX(), bounds.getBottom(), false);
    g.setGradientFill (smokedGlass);
    g.fillRoundedRectangle (bounds, 8.0f);

    // A restrained reflection on the glass face, kept inside the meter opening.
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

    // ── 2. Headroom zone tint ─────────────────────────────────────────────
    {
        const float zoneTop = dbToY ( 5.0f);
        const float zoneBot = dbToY ( 0.0f);
        g.setColour (P::pink.withAlpha (0.07f));
        g.fillRect (vizX, zoneTop, vizW, zoneBot - zoneTop);
    }

    // ── 3. Grid lines ─────────────────────────────────────────────────────
    {
        struct GridLine { float db; juce::Colour col; };
        const GridLine grid[] =
        {
            {  5.0f, juce::Colour (0xff382238) },
            {  3.0f, juce::Colour (0xff2e1d32) },
            {  0.0f, juce::Colour (0xff66304f) },
            { -3.0f, juce::Colour (0xff30283b) },
            { -6.0f, juce::Colour (0xff2a2436) },
            {-12.0f, juce::Colour (0xff252130) },
            {-24.0f, juce::Colour (0xff211d29) },
            {-48.0f, juce::Colour (0xff191720) },
        };

        for (auto& gl : grid)
        {
            g.setColour (gl.col);
            g.drawHorizontalLine (static_cast<int> (dbToY (gl.db)), vizX, vizX + vizW);
        }
    }

    // ── 4. Ceiling dashed line ────────────────────────────────────────────
    {
        const float ceilDb = juce::Decibels::gainToDecibels (clippingThreshold, -144.0f);
        const float cy     = dbToY (ceilDb);

        constexpr float dashLen = 4.0f;
        constexpr float gapLen  = 3.0f;

        g.setColour (P::pink.withAlpha (0.35f));
        float dx    = vizX;
        bool drawing = true;
        while (dx < vizX + vizW)
        {
            const float segEnd = juce::jmin (dx + (drawing ? dashLen : gapLen), vizX + vizW);
            if (drawing)
                g.drawLine (dx, cy, segEnd, cy, 0.8f);
            dx = segEnd;
            drawing = !drawing;
        }
    }

    // ── 5. dB ruler ───────────────────────────────────────────────────────
    {
        g.setFont (juce::Font (juce::FontOptions()
            .withName  (juce::Font::getDefaultSansSerifFontName())
            .withHeight (9.5f)
            .withStyle  ("Bold")));

        struct RulerMark { float db; juce::Colour textCol; juce::Colour tickCol; };
        const RulerMark marks[] =
        {
            {  5.0f, P::textPinkMid,  juce::Colour (0xff4a2040) },
            {  3.0f, P::textRulerDim, juce::Colour (0xff301828) },
            {  0.0f, P::textPinkFull, juce::Colour (0xff5a2848) },
            { -3.0f, P::textRuler,    juce::Colour (0xff2e2840) },
            { -6.0f, P::textRuler,    juce::Colour (0xff2a243a) },
            {-12.0f, P::textRulerDim, juce::Colour (0xff221e30) },
            {-24.0f, P::textRulerDim, juce::Colour (0xff1c1a28) },
            {-48.0f, juce::Colour (0xff383048), juce::Colour (0xff181520) },
        };

        for (auto& m : marks)
        {
            const float y = dbToY (m.db);

            g.setColour (m.tickCol);
            g.drawHorizontalLine (static_cast<int> (y), rulerW - 5.0f, rulerW);

            g.setColour (m.textCol);
            const juce::String lbl = (m.db > 0.0f)
                ? ("+" + juce::String (static_cast<int> (m.db)))
                :         juce::String (static_cast<int> (m.db));
            g.drawText (lbl,
                        0, static_cast<int> (y) - 6,
                        static_cast<int> (rulerW) - 7, 12,
                        juce::Justification::centredRight, false);
        }
    }

    // ── 6. Baseline rule ──────────────────────────────────────────────────
    g.setColour (juce::Colour (0xff221a22));
    g.drawHorizontalLine (static_cast<int> (plotBot), vizX, vizX + vizW);

    // ── 7. GR bar ─────────────────────────────────────────────────────────
    {
        const float grX = vizX + vizW + grPad;

        g.setColour (juce::Colour (0xff1e1620));
        g.fillRoundedRectangle (grX, plotTop, grBarW, plotH, grBarW * 0.5f);

        if (!frameHistory.empty())
        {
            const float gr = frameHistory.back().gainReductionDb;
            if (gr < -0.1f)
            {
                const float fill    = juce::jlimit (0.0f, 1.0f, -gr / 30.0f);
                const float filledH = fill * plotH;
                const juce::Colour col = getGRColour (gr);

                g.setColour (col.withAlpha (0.85f));
                g.fillRoundedRectangle (grX, plotTop, grBarW, filledH, grBarW * 0.5f);
            }
        }
    }

    if (frameHistory.empty()) return;

    const int   numFrames = static_cast<int> (frameHistory.size());
    const float xStep     = vizW / static_cast<float> (juce::jmax (numFrames - 1, 1));

    // ── 8. Build point lists ──────────────────────────────────────────────
    std::vector<juce::Point<float>> inPeakPts, inRmsPts, outPeakPts, outRmsPts;

    if (showInput)
    {
        inPeakPts.reserve (static_cast<size_t> (numFrames));
        inRmsPts .reserve (static_cast<size_t> (numFrames));
        int idx = 0;
        for (const auto& frame : frameHistory)
        {
            const float x = vizX + idx * xStep;
            inPeakPts.push_back ({ x, normToY (peakToNorm (frame.inputPeak)) });
            inRmsPts .push_back ({ x, normToY (peakToNorm (frame.inputRms))  });
            ++idx;
        }
    }

    if (showOutput)
    {
        outPeakPts.reserve (static_cast<size_t> (numFrames));
        outRmsPts .reserve (static_cast<size_t> (numFrames));
        int idx = 0;
        for (const auto& frame : frameHistory)
        {
            const float x = vizX + idx * xStep;
            outPeakPts.push_back ({ x, normToY (peakToNorm (frame.outputPeak)) });
            outRmsPts .push_back ({ x, normToY (peakToNorm (frame.outputRms))  });
            ++idx;
        }
    }

    // ── 9. Input waveform — blue ──────────────────────────────────────────
    if (showInput && !inPeakPts.empty())
    {
        // RMS fill
        juce::Path rmsF;
        buildFillPath (rmsF, inRmsPts, vizX, plotBot, vizW);
        g.setColour (P::inputWave.withAlpha (0.05f));
        g.fillPath (rmsF);

        // Peak fill — gradient
        juce::Path peakF;
        buildFillPath (peakF, inPeakPts, vizX, plotBot, vizW);
        {
            juce::ColourGradient grad (P::inputWave.withAlpha (0.14f), vizX, plotTop,
                                       P::inputWave.withAlpha (0.0f),  vizX, plotBot,
                                       false);
            g.setGradientFill (grad);
            g.fillPath (peakF);
        }

        // Peak stroke — spline
        juce::Path peakS;
        buildSplinePath (peakS, inPeakPts);
        g.setColour (P::inputWave.withAlpha (0.55f));
        g.strokePath (peakS, juce::PathStrokeType (1.0f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Peak-hold tick
        if (inputPeakHeld > 0.001f)
        {
            const float holdY = normToY (peakToNorm (inputPeakHeld));
            const float tickX = inPeakPts.back().x;
            g.setColour (P::inputWave.withAlpha (0.75f));
            g.drawLine (juce::jmax (vizX, tickX - 12.0f), holdY, tickX, holdY, 1.5f);
        }
    }

    // ── 10. Output waveform — pink ─────────────────────────────────────────
    if (showOutput && !outPeakPts.empty())
    {
        // RMS fill
        juce::Path rmsF;
        buildFillPath (rmsF, outRmsPts, vizX, plotBot, vizW);
        g.setColour (P::outputWave.withAlpha (0.06f));
        g.fillPath (rmsF);

        // Peak fill — gradient
        juce::Path peakF;
        buildFillPath (peakF, outPeakPts, vizX, plotBot, vizW);
        {
            juce::ColourGradient grad (P::outputWave.withAlpha (0.18f), vizX, plotTop,
                                       P::outputWave.withAlpha (0.0f),  vizX, plotBot,
                                       false);
            g.setGradientFill (grad);
            g.fillPath (peakF);
        }

        // Peak stroke — spline
        juce::Path peakS;
        buildSplinePath (peakS, outPeakPts);
        g.setColour (P::outputWave.withAlpha (0.90f));
        g.strokePath (peakS, juce::PathStrokeType (1.4f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Peak-hold tick
        if (outputPeakHeld > 0.001f)
        {
            const float holdY = normToY (peakToNorm (outputPeakHeld));
            const float tickX = outPeakPts.back().x;
            g.setColour (P::outputWave.withAlpha (0.80f));
            g.drawLine (juce::jmax (vizX, tickX - 12.0f), holdY, tickX, holdY, 1.5f);
        }
    }

    // ── 11. Clip flash overlay ────────────────────────────────────────────
    if (clipFlashAlpha > 0.001f)
    {
        g.setColour (P::pink.withAlpha (clipFlashAlpha * 0.12f));
        g.fillRect (vizX, plotTop, vizW, plotH);
    }

    // ── 12. Overage bar ───────────────────────────────────────────────────
    {
        constexpr float barH      =  3.0f;
        constexpr float barRadius =  1.5f;
        constexpr float maxOverage = 12.0f;
        constexpr float labelW    = 58.0f;

        const float barX = vizX;
        const float barY = 1.0f;
        const float barW = vizW - labelW - 4.0f;

        g.setColour (juce::Colour (0xff201820));
        g.fillRoundedRectangle (barX, barY, barW, barH, barRadius);

        if (smoothedOverageDb > 0.05f)
        {
            const float fill = juce::jlimit (0.0f, 1.0f, smoothedOverageDb / maxOverage);
            const float fillW = barW * fill;
            const juce::Colour col = getOverageColour (smoothedOverageDb);

            g.setColour (col.withAlpha (0.20f));
            g.fillRoundedRectangle (barX, barY - 1.0f, fillW, barH + 2.0f, barRadius + 0.5f);

            g.setColour (col.withAlpha (0.92f));
            g.fillRoundedRectangle (barX, barY, fillW, barH, barRadius);

            g.setFont (juce::Font (juce::FontOptions()
                .withName  (juce::Font::getDefaultSansSerifFontName())
                .withHeight (10.0f)
                .withStyle  ("Bold")));
            g.setColour (col);
            const juce::String ovLbl = "+" + juce::String (smoothedOverageDb, 1) + " dB";
            g.drawText (ovLbl,
                        static_cast<int> (barX + barW + 5.0f),
                        static_cast<int> (barY - 2.0f),
                        static_cast<int> (labelW),
                        static_cast<int> (barH + 6.0f),
                        juce::Justification::centredLeft, false);
        }
    }

    // ── 13. GR readout text ───────────────────────────────────────────────
    {
        const float gr = frameHistory.back().gainReductionDb;
        if (gr < -0.1f)
        {
            g.setFont (juce::Font (juce::FontOptions()
                .withName  (juce::Font::getDefaultSansSerifFontName())
                .withHeight (10.0f)
                .withStyle  ("Bold")));
            g.setColour (getGRColour (gr));
            g.drawText ("GR  " + juce::String (gr, 1) + " dB",
                        static_cast<int> (vizX + 6.0f),
                        static_cast<int> (plotTop + 4.0f),
                        120, 12,
                        juce::Justification::centredLeft, false);
        }
    }

    // ── 14. IN / OUT legend pills ─────────────────────────────────────────
    {
        g.setFont (juce::Font (juce::FontOptions()
            .withName  (juce::Font::getDefaultSansSerifFontName())
            .withHeight (9.5f)
            .withStyle  ("Bold")));

        const float py  = plotBot - 14.0f;
        const float inX = vizX + vizW - 76.0f;
        const float ouX = inX + 40.0f;

        // IN pill
        {
            const bool on = showInput;
            g.setColour (on ? juce::Colour (0xff1a2030) : juce::Colour (0xff181418));
            g.fillRoundedRectangle (inX, py, 33.0f, 11.0f, 3.5f);

            g.setColour (on ? juce::Colour (0xff3a5080) : VizPalette::borderViz);
            g.drawRoundedRectangle (inX + 0.5f, py + 0.5f, 32.0f, 10.0f, 3.5f, 1.0f);

            g.setColour (on ? P::textBlueMid : P::textRulerDim);
            g.drawText ("IN",
                        static_cast<int> (inX), static_cast<int> (py),
                        33, 11, juce::Justification::centred, false);
        }

        // OUT pill
        {
            const bool on = showOutput;
            g.setColour (on ? juce::Colour (0xff2a1020) : juce::Colour (0xff181418));
            g.fillRoundedRectangle (ouX, py, 36.0f, 11.0f, 3.5f);

            g.setColour (on ? juce::Colour (0xff6a2050) : VizPalette::borderViz);
            g.drawRoundedRectangle (ouX + 0.5f, py + 0.5f, 35.0f, 10.0f, 3.5f, 1.0f);

            g.setColour (on ? P::textPinkFull : P::textRulerDim);
            g.drawText ("OUT",
                        static_cast<int> (ouX), static_cast<int> (py),
                        36, 11, juce::Justification::centred, false);
        }
    }

    // Soft, diagonal specular reflection on the glass face, over the meter image.
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

    juce::Path reflectionEdge;
    reflectionEdge.startNewSubPath (bounds.getX() + 5.0f, bounds.getY() + 3.0f);
    reflectionEdge.cubicTo (bounds.getX() + bounds.getWidth() * 0.22f, bounds.getY() + 2.0f,
                            bounds.getX() + bounds.getWidth() * 0.40f, bounds.getY() + 3.0f,
                            bounds.getX() + bounds.getWidth() * 0.47f, bounds.getY() + 4.0f);
    g.setColour (juce::Colour (0x28fffaf3));
    g.strokePath (reflectionEdge, juce::PathStrokeType (1.0f,
        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

//==============================================================================
// INTERVIEW NOTE: this is the textbook RMS formula — sqrt(mean(x^2)) — computed
// across ALL channels pooled together (count accumulates numSamples per channel),
// so a stereo signal's RMS here reflects combined energy across both channels
// rather than per-channel RMS. Worth being explicit about that distinction if asked.
float VisualizerComponent::calculateRms (const juce::AudioBuffer<float>& buffer) const
{
    return calculateChunkRms (buffer, 0, buffer.getNumSamples());
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
    clippingThreshold     = juce::jlimit (0.1f, 2.0f, newThresholdLinear);
    // Soft-clip knee always sits at 95% of the hard ceiling — keeps a small
    // buffer zone between "starting to round off" and "hard limit", regardless
    // of what the user sets the ceiling to.
    softClippingThreshold = clippingThreshold * 0.95f;
}
