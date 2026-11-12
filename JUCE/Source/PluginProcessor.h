/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>

//==============================================================================
/**
*/
class HsVSTAudioProcessor  : public juce::AudioProcessor
{
public:

    friend class MainComponent;

    // --- Modalità di ingresso ---
    enum class InputMode
    {
        Test,
        ExternalAudio
    };

    //==============================================================================
    HsVSTAudioProcessor();
    ~HsVSTAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;


    void setInputMode(InputMode mode)
    {
        inputMode.store(mode);
    }

    InputMode getInputMode() const
    {
        return inputMode.load();
    }

private:

    std::atomic<InputMode> inputMode{ InputMode::Test };

    //==============================================================================
    // Stato del generatore interno
    double currentSampleRate = 44100.0;
    float currentFrequency = 440.0f;
    double phase = 0.0;
    bool isPlaying = false;
    int activeNotes = 0;
    float envelope = 0.0f;

    // Una voce per ogni nota MIDI
    struct SynthVoice
    {
        int noteNumber = -1;
        float frequency = 440.0f;
        double phase = 0.0;
        float envelope = 0.0f;
        bool isPlaying = false;
    };
    // Sintetizzatore polifonico a 8 voci
    std::array<SynthVoice, 8> voices;


    // --- Variabili Atomiche ---
    std::atomic<float> atomicDrive{ 0.0f };
    std::atomic<float> atomicChorusDepth{ 0.0f };
    std::atomic<float> atomicChorusRate{ 0.2f };
    std::atomic<float> atomicLpfCutoff{ 8000.0f };
    std::atomic<float> atomicFlangerDepth{ 0.0f };

    // --- Smoothed Values ---
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedChorusDepth;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedLpfCutoff;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedFlangerDepth;

    // --- Filtri LPF Master ---
    juce::dsp::StateVariableTPTFilter<float> masterLpfL;
    juce::dsp::StateVariableTPTFilter<float> masterLpfR;

    // Ultimo cutoff applicato ai filtri
    float lastAppliedLpfCutoff = -1.0f;

    //FLANGER
    struct SimpleFlanger
    {
        void prepare(double sr)
        {
            currentSampleRate = sr;

            // Calcoliamo la dimensione massima
            size_t maxPossibleSize =
                static_cast<size_t>(currentSampleRate * 0.006) + 2;

            // Ridimensiona e azzera il buffer a ogni inizializzazione
            buffer.assign(maxPossibleSize, 0.0f);

            writePos = 0;
            lfoPhase = 0.0f;
        }

        float processSample(float input, float depth)
        {
            if (buffer.empty()) return input;
            const size_t bufSize = buffer.size();
            if (bufSize < 2) return input;
            

            lfoPhase += 2.0f * static_cast<float>(juce::MathConstants<double>::pi) * 0.4f / static_cast<float>(currentSampleRate);
            if (lfoPhase >= 2.0f * static_cast<float>(juce::MathConstants<double>::pi))
                lfoPhase -= 2.0f * static_cast<float>(juce::MathConstants<double>::pi);

            float lfo = (std::sin(lfoPhase) + 1.0f) * 0.5f;
            float minDelay = 0.0005f * static_cast<float>(currentSampleRate);
            float maxDelay = 0.0050f * static_cast<float>(currentSampleRate);
            float delaySamples = minDelay + lfo * (maxDelay - minDelay);

            // Scrittura sicura
            size_t wIdx = writePos % bufSize;
            buffer[wIdx] = input;

            // Lettura fluttuante
            float readPos = static_cast<float>(writePos) - delaySamples;
            while (readPos < 0.0f) readPos += static_cast<float>(bufSize);
            while (readPos >= static_cast<float>(bufSize)) readPos -= static_cast<float>(bufSize);

            // Calcolo indici con modulo intero sicuro
            int intBufSize = static_cast<int>(bufSize);
            int readIdx = static_cast<int>(readPos) % intBufSize;
            if (readIdx < 0) readIdx += intBufSize;

            int nextIdx = (readIdx + 1) % intBufSize;

            float frac = readPos - static_cast<float>(readIdx);

            // --- PROTEZIONE ANTI-CRASH DI EMERGENZA ---
            // Se per qualsiasi motivo anomalo gli indici escono dai ranghi, 
            // restituisce il segnale pulito invece di far crashare Visual Studio.
            if (readIdx < 0 || readIdx >= intBufSize || nextIdx < 0 || nextIdx >= intBufSize)
                return input;

            // Accesso sicuro al std::vector
            float delayedSample = buffer[readIdx] * (1.0f - frac) + buffer[nextIdx] * frac;

            writePos = (writePos + 1) % intBufSize;

            if (depth < 0.001f)
                return input;

            return input + delayedSample * depth * 0.7f;
        }

