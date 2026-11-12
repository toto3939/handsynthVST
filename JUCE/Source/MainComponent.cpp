#include "MainComponent.h"
#include "PluginProcessor.h"


void MainComponent::sendAll()
{
    float leftOpenRaw = static_cast<float>(leftHandKnob.getValue());

    float leftOpen = leftOpenRaw;
    if (leftOpenRaw > 1.0f)
    {
        leftOpen = (leftOpenRaw < 15.0f) ? 0.0f : leftOpenRaw;
        leftOpen = leftOpen / 100.0f;
    }
    leftOpen = juce::jlimit(0.0f, 1.0f, leftOpen);

    float rightOpen = static_cast<float>(rightHandKnob.getValue());
    float leftThumb = static_cast<float>(leftThumbSlider.getValue());
    float rightThumb = static_cast<float>(rightThumbSlider.getValue());

    // --- AGGIORNAMENTO ETICHETTE CON VALORI PULITI ---
    double minFreq = 200.0;
    double maxFreq = 8000.0;
    double freq = minFreq * std::pow(maxFreq / minFreq, leftOpen);
    leftKnobLabel.setText("LPF Cutoff: " + juce::String((int)freq) + " Hz", juce::dontSendNotification);

    int flangerPct = static_cast<int>(leftThumb * 100.0);
    leftThumbLabel.setText("Flanger: " + juce::String(flangerPct) + " %", juce::dontSendNotification);

    int chorusPct = static_cast<int>(rightOpen * 100.0);
    rightKnobLabel.setText("Chorus: " + juce::String(chorusPct) + " %", juce::dontSendNotification);

    int distPct = static_cast<int>(rightThumb * 100.0);
    rightThumbLabel.setText("Distortion: " + juce::String(distPct) + " %", juce::dontSendNotification);
    // ------------------------------------------------

    // --- SICUREZZA THREAD: USIAMO SOLO LE ATOMICHE DALLA GUI ---
    float lpfFreq = static_cast<float>(freq);

    // Invece di toccare le smoothed value qui, aggiorniamo le variabili atomiche nel Processor.
    // Sarà il processBlock nel thread audio a leggerle e applicare il target in modo sicuro.
    processor.atomicLpfCutoff.store(lpfFreq);
    processor.atomicDrive.store(rightThumb);
    processor.atomicChorusDepth.store(rightOpen);
    processor.atomicFlangerDepth.store(leftThumb);

    if (testCppChorus)
    {
        processor.atomicChorusDepth.store(rightOpen);
    }
    else
    {
        sender.send("/hand/right/open", rightOpen);
    }

    sender.send("/hand/left/open", leftOpen);
    sender.send("/hand/left/thumb", leftThumb);
    sender.send("/hand/right/thumb", rightThumb);

    processingSender.send("/hand/left/open", leftOpen);
    processingSender.send("/hand/right/open", rightOpen);
    processingSender.send("/hand/left/thumb", leftThumb);
    processingSender.send("/hand/right/thumb", rightThumb);
}



