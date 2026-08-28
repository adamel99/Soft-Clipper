// PluginProcessor.cpp
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "VisualizerComponent.h"

//==============================================================================
ViveAudioProcessor::ViveAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
#if ! JucePlugin_IsMidiEffect
#if ! JucePlugin_IsSynth
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
#endif
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
    ),
#endif
    apvts(*this, nullptr, "Parameters", Params::createParameterLayout())
{
}

ViveAudioProcessor::~ViveAudioProcessor() {}

ViveAudioProcessor::SaturationResult ViveAudioProcessor::shapeSaturation(float sample,
                                                                         float driveAmount,
                                                                         int satMode,
                                                                         float ceiling) noexcept
{
    SaturationResult result;

    if (driveAmount <= 0.01f)
    {
        result.sample = sample;
        return result;
    }

    const float safeCeiling = juce::jmax(ceiling, 1.0e-6f);
    const float normalised = sample / safeCeiling;
    const float driven = normalised * (1.0f + driveAmount * 2.0f);
    const float driveCurve = 1.0f + driveAmount * 2.0f;

    float shaped = normalised;
    float makeupGain = 1.0f;

    switch (satMode)
    {
    case static_cast<int>(Params::SaturationMode::Clean):
    {
        const float t = std::tanh(driven / driveCurve);
        shaped = driveCurve * (t - (t * t * t) / 6.0f);
        makeupGain = 1.0f / (1.0f - driveAmount * 0.18f + 1.0e-9f);
        break;
    }
    case static_cast<int>(Params::SaturationMode::Tube):
        shaped = std::tanh(driven);
        makeupGain = 1.0f / (std::tanh(driveCurve) + 1.0e-9f);
        break;
    case static_cast<int>(Params::SaturationMode::Tape):
        shaped = driven / (1.0f + std::abs(driven));
        makeupGain = 1.0f + driveAmount * 0.55f;
        break;
    case static_cast<int>(Params::SaturationMode::SolidState):
        shaped = driven / std::sqrt(1.0f + driven * driven);
        makeupGain = std::sqrt(1.0f + driveAmount * 1.8f);
        break;
    case static_cast<int>(Params::SaturationMode::Asymmetric):
        if (driven >= 0.0f)
            shaped = std::tanh(driven * 0.8f) * 1.1f;
        else
            shaped = driven / (1.0f + std::abs(driven) * 1.4f);

        makeupGain = 1.0f / (std::tanh(driveCurve * 0.8f) * 1.1f + 1.0e-9f);
        break;
    default:
        shaped = std::tanh(driven);
        makeupGain = 1.0f / (std::tanh(driveCurve) + 1.0e-9f);
        break;
    }

    result.sample = shaped * makeupGain * safeCeiling;
    result.makeupGain = makeupGain;
    return result;
}

float ViveAudioProcessor::clampToCeiling(float sample, float ceiling) noexcept
{
    return juce::jlimit(-ceiling, ceiling, sample);
}

//==============================================================================
const juce::String ViveAudioProcessor::getName() const { return JucePlugin_Name; }

bool ViveAudioProcessor::acceptsMidi() const
{
#if JucePlugin_WantsMidiInput
    return true;
#else
    return false;
#endif
}

bool ViveAudioProcessor::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
    return true;
#else
    return false;
#endif
}

bool ViveAudioProcessor::isMidiEffect() const
{
#if JucePlugin_IsMidiEffect
    return true;
#else
    return false;
#endif
}

double ViveAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int    ViveAudioProcessor::getNumPrograms() { return 1; }
int    ViveAudioProcessor::getCurrentProgram() { return 0; }
void   ViveAudioProcessor::setCurrentProgram(int) {}
const  juce::String ViveAudioProcessor::getProgramName(int) { return {}; }
void   ViveAudioProcessor::changeProgramName(int, const juce::String&) {}

