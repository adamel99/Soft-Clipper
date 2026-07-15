// PluginProcessor.cpp
#include "PluginProcessor.h"
#include "PluginEditor.h"

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
void ViveAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    oversampler.initProcessing(static_cast<size_t>(samplesPerBlock));
    oversampler.reset();

    setLatencySamples(static_cast<int>(oversampler.getLatencyInSamples()));

    satModeParam = dynamic_cast<juce::AudioParameterChoice*>(
        apvts.getParameter(Params::IDs::SAT_MODE_ID));
    jassert(satModeParam != nullptr);

    const double rampSeconds = 0.02;

    inputGainSmoother.reset(sampleRate, rampSeconds);
    driveSmoother.reset(sampleRate, rampSeconds);
    ceilingSmoother.reset(sampleRate, rampSeconds);
    mixSmoother.reset(sampleRate, rampSeconds);
    outputGainSmoother.reset(sampleRate, rampSeconds);

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

    dryBuffer.setSize(2, samplesPerBlock);
    dryBuffer.clear();
    inputMeterBuffer.setSize(2, samplesPerBlock);
    inputMeterBuffer.clear();

    ceilingPerSample.resize(static_cast<size_t>(samplesPerBlock), 1.0f);
    drivePerSample.resize(static_cast<size_t>(samplesPerBlock), 0.0f);
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
void ViveAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    const int totalNumInputChannels = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin(totalNumInputChannels,
        totalNumOutputChannels);

    for (int i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, numSamples);

    // Guard against hosts that send more samples than samplesPerBlock
    if (static_cast<size_t>(numSamples) > drivePerSample.size())
    {
        drivePerSample.resize(static_cast<size_t>(numSamples), 0.0f);
        ceilingPerSample.resize(static_cast<size_t>(numSamples), 1.0f);
    }

    // ── Update smoother targets ───────────────────────────────────────────
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
    for (int s = 0; s < numSamples; ++s)
    {
        const float gain = inputGainSmoother.getNextValue();
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.getWritePointer(ch)[s] *= gain;
    }

    // ── Capture post-input-gain signal for visualizer + dry mix ──────────
    dryBuffer.makeCopyOf(buffer);
    inputMeterBuffer.makeCopyOf(buffer);

    // ── Snapshot per-base-sample drive and ceiling values ─────────────────
    for (int s = 0; s < numSamples; ++s)
    {
        drivePerSample[static_cast<size_t>(s)] = driveSmoother.getNextValue();
        ceilingPerSample[static_cast<size_t>(s)] = ceilingSmoother.getNextValue();
    }

    // ── Steps 2 + 3: Drive (alias-free) + Ceiling clip — inside oversampler ──
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

                // ── Drive / saturation ────────────────────────────────────
                if (driveAmount > 0.01f)
                {
                    const float normFactor = (c > 1e-6f) ? (1.0f / c) : 1.0f;
                    sample *= normFactor;

                    const float driven = sample * (1.0f + driveAmount * 2.0f);
                    float soft = sample;
                    float makeupGain = 1.0f;

                    switch (satMode)
                    {
                    case static_cast<int>(Params::SaturationMode::Clean):
                    {
                        const float k = 1.0f + driveAmount * 2.0f;
                        const float t = std::tanh(driven / k);
                        soft = k * (t - (t * t * t) / 6.0f);
                        makeupGain = 1.0f / (1.0f - driveAmount * 0.18f + 1e-9f);
                        break;
                    }
                    case static_cast<int>(Params::SaturationMode::Tube):
                    {
                        soft = std::tanh(driven);
                        makeupGain = 1.0f / (std::tanh(1.0f + driveAmount * 2.0f) + 1e-9f);
                        break;
                    }
                    case static_cast<int>(Params::SaturationMode::Tape):
                    {
                        soft = driven / (1.0f + std::abs(driven));
                        makeupGain = 1.0f + driveAmount * 0.6f;
                        break;
                    }
                    case static_cast<int>(Params::SaturationMode::SolidState):
                    {
                        soft = driven / std::sqrt(1.0f + driven * driven);
                        makeupGain = std::sqrt(1.0f + driveAmount * 2.0f);
                        break;
                    }
                    case static_cast<int>(Params::SaturationMode::Asymmetric):
                    {
                        if (driven >= 0.0f)
                            soft = std::tanh(driven * 0.8f) * 1.1f;
                        else
                            soft = driven / (1.0f + std::abs(driven) * 1.4f);

                        const float refOutput = std::tanh((1.0f + driveAmount * 2.0f) * 0.8f) * 1.1f;
                        makeupGain = 1.0f / (refOutput + 1e-9f);
                        break;
                    }
                    default:
                    {
                        soft = std::tanh(driven);
                        makeupGain = 1.0f / (std::tanh(1.0f + driveAmount * 2.0f) + 1e-9f);
                        break;
                    }
                    }

                    sample = soft * makeupGain;
                    sample *= c;
                }

                // ── Ceiling clip ──────────────────────────────────────────
                sample = softClipFSC(sample, c);
            }
        }

        oversampler.processSamplesDown(block);


        for (int s = 0; s < numSamples; ++s)
        {
            const float c = ceilingPerSample[static_cast<size_t>(s)];
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float& sample = buffer.getWritePointer(ch)[s];
                sample = juce::jlimit(-c, c, sample);  // hard clip only
            }
        }
    }

    // ── Step 4: Dry/wet mix ───────────────────────────────────────────────
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
    visualizer.setClippingThreshold(ceilingPerSample[static_cast<size_t>(numSamples - 1)]);
    visualizer.pushSamples(inputMeterBuffer, buffer);
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