MainComponent::MainComponent(HsVSTAudioProcessor& processorRef)
    : processor(processorRef)
{


    // --- Selezione della sorgente audio ---
    addAndMakeVisible(inputModeLabel);
    inputModeLabel.setText("Audio Source", juce::dontSendNotification);
    inputModeLabel.setJustificationType(juce::Justification::centred);

    addAndMakeVisible(inputModeSelector);

    inputModeSelector.addItem("Test - Square Wave", 1);
    inputModeSelector.addItem("External Audio", 2);

    // Sincronizza il selettore con la modalità del processore
    inputModeSelector.setSelectedId(
        processor.getInputMode() == HsVSTAudioProcessor::InputMode::Test
        ? 1 : 2,
        juce::dontSendNotification
    );

    // Aggiorna il processore quando l'utente cambia modalità
    inputModeSelector.onChange = [this]()
        {
            if (inputModeSelector.getSelectedId() == 1)
            {
                processor.setInputMode(HsVSTAudioProcessor::InputMode::Test);
                DBG("Modalita selezionata: TEST");
            }
            else if (inputModeSelector.getSelectedId() == 2)
            {
                processor.setInputMode(HsVSTAudioProcessor::InputMode::ExternalAudio);
                DBG("Modalita selezionata: EXTERNAL AUDIO");
            }
        };


    // --- 1. CONNETTI SUBITO L'OSC IN CIMA ---
    sender.connect("127.0.0.1", 57130);
    processingSender.connect("127.0.0.1", 9001);

    if (!ascoltatoreOSC.connect(9000))
    {
        DBG("Errore: Impossibile connettersi alla porta OSC di ricezione 9000!");
    }
    ascoltatoreOSC.addListener(this);



    // ==========================================
    // 1. LPF Cutoff
    // ==========================================
    addAndMakeVisible(leftHandKnob);
    leftHandKnob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    leftHandKnob.setRange(0.0, 1.0);
    leftHandKnob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    leftHandKnob.setInterceptsMouseClicks(true, false);
    // Legge il cutoff attuale dal processore
    float currentCutoff = processor.atomicLpfCutoff.load();

    // Converte la frequenza nel valore normalizzato della manopola (0-1)
    float normalizedCutoff = std::log(currentCutoff / 200.0f)
        / std::log(8000.0f / 200.0f);

    // Imposta la manopola senza generare notifiche
    leftHandKnob.setValue(
        juce::jlimit(0.0f, 1.0f, normalizedCutoff),
        juce::dontSendNotification
    );




    leftHandKnob.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff00a8b5).withAlpha(0.12f));
    leftHandKnob.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff00a8b5).withAlpha(0.6f));
    leftHandKnob.setColour(juce::Slider::thumbColourId, juce::Colour(0xff77e4d4));

    addAndMakeVisible(leftKnobLabel);
    leftKnobLabel.setJustificationType(juce::Justification::centred);
    leftKnobLabel.setColour(juce::Label::textColourId, juce::Colour(0xff77e4d4));
    leftKnobLabel.setFont(juce::Font(18.0f));

    // ==========================================
    // 2. Flanger (Left Thumb)
    // ==========================================
    addAndMakeVisible(leftThumbSlider);
    leftThumbSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    leftThumbSlider.setRange(0.0, 1.0);
    leftThumbSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    leftThumbSlider.setInterceptsMouseClicks(true, false);
    leftThumbSlider.setValue(
        juce::jlimit(0.0f, 1.0f, processor.atomicFlangerDepth.load()),
        juce::dontSendNotification
    );

    leftThumbSlider.setColour(juce::Slider::trackColourId, juce::Colour(0xff00a8b5).withAlpha(0.6f));
    leftThumbSlider.setColour(juce::Slider::thumbColourId, juce::Colour(0xff77e4d4));

    addAndMakeVisible(leftThumbLabel);
    leftThumbLabel.setJustificationType(juce::Justification::centred);
    leftThumbLabel.setColour(juce::Label::textColourId, juce::Colour(0xff77e4d4));
    leftThumbLabel.setFont(juce::Font(18.0f));

    // ==========================================
    // 3. Chorus (Right Hand)
    // ==========================================
    addAndMakeVisible(rightHandKnob);
    rightHandKnob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    rightHandKnob.setRange(0.0, 1.0);
    rightHandKnob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    rightHandKnob.setInterceptsMouseClicks(true, false);
    rightHandKnob.setValue(
        juce::jlimit(0.0f, 1.0f, processor.atomicChorusDepth.load()),
        juce::dontSendNotification
    );

    rightHandKnob.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff4a7c59).withAlpha(0.12f));
    rightHandKnob.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff4a7c59).withAlpha(0.6f));
    rightHandKnob.setColour(juce::Slider::thumbColourId, juce::Colour(0xff9cb380));

    addAndMakeVisible(rightKnobLabel);
    rightKnobLabel.setJustificationType(juce::Justification::centred);
    rightKnobLabel.setColour(juce::Label::textColourId, juce::Colour(0xff9cb380));
    rightKnobLabel.setFont(juce::Font(18.0f));

    // ==========================================
    // 4. Distortion (Right Thumb)
    // ==========================================
    addAndMakeVisible(rightThumbSlider);
    rightThumbSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    rightThumbSlider.setRange(0.0, 1.0);
    rightThumbSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    rightThumbSlider.setInterceptsMouseClicks(true, false);
    rightThumbSlider.setValue(
        juce::jlimit(0.0f, 1.0f, processor.atomicDrive.load()),
        juce::dontSendNotification
    );

    rightThumbSlider.setColour(juce::Slider::trackColourId, juce::Colour(0xff4a7c59).withAlpha(0.6f));
    rightThumbSlider.setColour(juce::Slider::thumbColourId, juce::Colour(0xff9cb380));

    addAndMakeVisible(rightThumbLabel);
    rightThumbLabel.setJustificationType(juce::Justification::centred);
    rightThumbLabel.setColour(juce::Label::textColourId, juce::Colour(0xff9cb380));
    rightThumbLabel.setFont(juce::Font(18.0f));

    // ==========================================
    // Bottoni Toggle
    // ==========================================
    juce::Colour offGrey(0xff2a2d34);

    addAndMakeVisible(leftHandToggle);
    leftHandToggle.setClickingTogglesState(true);
    leftHandToggle.setColour(juce::TextButton::buttonColourId, offGrey);
    leftHandToggle.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff77e4d4).withAlpha(0.6f));
    leftHandToggle.setToggleState(false, juce::dontSendNotification);

    addAndMakeVisible(rightHandToggle);
    rightHandToggle.setClickingTogglesState(true);
    rightHandToggle.setColour(juce::TextButton::buttonColourId, offGrey);
    rightHandToggle.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff9cb380).withAlpha(0.6f));
    rightHandToggle.setToggleState(false, juce::dontSendNotification);

    addAndMakeVisible(leftThumbToggle);
    leftThumbToggle.setClickingTogglesState(true);
    leftThumbToggle.setColour(juce::TextButton::buttonColourId, offGrey);
    leftThumbToggle.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff77e4d4).withAlpha(0.6f));
    leftThumbToggle.setToggleState(false, juce::dontSendNotification);

    addAndMakeVisible(rightThumbToggle);
    rightThumbToggle.setClickingTogglesState(true);
    rightThumbToggle.setColour(juce::TextButton::buttonColourId, offGrey);
    rightThumbToggle.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff9cb380).withAlpha(0.6f));
    rightThumbToggle.setToggleState(false, juce::dontSendNotification);


    leftHandToggle.onClick = [this]() { if (!leftHandToggle.getToggleState()) { sender.send("/hand/left/open", 0.0f); processingSender.send("/hand/left/open", 0.0f); } sendAll(); };
    rightHandToggle.onClick = [this]() { if (!rightHandToggle.getToggleState()) { sender.send("/hand/right/open", 0.0f); processingSender.send("/hand/right/open", 0.0f); } sendAll(); };
    leftThumbToggle.onClick = [this]() { if (!leftThumbToggle.getToggleState()) { sender.send("/hand/left/thumb", 0.0f); processingSender.send("/hand/left/thumb", 0.0f); } sendAll(); };
    rightThumbToggle.onClick = [this]() { if (!rightThumbToggle.getToggleState()) { sender.send("/hand/right/thumb", 0.0f); processingSender.send("/hand/right/thumb", 0.0f); } sendAll(); };

    leftHandKnob.onValueChange = [this]() { sendAll(); };
    rightHandKnob.onValueChange = [this]() { sendAll(); };
    leftThumbSlider.onValueChange = [this]() { sendAll(); };
    rightThumbSlider.onValueChange = [this]() { sendAll(); };

    // NOTA: La configurazione delle smoothed values (reset e currentAndTargetValue) 
    // va spostata nel PluginProcessor (nel metodo prepareToPlay), poiché appartengono al DSP!

    sendAll(); // Sincronizzazione iniziale

    setSize(1000, 700);
}