//==============================================================================
// INTERVIEW NOTE: prepareToPlay is the JUCE lifecycle hook called once before
// playback starts (and again if sampleRate/blockSize change). Anything that
// depends on sampleRate or needs pre-allocated memory belongs here, NOT in
// processBlock — processBlock runs on the audio thread and must never allocate
// (new/delete, resize, etc. can block on the heap allocator or trigger a
// page fault, both of which can cause an audio dropout/glitch).
void ViveAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    // JUCE's Oversampling class needs to know the max block size up front so it
    // can pre-allocate its internal upsampled buffers (size = samplesPerBlock * factor).
    preparedBlockSize = juce::jmax(1, samplesPerBlock);

    oversampler.initProcessing(static_cast<size_t>(preparedBlockSize));
    oversampler.reset();

    // The half-band FIR oversampling filters are linear-phase, which means they
    // introduce a fixed group delay. Reporting it via setLatencySamples lets the
    // host compensate (PDC — plugin delay compensation) so this track stays in
    // sample-accurate sync with the rest of the session.
    setLatencySamples(static_cast<int>(oversampler.getLatencyInSamples()));

    satModeParam = dynamic_cast<juce::AudioParameterChoice*>(
        apvts.getParameter(Params::IDs::SAT_MODE_ID));
    jassert(satModeParam != nullptr);

    // 20ms smoothing ramp time. INTERVIEW NOTE: SmoothedValue exists to solve
    // "zipper noise" — if a parameter (gain, mix, etc.) jumps instantly between
    // audio blocks, you get an audible click/stairstep because the waveform's
    // amplitude discontinuously steps rather than sliding. Ramping the value
    // sample-by-sample over ~20ms is fast enough to feel responsive to automation/
    // knob turns, but slow enough that the ear doesn't perceive a click.
    const double rampSeconds = 0.02;

    inputGainSmoother.reset(sampleRate, rampSeconds);
    driveSmoother.reset(sampleRate, rampSeconds);
    ceilingSmoother.reset(sampleRate, rampSeconds);
    mixSmoother.reset(sampleRate, rampSeconds);
    outputGainSmoother.reset(sampleRate, rampSeconds);

    // setCurrentAndTargetValue snaps the smoother immediately to the current
    // parameter value (no ramp) — this avoids a spurious ramp-in from 0 the
    // very first time audio is processed after load/reset.
    inputGainSmoother.setCurrentAndTargetValue(
        juce::Decibels::decibelsToGain(
            apvts.getRawParameterValue(Params::IDs::INPUT_GAIN_ID)->load()));
    driveSmoother.setCurrentAndTargetValue(
        apvts.getRawParameterValue(Params::IDs::DRIVE_ID)->load() / 100.0f);
    ceilingSmoother.setCurrentAndTargetValue(
        juce::Decibels::decibelsToGain(
            apvts.getRawParameterValue(Params::IDs::CEILING_ID)->load()));
    mixSmoother.setCurrentAndTargetValue(
        apvts.getRawParameterValue(Params::IDs::MIX_ID)->load() / 100.0f);
    outputGainSmoother.setCurrentAndTargetValue(
        juce::Decibels::decibelsToGain(
            apvts.getRawParameterValue(Params::IDs::OUTPUT_GAIN_ID)->load()));

    // Pre-allocate all working buffers to the max block size up front (RT-safety —
    // see the "never allocate on the audio thread" note above).
    dryBuffer.setSize(2, preparedBlockSize);
    dryBuffer.clear();
    inputMeterBuffer.setSize(2, preparedBlockSize);
    inputMeterBuffer.clear();

    ceilingPerSample.resize(static_cast<size_t>(preparedBlockSize), 1.0f);
    drivePerSample.resize(static_cast<size_t>(preparedBlockSize), 0.0f);
}

void ViveAudioProcessor::releaseResources()
{
    oversampler.reset();
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool ViveAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
    juce::ignoreUnused(layouts);
    return true;
#else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

#if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
#endif

    return true;
#endif
}
#endif

