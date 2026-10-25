#include "PluginProcessor.h"
#include "PluginEditor.h"

DubascoProcessor::DubascoProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", params::createLayout()),
      midiLearn (apvts, currentSampleRate),
      presets (apvts)
{
    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (pid::bypass));

    auto bind = [this] (P& p, const char* id)
    {
        p.raw = apvts.getRawParameterValue (id);
        p.learnIndex = midiLearn.indexOf (id);
        jassert (p.raw != nullptr);
    };
    bind (pInput, pid::input);       bind (pInputMode, pid::inputMode); bind (pDelay, pid::delayTime);
    bind (pFeedback, pid::feedback); bind (pBass, pid::bass);           bind (pTreble, pid::treble);
    bind (pEcho, pid::echoLevel);    bind (pSat, pid::tapeSat);         bind (pPower, pid::power);
    bind (pMode, pid::mode);         bind (pReverb, pid::reverb);       bind (pDecay, pid::springDecay);
    bind (pKick, pid::kick);         bind (pWow, pid::wowFlutter);      bind (pAge, pid::tapeAge);
    bind (pInertia, pid::inertia);   bind (pSync, pid::sync);           bind (pNote, pid::syncNote);
    bind (pStereo, pid::stereoMode); bind (pLowCut, pid::lowCut);       bind (pDuck, pid::duck);
    bind (pDry, pid::dry);           bind (pOutput, pid::output);       bind (pThrow, pid::throwBtn);
    bind (pHold, pid::hold);         bind (pSend, pid::sendMode);       bind (pBypass, pid::bypass);
}

bool DubascoProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo()) return false;
    return ! (in == juce::AudioChannelSet::stereo() && out == juce::AudioChannelSet::mono());
}

void DubascoProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    dub::EngineParams p;
    collectParams (p);
    engine.setParams (p);
    engine.prepare (sampleRate, samplesPerBlock);
    scratch.setSize (2, samplesPerBlock, false, false, true);
    setLatencySamples (0);
}

void DubascoProcessor::reset()
{
    engine.reset();
}

float DubascoProcessor::get (int index, std::atomic<float>* raw) const
{
    float v;
    if (midiLearn.getOverride (index, v)) return v;
    return raw->load (std::memory_order_relaxed);
}

void DubascoProcessor::collectParams (dub::EngineParams& e)
{
    auto g = [this] (const P& p) { return get (p.learnIndex, p.raw); };
    auto on = [&] (const P& p) { return g (p) > 0.5f; };

    const float in = g (pInput) / 5.0f;
    e.inputGain = in * in;                                  // 5 = unity, 10 = +12 dB
    e.micMode = juce::roundToInt (g (pInputMode)) == 0;

    if (on (pSync))
    {
        const double bpm = juce::jlimit (30.0, 300.0, hostBpm.load());
        e.delaySec = (float) (params::syncNoteBeats (juce::roundToInt (g (pNote))) * 60.0 / bpm);
    }
    else
        e.delaySec = g (pDelay) * 0.001f;

    e.feedback = g (pFeedback) / 10.0f;
    e.bassDb = g (pBass) * 2.0f;                            // -5..+5  ->  -10..+10 dB
    e.trebleDb = g (pTreble) * 2.0f;
    const float el = g (pEcho) / 10.0f;
    e.echoGain = el * el * 1.6f;
    e.tapeSat = g (pSat) / 10.0f;
    e.power = on (pPower);
    e.mode = juce::roundToInt (g (pMode)) + 1;
    const float rv = g (pReverb) / 10.0f;
    e.reverbGain = rv * rv * 1.4f;
    e.springDecay = g (pDecay) / 10.0f;
    e.kick = on (pKick);
    e.wowFlutter = g (pWow) / 10.0f;
    e.tapeAge = g (pAge) / 10.0f;
    e.inertia = g (pInertia) / 10.0f;
    e.stereoMode = juce::roundToInt (g (pStereo));
    e.lowCutHz = g (pLowCut);
    e.duck = g (pDuck) / 10.0f;
    const float dr = g (pDry) / 10.0f;
    e.dryGain = dr * dr;
    e.outGain = juce::Decibels::decibelsToGain (g (pOutput));
    e.throwOn = on (pThrow);
    e.hold = on (pHold);
    e.throwOnly = juce::roundToInt (g (pSend)) == 1;
    e.bypass = pBypass.raw->load() > 0.5f;
}

void DubascoProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();

    midiLearn.processMidi (midi, n);

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                hostBpm.store (*bpm);

    dub::EngineParams p;
    collectParams (p);
    engine.setParams (p);

    if (n == 0 || numOut == 0) return;
    for (int c = numIn; c < numOut; ++c)
        buffer.clear (c, 0, n);

    float* L = buffer.getWritePointer (0);
    float* R = numOut > 1 ? buffer.getWritePointer (1) : nullptr;
    const float* inL = buffer.getReadPointer (0);
    const float* inR = numIn > 1 ? buffer.getReadPointer (1) : inL;

    if (R == nullptr)
    {
        // mono out: render stereo into scratch, fold down
        if (scratch.getNumSamples() < n) scratch.setSize (2, n, false, false, true);
        float* sL = scratch.getWritePointer (0);
        float* sR = scratch.getWritePointer (1);
        engine.process (inL, inL, sL, sR, n);
        for (int i = 0; i < n; ++i) L[i] = 0.5f * (sL[i] + sR[i]);
    }
    else
    {
        engine.process (inL, inR, L, R, n);
    }

    midi.clear();
}

// --------------------------------------------------------------------------- programs
int DubascoProcessor::getNumPrograms() { return presets.getNumFactory(); }
int DubascoProcessor::getCurrentProgram()
{
    const int c = presets.getCurrentIndex();
    return c < presets.getNumFactory() ? c : 0;
}
void DubascoProcessor::setCurrentProgram (int i) { presets.loadFactory (i); }
const juce::String DubascoProcessor::getProgramName (int i) { return presets.getFactoryName (i); }

// --------------------------------------------------------------------------- state
void DubascoProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::ValueTree root ("DUBASCO_STATE");
    root.setProperty ("version", 1, nullptr);
    root.setProperty ("presetName", presets.getCurrentName(), nullptr);
    root.setProperty ("vuSource", vuSource.load(), nullptr);
    root.setProperty ("editorScale", editorScale.load(), nullptr);
    root.appendChild (apvts.copyState(), nullptr);
    root.appendChild (midiLearn.toValueTree(), nullptr);
    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, dest);
}

void DubascoProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr) return;
    auto root = juce::ValueTree::fromXml (*xml);
    if (! root.hasType ("DUBASCO_STATE")) return;

    auto params = root.getChildWithName (apvts.state.getType());
    if (params.isValid())
    {
        // A host may have pushed an un-snapped value into a discrete parameter (e.g. 0.49 into a
        // switch). APVTS compares denormalised values, so it would not restore such a parameter.
        // Snap everything first so replaceState() sees the real difference.
        for (auto* p : getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                const float snapped = rp->convertTo0to1 (rp->convertFrom0to1 (rp->getValue()));
                if (std::abs (snapped - rp->getValue()) > 1.0e-6f)
                    rp->setValueNotifyingHost (snapped);
            }
        apvts.replaceState (params);
    }
    auto map = root.getChildWithName ("MIDIMAP");
    if (map.isValid() && map.getNumChildren() > 0)
        midiLearn.fromValueTree (map);
    presets.setCurrentName (root.getProperty ("presetName", presets.getCurrentName()).toString());
    vuSource.store ((int) root.getProperty ("vuSource", 0));
    editorScale.store ((float) root.getProperty ("editorScale", 0.66f));
}

juce::AudioProcessorEditor* DubascoProcessor::createEditor() { return new DubascoEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DubascoProcessor(); }
