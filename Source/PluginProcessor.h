#pragma once
#include <JuceHeader.h>
#include "Parameters.h"
#include "MidiLearn.h"
#include "PresetManager.h"
#include "dsp/TapeEchoEngine.h"

class DubascoProcessor : public juce::AudioProcessor
{
public:
    DubascoProcessor();
    ~DubascoProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    juce::AudioProcessorValueTreeState apvts;
    MidiLearn midiLearn;
    PresetManager presets;
    dub::TapeEchoEngine engine;

    // UI state persisted with the session
    std::atomic<int> vuSource { 0 };           // 0 = input (record level), 1 = echo, 2 = output
    std::atomic<float> editorScale { 0.66f };
    std::atomic<double> hostBpm { 120.0 };

private:
    float get (int index, std::atomic<float>* raw) const;
    void collectParams (dub::EngineParams&);

    double currentSampleRate = 44100.0;
    juce::AudioParameterBool* bypassParam = nullptr;

    struct P { std::atomic<float>* raw = nullptr; int learnIndex = -1; };
    P pInput, pInputMode, pDelay, pFeedback, pBass, pTreble, pEcho, pSat, pPower, pMode, pReverb, pDecay, pKick, pWow, pAge,
      pInertia, pSync, pNote, pStereo, pLowCut, pDuck, pDry, pOutput, pThrow, pHold, pSend, pBypass;

    juce::AudioBuffer<float> scratch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DubascoProcessor)
};