//==============================================================================
// INTERVIEW NOTE: signal flow through this function, top to bottom, is the
// classic saturation-plugin pipeline:
//   input gain -> (dry copy taken here) -> nonlinear drive+clip (oversampled)
//   -> dry/wet mix -> output gain -> metering
// Being able to draw this signal chain from memory is a very common ask.
void ViveAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused(midiMessages);

    const int totalNumSamples = buffer.getNumSamples();
    const int chunkSize = juce::jmax(1, preparedBlockSize);

    for (int startSample = 0; startSample < totalNumSamples; startSample += chunkSize)
    {
        const int numSamples = juce::jmin(chunkSize, totalNumSamples - startSample);
        juce::AudioBuffer<float> chunkView(buffer.getArrayOfWritePointers(),
                                          getTotalNumOutputChannels(),
                                          startSample,
                                          numSamples);
        processBlockChunk(chunkView);
    }
}

void ViveAudioProcessor::processBlockChunk(juce::AudioBuffer<float>& buffer)
{
    juce::ScopedNoDenormals noDenormals;
    const int totalNumInputChannels = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin(totalNumInputChannels,
        totalNumOutputChannels);

    // If output has more channels than input (e.g. mono-in/stereo-out), the
    // extra output channels still contain garbage/leftover buffer data from a
    // previous block unless explicitly cleared — this is boilerplate required
    // by nearly every JUCE plugin template.
    for (int i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, numSamples);

    // Guard against hosts that send more samples than samplesPerBlock
    if (static_cast<size_t>(numSamples) > drivePerSample.size())
    {
        drivePerSample.resize(static_cast<size_t>(numSamples), 0.0f);
        ceilingPerSample.resize(static_cast<size_t>(numSamples), 1.0f);
    }

    // ── Update smoother targets ───────────────────────────────────────────
    // NOTE: this reads the atomic parameter values once per block (not once per
    // sample) — that's fine because the smoother itself interpolates sample-by-
    // sample toward this target via getNextValue() below. This is the standard
    // JUCE control-rate-vs-audio-rate split: parameters are polled at block rate,
    // but the resulting gain trajectory is still audio-rate smooth.
    inputGainSmoother.setTargetValue(
        juce::Decibels::decibelsToGain(
            apvts.getRawParameterValue(Params::IDs::INPUT_GAIN_ID)->load()));
    driveSmoother.setTargetValue(
        apvts.getRawParameterValue(Params::IDs::DRIVE_ID)->load() / 100.0f);
    ceilingSmoother.setTargetValue(
        juce::Decibels::decibelsToGain(
            apvts.getRawParameterValue(Params::IDs::CEILING_ID)->load()));
    mixSmoother.setTargetValue(
        apvts.getRawParameterValue(Params::IDs::MIX_ID)->load() / 100.0f);
    outputGainSmoother.setTargetValue(
        juce::Decibels::decibelsToGain(
            apvts.getRawParameterValue(Params::IDs::OUTPUT_GAIN_ID)->load()));

    const int satMode = satModeParam ? satModeParam->getIndex() : 0;

    // ── Step 1: Input gain ────────────────────────────────────────────────
    // Sample-accurate gain ramp: getNextValue() advances the smoother's internal
    // state by exactly one sample each call, so the gain applied at sample N is
    // slightly different from sample N+1 during a ramp — this is what avoids
    // zipper noise (see prepareToPlay note above) at the per-sample level.
    for (int s = 0; s < numSamples; ++s)
    {
        const float gain = inputGainSmoother.getNextValue();
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.getWritePointer(ch)[s] *= gain;
    }

    // ── Capture post-input-gain signal for visualizer + dry mix ──────────
    // dryBuffer: preserved so Step 4 can blend it back in for the dry/wet mix
    //            without needing to "undo" the saturation stage.
    // inputMeterBuffer: a snapshot purely for UI metering (see visualizer.pushSamples
    //            at the bottom) — kept separate from `buffer` because `buffer` gets
    //            mutated destructively by the saturation stage below.
    dryBuffer.makeCopyOf(buffer);
    inputMeterBuffer.makeCopyOf(buffer);

    // ── Snapshot per-base-sample drive and ceiling values ─────────────────
    // These parameters need a value at oversampled-rate resolution below, but the
    // smoothers only run at the original sample rate. So here we advance them once
    // per *original* sample and cache the sequence; the oversampled loop below maps
    // each upsampled index back down to the nearest original-rate index (baseIdx)
    // to look up the correct drive/ceiling for that instant. This avoids having to
    // run separate smoothers at 4x the sample rate.
    for (int s = 0; s < numSamples; ++s)
    {
        drivePerSample[static_cast<size_t>(s)] = driveSmoother.getNextValue();
        ceilingPerSample[static_cast<size_t>(s)] = ceilingSmoother.getNextValue();
    }

    // ── Steps 2 + 3: Drive (alias-free) + Ceiling clip — inside oversampler ──
    // Nonlinear shaping happens at 4x rate so the generated harmonics are less
    // likely to fold back into the audible band before the downsampler filters
    // them out.
    {
        juce::dsp::AudioBlock<float> block(buffer);
        auto oversampledBlock = oversampler.processSamplesUp(block);
        const int osNumSamples = static_cast<int>(oversampledBlock.getNumSamples());

        for (int s = 0; s < osNumSamples; ++s)
        {
            const int baseIdx = juce::jmin(s / oversampleFactor, numSamples - 1);
            const float driveAmount = drivePerSample[static_cast<size_t>(baseIdx)];
            const float c = ceilingPerSample[static_cast<size_t>(baseIdx)];

            for (int ch = 0; ch < numChannels; ++ch)
            {
                float& sample = oversampledBlock.getChannelPointer(ch)[s];
                const auto shaped = shapeSaturation(sample, driveAmount, satMode, c);
                sample = clampToCeiling(softClipFSC(shaped.sample, c), c);
            }
        }

        oversampler.processSamplesDown(block);

        for (int s = 0; s < numSamples; ++s)
        {
            const float c = ceilingPerSample[static_cast<size_t>(s)];
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float& sample = buffer.getWritePointer(ch)[s];
                sample = clampToCeiling(sample, c);
            }
        }
    }

    // ── Step 4: Dry/wet mix ───────────────────────────────────────────────
    // Sample-accurate equal-mix crossfade (linear, not equal-power) between the
    // pre-saturation dry signal and the fully processed wet signal.
    for (int s = 0; s < numSamples; ++s)
    {
        const float mix = mixSmoother.getNextValue();
        if (mix < 0.999f)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float dry = dryBuffer.getReadPointer(ch)[s];
                float& wet = buffer.getWritePointer(ch)[s];
                wet = dry * (1.0f - mix) + wet * mix;
            }
        }
    }

    // ── Step 5: Output gain ───────────────────────────────────────────────
    for (int s = 0; s < numSamples; ++s)
    {
        const float outGain = outputGainSmoother.getNextValue();
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.getWritePointer(ch)[s] *= outGain;
    }

    // ── Feed visualizer ───────────────────────────────────────────────────
    // INTERVIEW NOTE: this is a cross-thread handoff — processBlock runs on the
    // realtime audio thread, but the visualizer's paint() runs on the message/UI
    // thread. VisualizerComponent.pushSamples() writes into a lock-free
    // juce::AbstractFifo (single-producer/single-consumer) rather than directly
    // mutating UI-owned state, which is the standard safe pattern: the audio
    // thread must never block on a mutex/lock that the UI thread might be
    // holding, or you risk priority inversion and audible dropouts.
    float visualThreshold = ceilingPerSample.empty() ? 1.0f : ceilingPerSample.front();
    for (int s = 1; s < numSamples; ++s)
        visualThreshold = juce::jmin(visualThreshold, ceilingPerSample[static_cast<size_t>(s)]);

    if (visualizer != nullptr)
    {
        visualizer->setClippingThreshold(visualThreshold);
        visualizer->pushSamples(inputMeterBuffer, buffer);
    }
}

//==============================================================================
bool ViveAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* ViveAudioProcessor::createEditor()
{
    return new ViveAudioProcessorEditor(*this);
}

//==============================================================================
void ViveAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void ViveAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr)
        if (xmlState->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ViveAudioProcessor();
}
