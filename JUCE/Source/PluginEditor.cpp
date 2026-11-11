/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
HsVSTAudioProcessorEditor::HsVSTAudioProcessorEditor (HsVSTAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    
    // Aggiungi questa riga per rendere visibile il tuo MainComponent:
    addAndMakeVisible (mainComponent);
    
    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    setSize(1000, 700);
    setResizable(true, true); // (permesso di ridimensionare, mantiene le proporzioni sì/no)
    setResizeLimits(600, 400, 1920, 1080); // (larghezza min, altezza min, larghezza max, altezza max)
}

HsVSTAudioProcessorEditor::~HsVSTAudioProcessorEditor()
{
}

//==============================================================================
void HsVSTAudioProcessorEditor::paint (juce::Graphics& g)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (15.0f));
    g.drawFittedText ("Hello World!", getLocalBounds(), juce::Justification::centred, 1);
}

void HsVSTAudioProcessorEditor::resized()
{
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor..
    
    mainComponent.setBounds (getLocalBounds());
    
}