MainComponent::~MainComponent()
{
}




void MainComponent::oscMessageReceived(const juce::OSCMessage& message)
{
    auto pattern = message.getAddressPattern().toString();
    if (message.size() > 0 && message[0].isFloat32())
    {
        float val = message[0].getFloat32();
        DBG("OSC IN: " + pattern + " = " + juce::String(val));

        // Filtro anti-picco rigoroso
        if (std::isnan(val) || val < 0.0f || val > 105.0f) return;

        juce::MessageManager::callAsync([this, val, pattern]() {

            bool updated = false;

            if (pattern == "/hand/left/open" && leftHandToggle.getToggleState()) {
                float processedVal = val;
                if (val > 1.0f) {
                    processedVal = (val < 15.0f) ? 0.0f : val;
                    processedVal = processedVal / 100.0f;
                }
                processedVal = juce::jlimit(0.0f, 1.0f, processedVal);
                leftHandKnob.setValue(processedVal, juce::dontSendNotification);
                updated = true;
            }
            else if (pattern == "/hand/right/open" && rightHandToggle.getToggleState()) {
                rightHandKnob.setValue(juce::jlimit(0.0f, 1.0f, val), juce::dontSendNotification);
                updated = true;
            }
            else if (pattern == "/hand/left/thumb" && leftThumbToggle.getToggleState()) {
                leftThumbSlider.setValue(juce::jlimit(0.0f, 1.0f, val), juce::dontSendNotification);
                updated = true;
            }
            else if (pattern == "/hand/right/thumb" && rightThumbToggle.getToggleState()) {
                rightThumbSlider.setValue(juce::jlimit(0.0f, 1.0f, val), juce::dontSendNotification);
                updated = true;
            }

            if (updated) {
                sendAll(); // Aggiorna istantaneamente GUI, etichette e DSP!
            }
            });
    }
}


