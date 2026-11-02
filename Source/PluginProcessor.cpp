// PluginProcessor.cpp
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "VisualizerComponent.h"

namespace
{
    static juce::String makePresetFileName(const juce::String& presetName)
    {
        return juce::File::createLegalFileName(presetName.trim());
    }
}

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
    cacheParameterPointers();
}

ViveAudioProcessor::~ViveAudioProcessor() {}

void ViveAudioProcessor::cacheParameterPointers()
{
    inputGainParam = apvts.getRawParameterValue(Params::IDs::INPUT_GAIN_ID);
    driveParam = apvts.getRawParameterValue(Params::IDs::DRIVE_ID);
    ceilingParam = apvts.getRawParameterValue(Params::IDs::CEILING_ID);
    mixParam = apvts.getRawParameterValue(Params::IDs::MIX_ID);
    outputGainParam = apvts.getRawParameterValue(Params::IDs::OUTPUT_GAIN_ID);

    satModeParam = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(Params::IDs::SAT_MODE_ID));
    genreParam = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(Params::IDs::GENRE_ID));
}

void ViveAudioProcessor::applyPresetValues(const Params::FactoryPreset& preset)
{
    auto setParam = [this](const char* id, float value)
    {
        if (auto* param = apvts.getParameter(id))
        {
            param->setValueNotifyingHost(param->getNormalisableRange().convertTo0to1(value));
        }
    };

    setParam(Params::IDs::INPUT_GAIN_ID, preset.inputGainDb);
    setParam(Params::IDs::DRIVE_ID, preset.drivePercent);
    setParam(Params::IDs::CEILING_ID, preset.ceilingDb);
    setParam(Params::IDs::OUTPUT_GAIN_ID, preset.outputGainDb);
    setParam(Params::IDs::MIX_ID, preset.mixPercent);

    if (auto* satParam = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(Params::IDs::SAT_MODE_ID)))
    {
        satParam->setValueNotifyingHost(
            satParam->getNormalisableRange().convertTo0to1(static_cast<float>(preset.satMode)));
    }
}

void ViveAudioProcessor::updateGenreParameter(int presetIndex)
{
    if (genreParam == nullptr)
        return;

    const int clampedIndex = juce::jlimit(0, Params::numFactoryPresets - 1, presetIndex);
    genreParam->setValueNotifyingHost(
        genreParam->getNormalisableRange().convertTo0to1(static_cast<float>(clampedIndex)));
}

bool ViveAudioProcessor::isSupportedStateXml(const juce::XmlElement& xmlState) const
{
    if (xmlState.hasTagName(apvts.state.getType()))
        return true;

    return xmlState.getChildByName(apvts.state.getType()) != nullptr;
}

bool ViveAudioProcessor::restoreStateFromXml(const juce::XmlElement& xmlState)
{
    if (! isSupportedStateXml(xmlState))
        return false;

    const int formatVersion = xmlState.getIntAttribute("formatVersion", 0);
    if (formatVersion > stateFormatVersion)
        return false;

    if (xmlState.hasTagName(apvts.state.getType()))
    {
        apvts.replaceState(juce::ValueTree::fromXml(xmlState));
        cacheParameterPointers();
        return true;
    }

    if (auto* child = xmlState.getChildByName(apvts.state.getType()))
    {
        apvts.replaceState(juce::ValueTree::fromXml(*child));
        cacheParameterPointers();
        return true;
    }

    return false;
}

//==============================================================================
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
    case static_cast<int>(Params::SaturationMode::Off):
        result.sample = sample;
        return result;

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
const juce::String ViveAudioProcessor::getName() const { return Params::Branding::productName; }

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
int    ViveAudioProcessor::getNumPrograms() { return Params::numFactoryPresets; }
int    ViveAudioProcessor::getCurrentProgram()
{
    return genreParam != nullptr ? juce::jlimit(0, Params::numFactoryPresets - 1, genreParam->getIndex())
                                 : 0;
}
void   ViveAudioProcessor::setCurrentProgram(int index)
{
    loadFactoryPreset(index);
}
const  juce::String ViveAudioProcessor::getProgramName(int index)
{
    if (! juce::isPositiveAndBelow(index, Params::numFactoryPresets))
        return {};

    return Params::factoryPresets[index].name;
}
void   ViveAudioProcessor::changeProgramName(int, const juce::String&) {}

