/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
HsVSTAudioProcessor::HsVSTAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
#if ! JucePlugin_IsMidiEffect
#if ! JucePlugin_IsSynth
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
#endif
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
    )
#endif
{
    // Imposta valori iniziali sicuri per le atomiche
    atomicLpfCutoff.store(800.0f);  // Un valore di cutoff iniziale che fa passare il suono (es. 800 Hz)
    atomicFlangerDepth.store(0.0f);
    atomicChorusDepth.store(0.0f);
    atomicDrive.store(0.0f);
    atomicChorusRate.store(0.5f);
}

HsVSTAudioProcessor::~HsVSTAudioProcessor()
{
}

//==============================================================================
const juce::String HsVSTAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool HsVSTAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool HsVSTAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool HsVSTAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double HsVSTAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int HsVSTAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int HsVSTAudioProcessor::getCurrentProgram()
{
    return 0;
}

void HsVSTAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String HsVSTAudioProcessor::getProgramName (int index)
{
    return {};
}

void HsVSTAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void HsVSTAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    // Inizializza e sincronizza le smoothed values con le atomiche attuali
    smoothedChorusDepth.reset(sampleRate, 0.05);
    smoothedChorusDepth.setCurrentAndTargetValue(atomicChorusDepth.load());

    smoothedFlangerDepth.reset(sampleRate, 0.05);
    smoothedFlangerDepth.setCurrentAndTargetValue(atomicFlangerDepth.load());

    smoothedLpfCutoff.reset(sampleRate, 0.05);
    smoothedLpfCutoff.setCurrentAndTargetValue(atomicLpfCutoff.load());

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<uint32_t>(samplesPerBlock);
    spec.numChannels = 1;

    distortionModuleL.prepare(spec);
    distortionModuleR.prepare(spec);

    chorusModuleL.prepare(static_cast<float>(sampleRate));
    chorusModuleR.prepare(static_cast<float>(sampleRate));

    flangerModuleL.prepare(sampleRate);
    flangerModuleR.prepare(sampleRate);

    masterLpfL.prepare(spec);
    masterLpfL.setType(juce::dsp::StateVariableTPTFilterType::lowpass);

    masterLpfR.prepare(spec);
    masterLpfR.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
}


void HsVSTAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool HsVSTAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void HsVSTAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Pulizia dei canali di output in eccesso
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // 1. Gestione dei messaggi MIDI in arrivo dalla DAW
    for (const auto metadata : midiMessages)
    {
        auto message = metadata.getMessage();
        if (message.isNoteOn() && message.getVelocity() > 0)
        {
            currentFrequency = static_cast<float>(juce::MidiMessage::getMidiNoteInHertz(message.getNoteNumber()));
            activeNotes++;
            isPlaying = true;
        }
        else if (message.isNoteOff() || (message.isNoteOn() && message.getVelocity() == 0))
        {
            activeNotes--;
            if (activeNotes <= 0)
            {
                activeNotes = 0;
                isPlaying = false;
            }
        }
    }

    // 2. Estrazione dei puntatori audio dal buffer della DAW
    auto* leftChannel = buffer.getWritePointer(0);
    auto* rightChannel = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : leftChannel;

    // 3. Sintesi ed elaborazione DSP (Thread-safe con le atomiche)
    float driveVal = atomicDrive.load();
    float chorusRateVal = atomicChorusRate.load();

    smoothedChorusDepth.setTargetValue(atomicChorusDepth.load());
    smoothedFlangerDepth.setTargetValue(atomicFlangerDepth.load());
    smoothedLpfCutoff.setTargetValue(atomicLpfCutoff.load());

    float distMixVal = 0.5f;
    float chorusMixVal = 0.5f;
    double sampleRate = currentSampleRate;
    static float envelope = 0.0f;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        float sample = 0.0f;
        float targetEnv = isPlaying ? 0.3f : 0.0f;

        envelope += (targetEnv - envelope) * 0.001f;

        if (envelope > 0.0001f)
        {
            float squareVal = (std::sin(phase) >= 0.0f) ? 1.0f : -1.0f;
            sample = squareVal * envelope * 0.5f;

            phase += 2.0 * juce::MathConstants<double>::pi * currentFrequency / sampleRate;
            if (phase >= 2.0 * juce::MathConstants<double>::pi)
                phase -= 2.0 * juce::MathConstants<double>::pi;
        }
        else
        {
            phase = 0.0;
        }

        // Distorsione
        float distL = distortionModuleL.processSample(sample, driveVal, distMixVal);
        float distR = distortionModuleR.processSample(sample, driveVal, distMixVal);

        float finalL = distL;
        float finalR = distR;

        // Chorus
        float chorusDepthVal = smoothedChorusDepth.getNextValue();
        if (chorusDepthVal > 0.001f)
        {
            chorusModuleL.advancePhase(chorusRateVal, static_cast<float>(currentSampleRate));
            chorusModuleR.advancePhase(chorusRateVal, static_cast<float>(currentSampleRate));

            finalL = chorusModuleL.processSampleLeft(distL, chorusDepthVal, chorusRateVal, chorusMixVal, static_cast<float>(currentSampleRate));
            finalR = chorusModuleR.processSampleRight(distR, chorusDepthVal, chorusRateVal, chorusMixVal, static_cast<float>(currentSampleRate));
        }

        // Flanger
        float flangerDepthVal = smoothedFlangerDepth.getNextValue();
        finalL = flangerModuleL.processSample(finalL, flangerDepthVal);
        finalR = flangerModuleR.processSample(finalR, flangerDepthVal);

        // Filtro LPF Master
        float currentLpfCutoff = smoothedLpfCutoff.getNextValue();
        masterLpfL.setCutoffFrequency(currentLpfCutoff);
        masterLpfR.setCutoffFrequency(currentLpfCutoff);

        // Filtro LPF Master
        float filteredL = masterLpfL.processSample(0, finalL);
        float filteredR = masterLpfR.processSample(0, finalR);

        // Uscita audio sinistra
        leftChannel[i] = std::clamp(filteredL, -0.95f, 0.95f);

        // Uscita audio destra (solo se stereo)
        if (buffer.getNumChannels() > 1)
        {
            rightChannel[i] = std::clamp(filteredR, -0.95f, 0.95f);
        }
    }
}




//==============================================================================
bool HsVSTAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* HsVSTAudioProcessor::createEditor()
{
    return new HsVSTAudioProcessorEditor (*this);
}

//==============================================================================
void HsVSTAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
}

void HsVSTAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HsVSTAudioProcessor();
}