void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);
    g.setColour(juce::Colour(0x22ffffff));
    g.drawRect(getLocalBounds(), 1);
}


void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(40, 30);


    // --- Posizionamento selettore sorgente audio ---
    auto inputModeArea = area.removeFromTop(65);

    inputModeLabel.setBounds(
        inputModeArea.removeFromLeft(150)
    );

    inputModeSelector.setBounds(
        inputModeArea.removeFromLeft(220).reduced(5, 15)
    );


    auto middleRowArea = area.removeFromTop(area.getHeight() * 0.5f);
    auto bottomRowArea = area;

    int dividingLineY = middleRowArea.getBottom();
    int centerX = getLocalBounds().getCentreX();

    auto leftKnobZone = middleRowArea.removeFromLeft(middleRowArea.getWidth() * 0.5f);
    auto rightKnobZone = middleRowArea;

    int mainKnobSize = 200;
    leftHandKnob.setBounds(leftKnobZone.withSizeKeepingCentre(mainKnobSize, mainKnobSize).translated(0, -10));
    leftKnobLabel.setBounds(leftKnobZone.getX() + 10, leftHandKnob.getBottom() + 2, leftKnobZone.getWidth() - 20, 45);

    rightHandKnob.setBounds(rightKnobZone.withSizeKeepingCentre(mainKnobSize, mainKnobSize).translated(0, -10));
    rightKnobLabel.setBounds(rightKnobZone.getX() + 10, rightHandKnob.getBottom() + 2, rightKnobZone.getWidth() - 20, 45);

    auto leftSliderZone = bottomRowArea.removeFromLeft(bottomRowArea.getWidth() * 0.5f);
    auto rightSliderZone = bottomRowArea;

    leftThumbSlider.setBounds(leftSliderZone.withSizeKeepingCentre(320, 60).translated(0, -10));
    leftThumbLabel.setBounds(leftSliderZone.getX() + 10, leftThumbSlider.getBottom() + 2, leftSliderZone.getWidth() - 20, 45);

    rightThumbSlider.setBounds(rightSliderZone.withSizeKeepingCentre(320, 60).translated(0, -10));
    rightThumbLabel.setBounds(rightSliderZone.getX() + 10, rightThumbSlider.getBottom() + 2, rightSliderZone.getWidth() - 20, 45);

    int btnSize = 35;
    int gap = 2;
    leftHandToggle.setBounds(centerX - btnSize - gap, dividingLineY - btnSize - gap, btnSize, btnSize);
    rightHandToggle.setBounds(centerX + gap, dividingLineY - btnSize - gap, btnSize, btnSize);
    leftThumbToggle.setBounds(centerX - btnSize - gap, dividingLineY + gap, btnSize, btnSize);
    rightThumbToggle.setBounds(centerX + gap, dividingLineY + gap, btnSize, btnSize);
}