        double currentSampleRate = 44100.0;
        std::vector<float> buffer;
        size_t writePos = 0;
        float lfoPhase = 0.0f;
    };


    struct SimpleDelayLine {
        std::vector<float> buffer;
        int writeIndex = 0;
        float sampleRate = 44100.0f;

        // Costruttore di default sicuro per evitare corruzioni nei container
        SimpleDelayLine() = default;

        void init(float maxDelaySeconds, float sr)
        {
            sampleRate = sr;

            // Dimensiona il buffer usando il sample rate reale
            int maxPossibleSize =
                static_cast<int>(maxDelaySeconds * sampleRate) + 2;

            // Ridimensiona e azzera sempre il buffer
            buffer.assign(maxPossibleSize, 0.0f);

            writeIndex = 0;
        }

        void pushSample(float sample) {
            if (buffer.empty()) return;

            size_t bufSize = buffer.size();
            writeIndex = writeIndex % static_cast<int>(bufSize);
            if (writeIndex < 0) writeIndex += static_cast<int>(bufSize);

            buffer[writeIndex] = sample; // Accesso nativo con vector
            writeIndex = (writeIndex + 1) % static_cast<int>(bufSize);
        }


        float popSample(float delayInSamples)
        {
            if (buffer.size() < 2)
                return 0.0f;

            const size_t bufSize = buffer.size();

            if (!std::isfinite(delayInSamples))
                return 0.0f;

            // Limita il ritardo ai valori validi
            delayInSamples = std::clamp(
                delayInSamples,
                0.0f,
                static_cast<float>(bufSize - 1)
            );

            // Normalizza l'indice di scrittura
            int safeWriteIndex = writeIndex % static_cast<int>(bufSize);

            if (safeWriteIndex < 0)
                safeWriteIndex += static_cast<int>(bufSize);

            // Usa double per evitare errori di precisione
            double readPos = static_cast<double>(safeWriteIndex)
                - static_cast<double>(delayInSamples);

            const double size = static_cast<double>(bufSize);

            readPos = std::fmod(readPos, size);

            if (readPos < 0.0)
                readPos += size;

            // Protezione contro arrotondamenti numerici
            if (readPos >= size)
                readPos = 0.0;

            if (readPos < 0.0)
                readPos = 0.0;

            // Indice sempre entro i limiti del vettore
            const size_t readIdx = std::min(
                static_cast<size_t>(readPos),
                bufSize - 1
            );

            const size_t nextIdx = (readIdx + 1) % bufSize;

            const double frac = readPos - static_cast<double>(readIdx);

            // Lettura interpolata
            return buffer[readIdx] * static_cast<float>(1.0 - frac)
                + buffer[nextIdx] * static_cast<float>(frac);
        }

    };
    


    // CHORUS
    struct ChorusModule {

        std::array<SimpleDelayLine, 3> delayLinesLeft;
        std::array<SimpleDelayLine, 3> delayLinesRight;

        // Tre fasi indipendenti per le tre voci
        float phase1 = 0.0f;
        float phase2 = 2.094f; // 120 gradi
        float phase3 = 4.188f; // 240 gradi

        void prepare(float sampleRate) {
            for (auto& dl : delayLinesLeft)  dl.init(0.04f, sampleRate);
            for (auto& dl : delayLinesRight) dl.init(0.04f, sampleRate);
            phase1 = 0.0f;
            phase2 = 2.094f;
            phase3 = 4.188f;
        }

        void advancePhase(float rate, float sampleRate) {
            // Voci con velocità leggermente diverse per evitare il beating metallico
            float twoPi = 2.0f * juce::MathConstants<float>::pi;

            phase1 += (twoPi * (rate * 1.00f)) / sampleRate;
            phase2 += (twoPi * (rate * 0.85f)) / sampleRate;
            phase3 += (twoPi * (rate * 1.18f)) / sampleRate;

            if (phase1 >= twoPi) phase1 -= twoPi;
            if (phase2 >= twoPi) phase2 -= twoPi;
            if (phase3 >= twoPi) phase3 -= twoPi;
        }

