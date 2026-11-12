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
    atomicChorusRate.store(0.8f);
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
    envelope = 0.0f;

    // Inizializza le 8 voci del sintetizzatore
    for (auto& voice : voices)
    {
        voice = SynthVoice{};
    }


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

    chorusModule.prepare(static_cast<float>(sampleRate));

    flangerModuleL.prepare(sampleRate);
    flangerModuleR.prepare(sampleRate);

    masterLpfL.prepare(spec);
    masterLpfL.setType(juce::dsp::StateVariableTPTFilterType::lowpass);

    masterLpfR.prepare(spec);
    masterLpfR.setType(juce::dsp::StateVariableTPTFilterType::lowpass);

    // Sincronizza il cutoff iniziale dei due filtri
    float initialCutoff = atomicLpfCutoff.load();

    masterLpfL.setCutoffFrequency(initialCutoff);
    masterLpfR.setCutoffFrequency(initialCutoff);

    lastAppliedLpfCutoff = initialCutoff;
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

            // Cerca una voce libera per la nuova nota MIDI
            for (auto& voice : voices)
            {
                if (voice.noteNumber == -1 || !voice.isPlaying)
                {
                    voice.noteNumber = message.getNoteNumber();
                    voice.frequency = static_cast<float>(
                        juce::MidiMessage::getMidiNoteInHertz(message.getNoteNumber())
                        );
                    voice.phase = 0.0;
                    voice.envelope = 0.0f;
                    voice.isPlaying = true;
                    break;
                }
            }
        }
        else if (message.isNoteOff() || (message.isNoteOn() && message.getVelocity() == 0))
        {
            activeNotes--;

            if (activeNotes <= 0)
            {
                activeNotes = 0;
                isPlaying = false;
            }

            // Rilascia la voce corrispondente alla nota MIDI
            for (auto& voice : voices)
            {
                if (voice.noteNumber == message.getNoteNumber() && voice.isPlaying)
                {
                    voice.isPlaying = false;
                    break;
                }
            }
        }
    }

    // 2. Lettura della sorgente audio
    const auto mode = inputMode.load();

    const float* inputLeft = totalNumInputChannels > 0
        ? buffer.getReadPointer(0)
        : nullptr;

    const float* inputRight = totalNumInputChannels > 1
        ? buffer.getReadPointer(1)
        : inputLeft;

    // Puntatori per l'uscita
    auto* leftChannel = buffer.getWritePointer(0);

    auto* rightChannel = buffer.getNumChannels() > 1
        ? buffer.getWritePointer(1)
        : leftChannel;

    // 3. Sintesi ed elaborazione DSP (Thread-safe con le atomiche)
    float driveVal = atomicDrive.load();
    float chorusRateVal = atomicChorusRate.load();

    smoothedChorusDepth.setTargetValue(atomicChorusDepth.load());
    smoothedFlangerDepth.setTargetValue(atomicFlangerDepth.load());
    smoothedLpfCutoff.setTargetValue(atomicLpfCutoff.load());

    float distMixVal = 0.5f;
    float chorusMixVal = 0.5f;
    double sampleRate = currentSampleRate;
    

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        float sampleL = 0.0f;
        float sampleR = 0.0f;

        if (mode == InputMode::Test)
        {
            float mixedSample = 0.0f;

            // Elabora tutte le 8 voci
            for (auto& voice : voices)
            {
                if (voice.noteNumber == -1)
                    continue;

                // Inviluppo indipendente per ogni nota
                float targetEnv = voice.isPlaying ? 0.3f : 0.0f;

                voice.envelope +=
                    (targetEnv - voice.envelope) * 0.001f;

                if (voice.envelope > 0.0001f)
                {
                    // Oscillatore a onda quadra
                    float squareVal = (std::sin(voice.phase) >= 0.0)
                        ? 1.0f : -1.0f;

                    mixedSample += squareVal * voice.envelope;

                    // Avanza la fase della singola voce
                    voice.phase +=
                        2.0 * juce::MathConstants<double>::pi
                        * voice.frequency / sampleRate;

                    if (voice.phase >=
                        2.0 * juce::MathConstants<double>::pi)
                    {
                        voice.phase -=
                            2.0 * juce::MathConstants<double>::pi;
                    }
                }
                else if (!voice.isPlaying)
                {
                    // La nota è terminata: libera la voce
                    voice = SynthVoice{};
                }
            }

            // Riduce il volume complessivo per evitare clipping
            sampleL = mixedSample * 0.125f;
            sampleR = sampleL;
        }
        else
        {
            if (inputLeft != nullptr)
                sampleL = inputLeft[i];

            if (inputRight != nullptr)
                sampleR = inputRight[i];
            else
                sampleR = sampleL;
        }

        float distL = distortionModuleL.processSample(sampleL, driveVal, distMixVal);
        float distR = distortionModuleR.processSample(sampleR, driveVal, distMixVal);

        float finalL = distL;
        float finalR = distR;

        // Chorus
        float chorusDepthVal = smoothedChorusDepth.getNextValue();
        
            chorusModule.advancePhase(
                chorusRateVal,
                static_cast<float>(currentSampleRate)
            );

            finalL = chorusModule.processSampleLeft(
                distL,
                chorusDepthVal,
                chorusMixVal,
                static_cast<float>(currentSampleRate)
            );

            finalR = chorusModule.processSampleRight(
                distR,
                chorusDepthVal,
                chorusMixVal,
                static_cast<float>(currentSampleRate)
            );
        

        // Flanger
        float flangerDepthVal = smoothedFlangerDepth.getNextValue();
        finalL = flangerModuleL.processSample(finalL, flangerDepthVal);
        finalR = flangerModuleR.processSample(finalR, flangerDepthVal);

        // Filtro LPF Master
        float currentLpfCutoff = smoothedLpfCutoff.getNextValue();

        // Aggiorna i filtri soltanto quando cambia il cutoff
        if (currentLpfCutoff != lastAppliedLpfCutoff)
        {
            masterLpfL.setCutoffFrequency(currentLpfCutoff);
            masterLpfR.setCutoffFrequency(currentLpfCutoff);

            lastAppliedLpfCutoff = currentLpfCutoff;
        }

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
void HsVSTAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ValueTree state("HandSynthState");

    state.setProperty("inputMode",
        static_cast<int>(inputMode.load()), nullptr);

    state.setProperty("lpfCutoff",
        atomicLpfCutoff.load(), nullptr);

    state.setProperty("distortion",
        atomicDrive.load(), nullptr);

    state.setProperty("chorusDepth",
        atomicChorusDepth.load(), nullptr);

    state.setProperty("chorusRate",
        atomicChorusRate.load(), nullptr);

    state.setProperty("flangerDepth",
        atomicFlangerDepth.load(), nullptr);

    juce::MemoryOutputStream stream(destData, false);
    state.writeToStream(stream);
}

void HsVSTAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto state = juce::ValueTree::readFromData(data, sizeInBytes);

    if (!state.isValid() || !state.hasType("HandSynthState"))
        return;

    int savedMode = static_cast<int>(
        state.getProperty("inputMode", 0));

    setInputMode(
        savedMode == 1
        ? InputMode::ExternalAudio
        : InputMode::Test
    );

    atomicLpfCutoff.store(static_cast<float>(
        state.getProperty("lpfCutoff", 8000.0f)));

    atomicDrive.store(static_cast<float>(
        state.getProperty("distortion", 0.0f)));

    atomicChorusDepth.store(static_cast<float>(
        state.getProperty("chorusDepth", 0.0f)));

    atomicChorusRate.store(static_cast<float>(
        state.getProperty("chorusRate", 0.2f)));

    atomicFlangerDepth.store(static_cast<float>(
        state.getProperty("flangerDepth", 0.0f)));
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HsVSTAudioProcessor();
}