//==============================================================================
void ViveAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    preparedBlockSize = juce::jmax(1, samplesPerBlock);

    cacheParameterPointers();

    oversampler.initProcessing(static_cast<size_t>(preparedBlockSize));
    oversampler.reset();
    const auto oversamplerLatency = oversampler.getLatencyInSamples();
    setLatencySamples(juce::roundToInt(oversamplerLatency));

    dryDelayLine.setMaximumDelayInSamples(juce::jmax(1, juce::roundToInt(oversamplerLatency) + 1));
    dryDelayLine.prepare(juce::dsp::ProcessSpec {
        sampleRate,
        static_cast<juce::uint32>(preparedBlockSize),
        static_cast<juce::uint32>(juce::jmax(1, getTotalNumOutputChannels()))
    });
    dryDelayLine.setDelay(oversamplerLatency);
    dryDelayLine.reset();

    const double rampSeconds = 0.02;

    inputGainSmoother.reset(sampleRate, rampSeconds);
    driveSmoother.reset(sampleRate, rampSeconds);
    ceilingSmoother.reset(sampleRate, rampSeconds);
    mixSmoother.reset(sampleRate, rampSeconds);
    outputGainSmoother.reset(sampleRate, rampSeconds);

    inputGainSmoother.setCurrentAndTargetValue(
        juce::Decibels::decibelsToGain(inputGainParam != nullptr ? inputGainParam->load() : Params::Ranges::INPUT_GAIN_DEFAULT));
    driveSmoother.setCurrentAndTargetValue(
        (driveParam != nullptr ? driveParam->load() : Params::Ranges::DRIVE_DEFAULT) / 100.0f);
    ceilingSmoother.setCurrentAndTargetValue(
        juce::Decibels::decibelsToGain(ceilingParam != nullptr ? ceilingParam->load() : Params::Ranges::CEILING_DEFAULT));
    mixSmoother.setCurrentAndTargetValue(
        (mixParam != nullptr ? mixParam->load() : Params::Ranges::MIX_DEFAULT) / 100.0f);
    outputGainSmoother.setCurrentAndTargetValue(
        juce::Decibels::decibelsToGain(outputGainParam != nullptr ? outputGainParam->load() : Params::Ranges::OUTPUT_GAIN_DEFAULT));

    dryBuffer.setSize(juce::jmax(1, getTotalNumOutputChannels()),
                      preparedBlockSize, false, true, true);
    dryBuffer.clear();
    ceilingStageBuffer.setSize(juce::jmax(1, getTotalNumOutputChannels()),
                               preparedBlockSize, false, true, true);
    ceilingStageBuffer.clear();

    ceilingPerSample.resize(static_cast<size_t>(preparedBlockSize), 1.0f);
    drivePerSample.resize(static_cast<size_t>(preparedBlockSize), 0.0f);
}

