#pragma once
#include <JuceHeader.h>
#include <juce_osc/juce_osc.h>

class MainComponent : public juce::Component,
    private juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback>
{
public:
    MainComponent(class HsVSTAudioProcessor& processorRef); // Passiamo il riferimento al processor per legare i controlli
    ~MainComponent() override;

    void oscMessageReceived(const juce::OSCMessage& message) override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void sendAll();

private:
    HsVSTAudioProcessor& processor; // Riferimento al processor

    // OSC Sender/Receiver per la GUI
    juce::OSCSender sender;
    juce::OSCSender processingSender;
    juce::OSCReceiver ascoltatoreOSC;

    // Componenti GUI (Slider, Bottoni, Etichette)
    juce::Slider leftHandKnob;
    juce::Label leftKnobLabel;
    juce::Slider rightHandKnob;
    juce::Label rightKnobLabel;
    juce::Slider leftThumbSlider;
    juce::Label leftThumbLabel;
    juce::Slider rightThumbSlider;
    juce::Label rightThumbLabel;

    juce::TextButton leftHandToggle;
    juce::TextButton rightHandToggle;
    juce::TextButton leftThumbToggle;
    juce::TextButton rightThumbToggle;

    // Selezione della sorgente audio
    juce::ComboBox inputModeSelector;
    juce::Label inputModeLabel;

    bool testCppChorus = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};