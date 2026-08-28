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
                                        const juce::AudioBuffer<float>& output)
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
        frame.isClipping      = rawOutPeak > clippingThreshold;
        frame.isSoftClipping  = !frame.isClipping && rawOutPeak > softClippingThreshold;

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

    // ── Cat state ─────────────────────────────────────────────────────────
    // Purely cosmetic UI state machine driven off output peak level / clipping
    // flag — not DSP, just a fun visual layer on top of the metering data above.
    {
        const float outPeak = frameHistory.empty() ? 0.0f : frameHistory.back().outputPeak;
        const bool  clipping = !frameHistory.empty() && frameHistory.back().isClipping;

        CatState newState;
        if      (clipping)          newState = CatState::Clipping;
        else if (outPeak > 0.70f)   newState = CatState::Peak;
        else if (outPeak > 0.45f)   newState = CatState::Nervous;
        else if (outPeak > 0.08f)   newState = CatState::Active;
        else                        newState = CatState::Idle;

        // Trigger shake decay when transitioning out of Clipping
        if (catState == CatState::Clipping && newState != CatState::Clipping)
            catShakeDecay = 1.0f;
        catState = newState;

        catShakeDecay = juce::jmax (0.0f, catShakeDecay - 0.03f);

        constexpr float bobRate    = 1.0f / (60.0f * 2.4f);
        constexpr float panicRate  = 1.0f / (60.0f * 0.12f);
        const float phaseRate = (catState == CatState::Clipping) ? panicRate : bobRate;
        catAnimPhase = std::fmod (catAnimPhase + phaseRate, 1.0f);

        catBlinkPhase += 1.0f / (60.0f * 4.0f);
        if (catBlinkPhase >= 1.0f) { catBlinkPhase = 0.0f; catBlinking = !catBlinking; }
        if (catState == CatState::Clipping || catState == CatState::Peak)
            catBlinking = false;

        // Smooth mouth open target
        float mouthTarget = 0.0f;
        if      (catState == CatState::Clipping) mouthTarget = 1.0f;
        else if (catState == CatState::Peak)     mouthTarget = 0.7f;
        else if (catState == CatState::Nervous)  mouthTarget = 0.2f;
        catMouthOpen += (mouthTarget - catMouthOpen) * 0.12f;
    }

    // ── Clip flash ────────────────────────────────────────────────────────
    if (catState == CatState::Clipping)
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
    static const juce::Colour dark    { 0xff110b11 };
    static const juce::Colour cream   { 0xfff9dec9 };
    static const juce::Colour blue    { 0xff99b2dd };
    static const juce::Colour pink    { 0xffde369d };

    static const juce::Colour inputWave  = blue;
    static const juce::Colour outputWave = pink;

    static const juce::Colour textRuler    { 0xff7a8aaa };
    static const juce::Colour textRulerDim { 0xff4a5268 };
    static const juce::Colour textPinkMid  { 0xffb83080 };
    static const juce::Colour textPinkFull { 0xffde369d };
    static const juce::Colour textBlueMid  { 0xff6a80aa };
    static const juce::Colour borderViz    { 0xff2a1f2a };
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
    g.setColour (juce::Colour (0xff160d16));
    g.fillRoundedRectangle (bounds, 8.0f);

    g.setColour (VizPalette::borderViz);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

    // ── 2. Headroom zone tint ─────────────────────────────────────────────
    {
        const float zoneTop = dbToY ( 5.0f);
        const float zoneBot = dbToY ( 0.0f);
        g.setColour (P::pink.withAlpha (0.04f));
        g.fillRect (vizX, zoneTop, vizW, zoneBot - zoneTop);
    }

    // ── 3. Grid lines ─────────────────────────────────────────────────────
    {
        struct GridLine { float db; juce::Colour col; };
        const GridLine grid[] =
        {
            {  5.0f, juce::Colour (0xff2a1525) },
            {  3.0f, juce::Colour (0xff241020) },
            {  0.0f, juce::Colour (0xff3a1a32) },
            { -3.0f, juce::Colour (0xff1e1a26) },
            { -6.0f, juce::Colour (0xff1c1822) },
            {-12.0f, juce::Colour (0xff1a161e) },
            {-24.0f, juce::Colour (0xff17141a) },
            {-48.0f, juce::Colour (0xff151218) },
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

    // ── 15. Cat mascot ────────────────────────────────────────────────────
    {
        const float catX = vizX + vizW - 20.0f;
        const float catY = (dbToY (5.0f) + dbToY (0.0f)) * 0.5f;
        paintCat (g, catX, catY, 0.75f);
    }
}

//==============================================================================
// NOTE: paintCat() below is purely cosmetic mascot rendering (path drawing for
// ears/eyes/tail/whiskers etc.), driven by the CatState set in timerCallback.
// Not DSP-relevant — safe to skim past in an interview unless asked about UI/
// animation architecture specifically.
void VisualizerComponent::paintCat (juce::Graphics& g,
                                     float cx, float cy, float scale) const
{
    const float twoPi = juce::MathConstants<float>::twoPi;

    // ── Offset / bob ──────────────────────────────────────────────────────
    float bobY = 0.0f;
    float bobX = 0.0f;

    if (catState == CatState::Clipping)
    {
        // Chaotic panic shake: two layered frequencies
        bobX = std::sin (catAnimPhase * twoPi)        * 2.5f
             + std::sin (catAnimPhase * twoPi * 2.7f) * 1.2f;
    }
    else if (catShakeDecay > 0.0f)
    {
        // Post-clip settling shake, decaying amplitude
        bobX = std::sin (catAnimPhase * twoPi * 3.0f) * catShakeDecay * 2.0f;
    }
    else
    {
        // Organic non-linear bob — spends more time at rest, pops up briefly
        const float raw = std::sin (catAnimPhase * twoPi);
        bobY = std::pow (std::abs (raw), 0.4f) * (raw < 0 ? -1.0f : 1.0f) * 1.4f;
    }

    const float ox = cx + bobX;
    const float oy = cy + bobY;
    auto scl = [&] (float v) { return v * scale; };

    // ── Colours ───────────────────────────────────────────────────────────
    const juce::Colour bodyFill  { 0xff1e1220 };
    const juce::Colour blue      { 0xff99b2dd };
    const juce::Colour pink      { 0xffde369d };
    const juce::Colour red       { 0xffff6666 };
    const juce::Colour bodyStroke = (catState == CatState::Clipping) ? red
                                  : (catState == CatState::Peak
                                  || catState == CatState::Nervous)  ? pink
                                  : blue;
    const float strokeW = (catState == CatState::Clipping) ? 1.4f : 1.0f;

    // ── Tail ──────────────────────────────────────────────────────────────
    // Built as a tapered filled path so it's thick at base, pointed at tip
    {
        // Compute the spine points based on state
        juce::Point<float> p0, p1, p2;
        p0 = { ox + scl(11), oy + scl(19) };

        if (catState == CatState::Clipping)
        {
            // Puffed-up panic tail: short, bristled upward
            p1 = { ox + scl(26), oy + scl(8)  };
            p2 = { ox + scl(20), oy - scl(6)  };
        }
        else if (catState == CatState::Peak || catState == CatState::Nervous)
        {
            p1 = { ox + scl(26), oy + scl(12) };
            p2 = { ox + scl(22), oy + scl(2)  };
        }
        else
        {
            p1 = { ox + scl(26), oy + scl(22) };
            p2 = { ox + scl(20), oy + scl(8)  };
        }

        if (catState == CatState::Clipping)
        {
            // Jagged puffed tail — draw as thick bristles radiating outward
            g.setColour (red.withAlpha (0.85f));
            const int bristles = 7;
            for (int i = 0; i < bristles; ++i)
            {
                const float t    = static_cast<float>(i) / (bristles - 1);
                const float bx   = p0.x + (p2.x - p0.x) * t;
                const float by   = p0.y + (p2.y - p0.y) * t;
                const float perpAngle = -0.6f + t * 1.2f;  // fan angle
                const float blen = scl(5.0f + std::sin(t * 3.14f) * 4.0f);
                g.drawLine (bx, by,
                            bx + std::cos(perpAngle) * blen,
                            by + std::sin(perpAngle) * blen,
                            scl(1.8f));
            }
            // Core tail stroke underneath
            juce::Path tail;
            tail.startNewSubPath (p0);
            tail.quadraticTo     (p1, p2);
            g.setColour (red.withAlpha (0.60f));
            g.strokePath (tail, juce::PathStrokeType (scl(2.0f),
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else
        {
            // Normal tail: tapered filled path
            // Build two offset sides of the spine, fat at base, thin at tip
            auto evalQuad = [&](float t) -> juce::Point<float>
            {
                const float u = 1.0f - t;
                return { u*u*p0.x + 2*u*t*p1.x + t*t*p2.x,
                         u*u*p0.y + 2*u*t*p1.y + t*t*p2.y };
            };
            auto evalQuadTangent = [&](float t) -> juce::Point<float>
            {
                const float u = 1.0f - t;
                return { 2*u*(p1.x-p0.x) + 2*t*(p2.x-p1.x),
                         2*u*(p1.y-p0.y) + 2*t*(p2.y-p1.y) };
            };

            juce::Path tailFill;
            constexpr int steps = 12;
            // Left side (base→tip)
            for (int i = 0; i <= steps; ++i)
            {
                const float t    = static_cast<float>(i) / steps;
                const auto  pt   = evalQuad(t);
                const auto  tan  = evalQuadTangent(t);
                const float len  = std::sqrt(tan.x*tan.x + tan.y*tan.y) + 1e-6f;
                const float nx   = -tan.y / len;
                const float ny   =  tan.x / len;
                const float half = scl(2.5f - 2.0f * t);   // taper 2.5→0.5
                if (i == 0) tailFill.startNewSubPath (pt.x + nx*half, pt.y + ny*half);
                else        tailFill.lineTo          (pt.x + nx*half, pt.y + ny*half);
            }
            // Right side (tip→base)
            for (int i = steps; i >= 0; --i)
            {
                const float t    = static_cast<float>(i) / steps;
                const auto  pt   = evalQuad(t);
                const auto  tan  = evalQuadTangent(t);
                const float len  = std::sqrt(tan.x*tan.x + tan.y*tan.y) + 1e-6f;
                const float nx   = -tan.y / len;
                const float ny   =  tan.x / len;
                const float half = scl(2.5f - 2.0f * t);
                tailFill.lineTo (pt.x - nx*half, pt.y - ny*half);
            }
            tailFill.closeSubPath();

            g.setColour (bodyFill);
            g.fillPath  (tailFill);
            g.setColour (bodyStroke.withAlpha(0.85f));
            g.strokePath(tailFill, juce::PathStrokeType(strokeW));
        }
    }

    // ── Body ──────────────────────────────────────────────────────────────
    {
        const float bx = ox - scl(14);
        const float by = oy;
        const float bw = scl(28);
        const float bh = scl(22);
        g.setColour (bodyFill);
        g.fillRoundedRectangle (bx, by, bw, bh, scl(9));
        g.setColour (bodyStroke);
        g.drawRoundedRectangle (bx, by, bw, bh, scl(9), strokeW);
    }

    // ── Neck ──────────────────────────────────────────────────────────────
    // Small tapered trapezoid connecting body top to head bottom
    {
        const float neckW1 = scl(8);   // narrower at head
        const float neckW2 = scl(11);  // wider at body
        const float neckTop = oy - scl(14);
        const float neckBot = oy + scl(1);

        juce::Path neck;
        neck.startNewSubPath (ox - neckW1, neckTop);
        neck.lineTo          (ox + neckW1, neckTop);
        neck.lineTo          (ox + neckW2, neckBot);
        neck.lineTo          (ox - neckW2, neckBot);
        neck.closeSubPath();

        g.setColour (bodyFill);
        g.fillPath  (neck);
        // No stroke on neck — it reads cleaner blending into head + body
    }

    // ── Paws ──────────────────────────────────────────────────────────────
    {
        const float pawW = scl(8);
        const float pawH = scl(5.5f);
        const float pawY = oy + scl(18);

        for (float sign : { -1.0f, 1.0f })
        {
            const float pawX = ox + sign * scl(6) - pawW * 0.5f;
            g.setColour (bodyFill);
            g.fillRoundedRectangle (pawX, pawY, pawW, pawH, scl(3.5f));
            g.setColour (bodyStroke);
            g.drawRoundedRectangle (pawX, pawY, pawW, pawH, scl(3.5f), strokeW);

            // Toe divot lines
            g.setColour (bodyStroke.withAlpha(0.35f));
            for (int toe = 1; toe <= 2; ++toe)
            {
                const float tx = pawX + pawW * (toe / 3.0f);
                g.drawLine (tx, pawY + pawH * 0.35f, tx, pawY + pawH * 0.85f, 0.6f);
            }
        }
    }

    // ── Head ──────────────────────────────────────────────────────────────
    {
        const float hr = scl(13);
        const float hx = ox;
        const float hy = oy - scl(13) - hr;  // centre Y of head circle

        g.setColour (bodyFill);
        g.fillEllipse (hx - hr, hy, hr * 2, hr * 2);
        g.setColour (bodyStroke);
        g.drawEllipse (hx - hr, hy, hr * 2, hr * 2, strokeW);
    }

    // ── Ears ──────────────────────────────────────────────────────────────
    // Nervous/Clipping: ears flatten sideways (stress signal)
    const bool earsFlat = (catState == CatState::Nervous
                        || catState == CatState::Clipping);

    auto drawEar = [&] (float sign)
    {
        const float baseL = ox + sign * scl(4);
        const float baseR = ox + sign * scl(13);
        const float baseY = oy - scl(24);

        float tipX, tipY;
        if (earsFlat)
        {
            // Flattened sideways — classic cat stress/anger ear
            tipX = ox + sign * scl(17);
            tipY = oy - scl(26);
        }
        else
        {
            tipX = ox + sign * scl(14);
            tipY = oy - scl(38);
        }

        // Outer ear
        juce::Path ear;
        ear.startNewSubPath (baseL, baseY);
        ear.quadraticTo     (ox + sign * scl(7), baseY - scl(3), tipX, tipY);
        ear.lineTo          (baseR, baseY);
        ear.closeSubPath();

        g.setColour (bodyFill);
        g.fillPath  (ear);
        g.setColour (bodyStroke);
        g.strokePath(ear, juce::PathStrokeType(strokeW));

        // Inner ear tint (slightly inset)
        juce::Path inner;
        inner.startNewSubPath (baseL + sign * scl(1.5f), baseY - scl(1));
        inner.quadraticTo     (ox + sign * scl(7.5f), baseY - scl(2),
                               tipX + sign * scl(-1.5f), tipY + scl(earsFlat ? 2.0f : 5.0f));
        inner.lineTo          (baseR + sign * scl(-1.5f), baseY - scl(1));
        inner.closeSubPath();
        g.setColour (pink.withAlpha(0.30f));
        g.fillPath  (inner);
    };
    drawEar (-1.0f);
    drawEar ( 1.0f);

    // ── Eyes ──────────────────────────────────────────────────────────────
    const float eyeY  = oy - scl(17);
    const float eyeLX = ox - scl(5.5f);
    const float eyeRX = ox + scl(5.5f);

    float irisR;
    if      (catState == CatState::Clipping) irisR = scl(4.5f);
    else if (catState == CatState::Peak)     irisR = scl(4.2f);
    else if (catState == CatState::Nervous)  irisR = scl(3.8f);
    else if (catState == CatState::Active)   irisR = scl(3.5f);
    else                                     irisR = scl(3.0f);

    const float blinkSqsh = (catBlinking
        && (catState == CatState::Idle || catState == CatState::Active))
        ? 0.10f : 1.0f;

    const bool dilated = (catState == CatState::Clipping || catState == CatState::Peak);

    for (float ex : { eyeLX, eyeRX })
    {
        // Iris
        g.setColour (blue);
        g.fillEllipse (ex - irisR,          eyeY - irisR * blinkSqsh,
                       irisR * 2,           irisR * 2 * blinkSqsh);

        if (dilated)
        {
            // Wide round pupil when startled/clipping
            const float pr = irisR * 0.62f;
            g.setColour (juce::Colour(0xff160d16));
            g.fillEllipse (ex - pr, eyeY - pr * blinkSqsh, pr*2, pr*2*blinkSqsh);
        }
        else
        {
            // Slit pupil — tall narrow diamond path
            const float pw = irisR * 0.22f;
            const float ph = irisR * 0.82f * blinkSqsh;
            juce::Path slit;
            slit.startNewSubPath (ex,       eyeY - ph);
            slit.quadraticTo     (ex + pw,  eyeY,       ex,       eyeY + ph);
            slit.quadraticTo     (ex - pw,  eyeY,       ex,       eyeY - ph);
            slit.closeSubPath();
            g.setColour (juce::Colour(0xff160d16));
            g.fillPath  (slit);
        }

        // Primary catchlight
        if (blinkSqsh > 0.5f)
        {
            g.setColour (juce::Colours::white.withAlpha(0.85f));
            g.fillEllipse (ex - irisR*0.35f, eyeY - irisR*0.55f,
                           scl(1.3f), scl(1.3f));
            // Secondary ambient catchlight (opposite side, dimmer)
            g.setColour (juce::Colours::white.withAlpha(0.30f));
            g.fillEllipse (ex + irisR*0.20f, eyeY + irisR*0.15f,
                           scl(0.8f), scl(0.8f));
        }
    }

    // ── Nose ─────────────────────────────────────────────────────────────
    {
        const float ny = oy - scl(9);
        juce::Path nose;
        nose.startNewSubPath (ox,            ny - scl(2));
        nose.quadraticTo     (ox - scl(2.5f), ny,         ox - scl(2), ny + scl(0.5f));
        nose.quadraticTo     (ox,             ny + scl(1), ox + scl(2), ny + scl(0.5f));
        nose.quadraticTo     (ox + scl(2.5f), ny,         ox,           ny - scl(2));
        nose.closeSubPath();
        g.setColour (pink.withAlpha(0.90f));
        g.fillPath  (nose);
    }

    // ── Mouth — dynamic, driven by catMouthOpen ───────────────────────────
    {
        const float mouthY = oy - scl(7);
        const float openAmt = catMouthOpen;  // 0=smile, 1=wide open

        if (openAmt > 0.05f)
        {
            // Open mouth — ellipse that grows with openAmt
            const float mw = scl(4.0f + openAmt * 3.0f);
            const float mh = scl(2.5f + openAmt * 4.5f);
            g.setColour (juce::Colour(0xff160d16));
            g.fillEllipse (ox - mw*0.5f, mouthY - mh*0.5f, mw, mh);
            g.setColour (bodyStroke.withAlpha(0.7f));
            g.drawEllipse (ox - mw*0.5f, mouthY - mh*0.5f, mw, mh, 0.8f);

            // Tongue — only visible when fairly open
            if (openAmt > 0.4f)
            {
                const float tongueAlpha = (openAmt - 0.4f) / 0.6f;
                const float tw = mw * 0.65f;
                const float th = mh * 0.60f;
                juce::Path tongue;
                tongue.startNewSubPath (ox - tw*0.5f, mouthY);
                tongue.quadraticTo     (ox - tw*0.5f, mouthY + th, ox, mouthY + th * 0.85f);
                tongue.quadraticTo     (ox + tw*0.5f, mouthY + th, ox + tw*0.5f, mouthY);
                tongue.closeSubPath();
                g.setColour (pink.withAlpha(0.80f * tongueAlpha));
                g.fillPath  (tongue);
                // Tongue crease
                g.setColour (pink.darker(0.3f).withAlpha(0.50f * tongueAlpha));
                g.drawLine  (ox, mouthY + scl(0.5f), ox, mouthY + th * 0.7f, 0.7f);
            }
        }
        else
        {
            // Closed smile — amplitude responds to output peak level
            const float peakAmt = frameHistory.empty() ? 0.0f
                                 : juce::jlimit(0.0f, 1.0f, frameHistory.back().outputPeak * 1.4f);
            const float smileDepth = scl(1.5f + peakAmt * 2.5f);

            juce::Path mouth;
            mouth.startNewSubPath (ox - scl(3.5f), mouthY);
            mouth.quadraticTo     (ox,  mouthY + smileDepth, ox + scl(3.5f), mouthY);
            g.setColour (pink.withAlpha(0.55f));
            g.strokePath(mouth, juce::PathStrokeType(0.9f,
                juce::PathStrokeType::curved,
                juce::PathStrokeType::rounded));
        }
    }

    // ── Cheek blush ───────────────────────────────────────────────────────
    if (catState == CatState::Active || catState == CatState::Peak
     || catState == CatState::Nervous)
    {
        const float blushAlpha = (catState == CatState::Peak) ? 0.18f
                               : (catState == CatState::Nervous) ? 0.12f : 0.08f;
        for (float sign : { -1.0f, 1.0f })
        {
            g.setColour (pink.withAlpha(blushAlpha));
            g.fillEllipse (ox + sign * scl(8) - scl(4),
                           eyeY + scl(2),
                           scl(8), scl(4));
        }
    }

    // ── Whiskers — curved, fanned ─────────────────────────────────────────
    // Hidden when ears are flat (stressed) — real cats retract whiskers
    if (!earsFlat)
    {
        // 3 whiskers per side, fanning from top to bottom
        // Angles: slight upward, horizontal, slight downward
        struct Whisker { float angleRad; float length; float alpha; };
        const Whisker wLeft[]  = {
            { -0.12f, scl(15), 0.50f },
            {  0.00f, scl(16), 0.45f },
            {  0.14f, scl(13), 0.35f },
        };
        const Whisker wRight[] = {
            {  juce::MathConstants<float>::pi + 0.12f, scl(15), 0.50f },
            {  juce::MathConstants<float>::pi + 0.00f, scl(16), 0.45f },
            {  juce::MathConstants<float>::pi - 0.14f, scl(13), 0.35f },
        };

        const float wRootY = oy - scl(9.5f);

        auto drawWhisker = [&](float rootX, float angle, float length, float alpha)
        {
            const float ex2 = rootX + std::cos(angle) * length;
            const float ey2 = wRootY + std::sin(angle) * length;
            // Slight curve — control point offset perpendicular to whisker
            const float cpx = rootX + std::cos(angle) * length * 0.5f;
            const float cpy = wRootY + std::sin(angle) * length * 0.5f + scl(1.5f);
            juce::Path w;
            w.startNewSubPath (rootX, wRootY);
            w.quadraticTo     (cpx, cpy, ex2, ey2);
            g.setColour (blue.withAlpha(alpha));
            g.strokePath (w, juce::PathStrokeType(0.65f,
                juce::PathStrokeType::curved,
                juce::PathStrokeType::rounded));
        };

        const float wRootL = ox - scl(5);
        const float wRootR = ox + scl(5);
        for (auto& w : wLeft)  drawWhisker (wRootL, w.angleRad, w.length, w.alpha);
        for (auto& w : wRight) drawWhisker (wRootR, w.angleRad, w.length, w.alpha);
    }

    // ── Idle: sequential floating Zzz ─────────────────────────────────────
    if (catState == CatState::Idle)
    {
        g.setFont (juce::Font (juce::FontOptions()
            .withName  (juce::Font::getDefaultSansSerifFontName())
            .withHeight (scl(8.5f))));

        // Three Z's at different phases so they appear to float up one by one
        for (int i = 0; i < 3; ++i)
        {
            const float zPhase = std::fmod (catAnimPhase + i * 0.33f, 1.0f);
            const float zAlpha = zPhase < 0.7f
                               ? juce::jmin(1.0f, zPhase / 0.3f) * 0.55f
                               : (1.0f - zPhase) / 0.3f * 0.55f;
            const float zRise  = zPhase * scl(14.0f);
            const float zSize  = 0.7f + i * 0.15f;  // each Z slightly bigger

            g.setFont (juce::Font (juce::FontOptions()
                .withName  (juce::Font::getDefaultSansSerifFontName())
                .withHeight (scl(7.0f) * zSize)));
            g.setColour (blue.withAlpha(zAlpha));
            g.drawText  ("z",
                         static_cast<int>(ox + scl(13) + i * scl(2.5f)),
                         static_cast<int>(oy - scl(30) - zRise),
                         static_cast<int>(scl(10)), static_cast<int>(scl(10)),
                         juce::Justification::left, false);
        }
    }

    // ── Clipping: sweat drop + !! ─────────────────────────────────────────
    if (catState == CatState::Clipping)
    {
        // Animated sweat drop — bounces with panic phase
        const float sweatBob = std::sin(catAnimPhase * twoPi * 2.0f) * scl(1.5f);
        juce::Path sweat;
        const float sx = ox + scl(13), sy = oy - scl(28) + sweatBob;
        sweat.startNewSubPath (sx,           sy - scl(6));
        sweat.quadraticTo     (sx - scl(3.5f), sy + scl(1), sx, sy + scl(3));
        sweat.quadraticTo     (sx + scl(3.5f), sy + scl(1), sx, sy - scl(6));
        sweat.closeSubPath();
        g.setColour (blue.withAlpha(0.80f));
        g.fillPath  (sweat);
        // Highlight on sweat drop
        g.setColour (juce::Colours::white.withAlpha(0.40f));
        g.fillEllipse (sx - scl(1.0f), sy - scl(4.5f), scl(1.2f), scl(1.5f));

        g.setFont (juce::Font (juce::FontOptions()
            .withName  (juce::Font::getDefaultSansSerifFontName())
            .withHeight (scl(12.0f)).withStyle("Bold")));
        g.setColour (red.withAlpha(0.90f));
        g.drawText  ("!!",
                     static_cast<int>(ox - scl(26)),
                     static_cast<int>(oy - scl(44)),
                     20, 16, juce::Justification::left, false);
    }

    // ── Nervous: action lines (speed lines around head) ───────────────────
    if (catState == CatState::Nervous)
    {
        const float headCY = oy - scl(13) - scl(13);  // head centre Y
        g.setColour (pink.withAlpha(0.25f));
        for (int i = 0; i < 5; ++i)
        {
            const float angle = (i / 5.0f) * juce::MathConstants<float>::pi - 0.3f;
            const float innerR = scl(15);
            const float outerR = scl(19) + scl(i % 2 == 0 ? 2.0f : 0.0f);
            g.drawLine (ox + std::cos(angle) * innerR,
                        headCY + std::sin(angle) * innerR,
                        ox + std::cos(angle) * outerR,
                        headCY + std::sin(angle) * outerR,
                        0.7f);
        }
    }

    // ── Peak: rising music-note dots ──────────────────────────────────────
    if (catState == CatState::Peak)
    {
        const float dotAlpha = 0.5f + 0.4f * std::sin(catAnimPhase * twoPi * 2.0f);
        for (int i = 0; i < 3; ++i)
        {
            const float t   = static_cast<float>(i) / 2.0f;
            const float da  = dotAlpha * (1.0f - i * 0.22f);
            const float dr  = scl(2.2f - i * 0.45f);
            const float riseOffset = std::fmod(catAnimPhase + t * 0.33f, 1.0f) * scl(8.0f);
            g.setColour (pink.withAlpha(da));
            g.fillEllipse (ox - scl(22) - scl(i * 5.0f),
                           oy - scl(26) - riseOffset,
                           dr * 2, dr * 2);
        }
    }
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