void ViveAudioProcessor::releaseResources()
{
    oversampler.reset();
    dryDelayLine.reset();
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
    const int numChannels = juce::jmin(totalNumInputChannels, totalNumOutputChannels);
    auto* const* bufferChannels = buffer.getArrayOfWritePointers();

    for (int i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, numSamples);

    if (static_cast<size_t>(numSamples) > drivePerSample.size())
    {
        drivePerSample.resize(static_cast<size_t>(numSamples), 0.0f);
        ceilingPerSample.resize(static_cast<size_t>(numSamples), 1.0f);
    }

    const float inputGainDb = inputGainParam != nullptr ? inputGainParam->load() : Params::Ranges::INPUT_GAIN_DEFAULT;
    const float driveValue = driveParam != nullptr ? driveParam->load() : Params::Ranges::DRIVE_DEFAULT;
    const float ceilingDb = ceilingParam != nullptr ? ceilingParam->load() : Params::Ranges::CEILING_DEFAULT;
    const float finalCeiling = juce::Decibels::decibelsToGain(ceilingDb);
    const float mixValue = mixParam != nullptr ? mixParam->load() : Params::Ranges::MIX_DEFAULT;
    const float outputGainDb = outputGainParam != nullptr ? outputGainParam->load() : Params::Ranges::OUTPUT_GAIN_DEFAULT;

    inputGainSmoother.setTargetValue(juce::Decibels::decibelsToGain(inputGainDb));
    driveSmoother.setTargetValue(driveValue / 100.0f);
    ceilingSmoother.setTargetValue(juce::Decibels::decibelsToGain(ceilingDb));
    mixSmoother.setTargetValue(mixValue / 100.0f);
    outputGainSmoother.setTargetValue(juce::Decibels::decibelsToGain(outputGainDb));

    const int satMode = satModeParam ? satModeParam->getIndex() : 0;

    // Apply input gain first, then preserve one dry copy for both the wet/dry mix and the visualizer.
    for (int s = 0; s < numSamples; ++s)
    {
        const float gain = inputGainSmoother.getNextValue();
        for (int ch = 0; ch < numChannels; ++ch)
            bufferChannels[ch][s] *= gain;
    }

    // Keep the pre-saturation signal in a buffer whose channel count and block
    // capacity were allocated in prepareToPlay(). Copying channel-by-channel
    // avoids AudioBuffer::makeCopyOf() resizing this buffer in the audio callback.
    for (int ch = 0; ch < dryBuffer.getNumChannels(); ++ch)
        dryBuffer.clear(ch, 0, numSamples);
    for (int ch = 0; ch < numChannels; ++ch)
        dryBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);
    const auto* const* dryChannels = dryBuffer.getArrayOfReadPointers();

    for (int s = 0; s < numSamples; ++s)
    {
        drivePerSample[static_cast<size_t>(s)] = driveSmoother.getNextValue();
        ceilingPerSample[static_cast<size_t>(s)] = ceilingSmoother.getNextValue();
    }

    {
        juce::dsp::AudioBlock<float> block(buffer);
        auto oversampledBlock = oversampler.processSamplesUp(block);
        const int osNumSamples = static_cast<int>(oversampledBlock.getNumSamples());

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* osSamples = oversampledBlock.getChannelPointer(ch);

            for (int s = 0; s < osNumSamples; ++s)
            {
                const int baseIdx = juce::jmin(s >> 2, numSamples - 1);
                const float driveAmount = drivePerSample[static_cast<size_t>(baseIdx)];
                const float c = ceilingPerSample[static_cast<size_t>(baseIdx)];
                if (satMode == static_cast<int>(Params::SaturationMode::Off))
                    continue;
                const auto shaped = shapeSaturation(osSamples[s], driveAmount, satMode, c);
                osSamples[s] = clampToCeiling(softClipFSC(shaped.sample, c), c);
            }
        }

        oversampler.processSamplesDown(block);

        for (int s = 0; s < numSamples; ++s)
        {
            const float c = ceilingPerSample[static_cast<size_t>(s)];
            const float mix = mixSmoother.getNextValue();
            const float outGain = outputGainSmoother.getNextValue();
            const float dryMix = 1.0f - mix;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                float& wet = bufferChannels[ch][s];
                dryDelayLine.pushSample(ch, dryChannels[ch][s]);
                const float alignedDry = dryDelayLine.popSample(ch);
                if (satMode != static_cast<int>(Params::SaturationMode::Off))
                    wet = clampToCeiling(wet, c);

                if (mix < 0.999f)
                    wet = alignedDry * dryMix + wet * mix;

                // Preserve the signal at the ceiling stage for clip metering;
                // Output Gain is applied after this stage.
                ceilingStageBuffer.setSample(ch, s, wet);
                wet = clampToCeiling(wet, finalCeiling);
                wet *= outGain;
            }
        }
    }

    if (visualizer != nullptr)
    {
        visualizer->setClippingThreshold(finalCeiling);
        visualizer->pushSamples(dryBuffer, buffer, ceilingStageBuffer);
    }
}