        float processSampleLeft(float sample, float depth, float mix, float sampleRate) {
            delayLinesLeft[0].pushSample(sample);
            delayLinesLeft[1].pushSample(sample);
            delayLinesLeft[2].pushSample(sample);

            float lfo1 = std::sin(phase1);
            float lfo2 = std::sin(phase2);
            float lfo3 = std::sin(phase3);

            // Ritardi base differenziati per ogni voce (spaziatura temporale)
            float baseD1 = 0.008f * sampleRate; // 8 ms
            float baseD2 = 0.014f * sampleRate; // 14 ms
            float baseD3 = 0.021f * sampleRate; // 21 ms

            float depthSamples = depth * 0.006f * sampleRate;

            float d1 = baseD1 + lfo1 * depthSamples;
            float d2 = baseD2 + lfo2 * depthSamples;
            float d3 = baseD3 + lfo3 * depthSamples;

            float wet = (delayLinesLeft[0].popSample(d1) +
                delayLinesLeft[1].popSample(d2) +
                delayLinesLeft[2].popSample(d3)) / 3.0f;

            float effectiveMix = mix * depth;
            return (1.0f - effectiveMix) * sample + effectiveMix * wet;
        }

        float processSampleRight(float sample, float depth, float mix, float sampleRate) {
            delayLinesRight[0].pushSample(sample);
            delayLinesRight[1].pushSample(sample);
            delayLinesRight[2].pushSample(sample);

            float lfo1 = std::cos(phase1);
            float lfo2 = std::cos(phase2);
            float lfo3 = std::cos(phase3);

            // Ritardi base differenziati per il canale destro
            float baseD1 = 0.009f * sampleRate; // 9 ms
            float baseD2 = 0.016f * sampleRate; // 16 ms
            float baseD3 = 0.023f * sampleRate; // 23 ms

            float depthSamples = depth * 0.006f * sampleRate;

            float d1 = baseD1 + lfo1 * depthSamples;
            float d2 = baseD2 + lfo2 * depthSamples;
            float d3 = baseD3 + lfo3 * depthSamples;

            float wet = (delayLinesRight[0].popSample(d1) +
                delayLinesRight[1].popSample(d2) +
                delayLinesRight[2].popSample(d3)) / 3.0f;

            float effectiveMix = mix * depth;
            return (1.0f - effectiveMix) * sample + effectiveMix * wet;
        }
    };


    // 3 DISTORSIONE
    struct DistortionModule {

        juce::dsp::StateVariableTPTFilter<float> lowPassFilter;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedDrive;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedMix;

        void prepare(const juce::dsp::ProcessSpec& spec) {
            lowPassFilter.prepare(spec);
            lowPassFilter.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
            lowPassFilter.setCutoffFrequency(1000.0f);

            smoothedDrive.reset(spec.sampleRate, 0.02);
            smoothedMix.reset(spec.sampleRate, 0.02);
        }

        float processSample(float inputSample, float targetDrive, float targetMix) {
            smoothedDrive.setTargetValue(targetDrive);
            smoothedMix.setTargetValue(targetMix);

            float currentDrive = smoothedDrive.getNextValue();
            float currentMix = smoothedMix.getNextValue();

            // 1. Guadagno e saturazione aggressiva
            float gain = 1.0f + (currentDrive * 7.0f);
            float distorted = inputSample * gain;
            distorted = std::tanh(distorted * 1.5f);

            // 2. FILTRO PASSA-BASSO DINAMICO (Da 1000 Hz a 6500 Hz in base al Drive)
            float cutoffFreq = 1000.0f + (currentDrive * 5500.0f);
            lowPassFilter.setCutoffFrequency(cutoffFreq);

            distorted = lowPassFilter.processSample(0, distorted);

            return (1.0f - currentMix) * inputSample + currentMix * distorted;
        }

    };
    

    SimpleFlanger flangerModuleL;
    SimpleFlanger flangerModuleR;

    ChorusModule chorusModule;

    DistortionModule distortionModuleL;
    DistortionModule distortionModuleR;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HsVSTAudioProcessor)
};