//==============================================================================
bool ViveAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* ViveAudioProcessor::createEditor()
{
    return new ViveAudioProcessorEditor(*this);
}

//==============================================================================
juce::StringArray ViveAudioProcessor::getFactoryPresetNames() const
{
    juce::StringArray names;
    for (const auto& preset : Params::factoryPresets)
        names.add(preset.name);
    return names;
}

juce::StringArray ViveAudioProcessor::getUserPresetNames() const
{
    juce::StringArray names;
    const auto directory = getUserPresetDirectory();

    if (! directory.exists())
        return names;

    juce::Array<juce::File> files;
    directory.findChildFiles(files, juce::File::findFiles, false, "*." + Params::Branding::stateFileExtension);
    files.sort();

    for (const auto& file : files)
        names.add(file.getFileNameWithoutExtension());

    return names;
}

juce::File ViveAudioProcessor::getUserPresetDirectory() const
{
    auto directory = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile(Params::Branding::shortName)
        .getChildFile("Presets");

    directory.createDirectory();
    return directory;
}

bool ViveAudioProcessor::loadFactoryPreset(int presetIndex)
{
    if (presetIndex < 0 || presetIndex >= Params::numFactoryPresets)
        return false;

    const auto& preset = Params::factoryPresets[presetIndex];
    applyPresetValues(preset);
    updateGenreParameter(presetIndex);
    return true;
}

bool ViveAudioProcessor::saveUserPreset(const juce::String& presetName)
{
    const auto trimmedName = presetName.trim();
    if (trimmedName.isEmpty())
        return false;

    auto xmlState = std::unique_ptr<juce::XmlElement>(apvts.copyState().createXml());
    if (xmlState == nullptr)
        return false;

    xmlState->setAttribute("presetName", trimmedName);
    xmlState->setAttribute("presetType", "user");
    xmlState->setAttribute("pluginName", Params::Branding::productName);
    xmlState->setAttribute("formatVersion", stateFormatVersion);

    const auto file = getUserPresetDirectory().getChildFile(makePresetFileName(trimmedName))
        .withFileExtension(Params::Branding::stateFileExtension);

    juce::TemporaryFile tempFile(file);
    if (auto out = std::unique_ptr<juce::FileOutputStream>(tempFile.getFile().createOutputStream()))
    {
        out->writeText(xmlState->toString(), false, false, "\n");
        out->flush();
        out.reset();

        return tempFile.overwriteTargetFileWithTemporary();
    }

    return false;
}

bool ViveAudioProcessor::loadUserPreset(const juce::String& presetName)
{
    const auto file = getUserPresetDirectory().getChildFile(makePresetFileName(presetName))
        .withFileExtension(Params::Branding::stateFileExtension);

    if (! file.existsAsFile())
        return false;

    auto xmlState = std::unique_ptr<juce::XmlElement>(juce::XmlDocument::parse(file));
    if (xmlState == nullptr)
        return false;

    if (xmlState->getStringAttribute("presetType") == "user"
        && xmlState->getStringAttribute("presetName").isNotEmpty())
    {
        const auto expectedName = makePresetFileName(presetName);
        if (file.getFileNameWithoutExtension() != expectedName)
            return false;
    }

    return restoreStateFromXml(*xmlState);
}

bool ViveAudioProcessor::deleteUserPreset(const juce::String& presetName)
{
    const auto file = getUserPresetDirectory().getChildFile(makePresetFileName(presetName))
        .withFileExtension(Params::Branding::stateFileExtension);

    return file.existsAsFile() ? file.deleteFile() : false;
}

//==============================================================================
void ViveAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    if (xml != nullptr)
    {
        xml->setAttribute("pluginName", Params::Branding::productName);
        xml->setAttribute("formatVersion", stateFormatVersion);
        copyXmlToBinary(*xml, destData);
    }
}

void ViveAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr)
        restoreStateFromXml(*xmlState);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ViveAudioProcessor();
}
