#include "MidiLearn.h"
#include "Parameters.h"

MidiLearn::MidiLearn (juce::AudioProcessorValueTreeState& state, double& sampleRateRef)
    : apvts (state), sampleRate (sampleRateRef)
{
    for (auto* p : state.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            if (rp->getParameterID() == pid::bypass)
                continue;   // bypass belongs to the host
            paramIds.add (rp->getParameterID());
            params.push_back (rp);
            const auto& id = rp->getParameterID();
            Kind k = Kind::continuous;
            if (id == pid::kick || id == pid::throwBtn) k = Kind::momentary;
            else if (dynamic_cast<juce::AudioParameterBool*> (rp) != nullptr) k = Kind::toggle;
            else if (dynamic_cast<juce::AudioParameterChoice*> (rp) != nullptr) k = Kind::stepped;
            kinds.push_back (k);
            overrides.push_back (std::make_unique<Override>());
        }

    for (auto& m : ccMap) m.store (-1);
    for (auto& m : noteMap) m.store (-1);
    loadDefault();
    startTimerHz (60);
}

MidiLearn::~MidiLearn() { stopTimer(); }

void MidiLearn::processMidi (const juce::MidiBuffer& midi, int numSamples)
{
    for (auto& o : overrides)
    {
        const int left = o->samplesLeft.load (std::memory_order_relaxed);
        if (left > 0) o->samplesLeft.store (std::max (0, left - numSamples), std::memory_order_relaxed);
    }

    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        const bool isCC = msg.isController();
        const bool isNoteOn = msg.isNoteOn();
        const bool isNoteOff = msg.isNoteOff();
        if (! (isCC || isNoteOn || isNoteOff))
            continue;

        const int ch = msg.getChannel() - 1;
        const int num = isCC ? msg.getControllerNumber() : msg.getNoteNumber();
        auto& map = isCC ? ccMap : noteMap;

        // learning: bind the first CC / note-on to the armed parameter
        const int target = armed.load();
        if (target >= 0 && (isCC || isNoteOn))
        {
            for (auto& m : ccMap) if (m.load() == target) m.store (-1);
            for (auto& m : noteMap) if (m.load() == target) m.store (-1);
            map[(size_t) key (ch, num)].store (target);
            armed.store (-1);
            mappingChanged.store (true);
            continue;
        }

        const int pIndex = map[(size_t) key (ch, num)].load();
        if (pIndex < 0 || pIndex >= (int) params.size())
            continue;

        const auto kind = kinds[(size_t) pIndex];
        auto* p = params[(size_t) pIndex];
        float norm = p->getValue();

        if (isCC)
        {
            const int v = msg.getControllerValue();
            switch (kind)
            {
                case Kind::continuous: norm = (float) v / 127.0f; break;
                case Kind::stepped:    norm = (float) v / 127.0f; break;
                case Kind::toggle:
                case Kind::momentary:  norm = v >= 64 ? 1.0f : 0.0f; break;
            }
        }
        else if (isNoteOn)
        {
            switch (kind)
            {
                case Kind::momentary:  norm = 1.0f; break;
                case Kind::toggle:     norm = norm > 0.5f ? 0.0f : 1.0f; break;
                case Kind::continuous: norm = msg.getFloatVelocity(); break;
                case Kind::stepped:
                {
                    const int steps = std::max (2, p->getNumSteps());
                    int idx = juce::roundToInt (norm * (float) (steps - 1)) + 1;
                    if (idx >= steps) idx = 0;
                    norm = (float) idx / (float) (steps - 1);
                    break;
                }
            }
        }
        else // note off
        {
            if (kind != Kind::momentary) continue;
            norm = 0.0f;
        }

        auto& o = *overrides[(size_t) pIndex];
        o.plain.store (p->convertFrom0to1 (norm), std::memory_order_relaxed);
        o.samplesLeft.store ((int) (sampleRate * 0.25), std::memory_order_release);

        int start1, size1, start2, size2;
        fifo.prepareToWrite (1, start1, size1, start2, size2);
        if (size1 > 0) events[(size_t) start1] = { pIndex, norm };
        else if (size2 > 0) events[(size_t) start2] = { pIndex, norm };
        fifo.finishedWrite (size1 + size2);
    }
}

bool MidiLearn::getOverride (int paramIndex, float& plainValue) const noexcept
{
    if (paramIndex < 0 || paramIndex >= (int) overrides.size()) return false;
    const auto& o = *overrides[(size_t) paramIndex];
    if (o.samplesLeft.load (std::memory_order_acquire) <= 0) return false;
    plainValue = o.plain.load (std::memory_order_relaxed);
    return true;
}

void MidiLearn::timerCallback()
{
    int start1, size1, start2, size2;
    fifo.prepareToRead (fifo.getNumReady(), start1, size1, start2, size2);
    for (int i = 0; i < size1; ++i) apply (events[(size_t) (start1 + i)].param, events[(size_t) (start1 + i)].norm);
    for (int i = 0; i < size2; ++i) apply (events[(size_t) (start2 + i)].param, events[(size_t) (start2 + i)].norm);
    fifo.finishedRead (size1 + size2);

    if (mappingChanged.exchange (false))
        sendChangeMessage();
}

void MidiLearn::apply (int paramIndex, float normValue)
{
    auto* p = params[(size_t) paramIndex];
    if (std::abs (p->getValue() - normValue) < 1.0e-6f) return;
    p->beginChangeGesture();
    p->setValueNotifyingHost (normValue);
    p->endChangeGesture();
}

void MidiLearn::arm (int paramIndex)
{
    armed.store (paramIndex);
    sendChangeMessage();
}

void MidiLearn::clear (int paramIndex)
{
    for (auto& m : ccMap) if (m.load() == paramIndex) m.store (-1);
    for (auto& m : noteMap) if (m.load() == paramIndex) m.store (-1);
    sendChangeMessage();
}

void MidiLearn::clearAll()
{
    for (auto& m : ccMap) m.store (-1);
    for (auto& m : noteMap) m.store (-1);
    armed.store (-1);
    sendChangeMessage();
}

juce::String MidiLearn::describe (int paramIndex) const
{
    if (paramIndex < 0) return {};
    for (int k = 0; k < 2048; ++k)
    {
        if (ccMap[(size_t) k].load() == paramIndex)
            return "CC " + juce::String (k & 127) + " · Ch " + juce::String ((k >> 7) + 1);
        if (noteMap[(size_t) k].load() == paramIndex)
            return juce::MidiMessage::getMidiNoteName (k & 127, true, true, 3) + " · Ch " + juce::String ((k >> 7) + 1);
    }
    return {};
}

juce::ValueTree MidiLearn::toValueTree() const
{
    juce::ValueTree t ("MIDIMAP");
    for (int k = 0; k < 2048; ++k)
        for (int type = 0; type < 2; ++type)
        {
            const int p = (type == 0 ? ccMap : noteMap)[(size_t) k].load();
            if (p >= 0 && p < paramIds.size())
            {
                juce::ValueTree m ("MAP");
                m.setProperty ("type", type == 0 ? "cc" : "note", nullptr);
                m.setProperty ("channel", (k >> 7) + 1, nullptr);
                m.setProperty ("number", k & 127, nullptr);
                m.setProperty ("param", paramIds[p], nullptr);
                t.appendChild (m, nullptr);
            }
        }
    return t;
}

void MidiLearn::fromValueTree (const juce::ValueTree& t)
{
    if (! t.hasType ("MIDIMAP")) return;
    for (auto& m : ccMap) m.store (-1);
    for (auto& m : noteMap) m.store (-1);
    for (auto m : t)
    {
        const int p = paramIds.indexOf (m.getProperty ("param").toString());
        if (p < 0) continue;
        const int k = key ((int) m.getProperty ("channel") - 1, (int) m.getProperty ("number"));
        (m.getProperty ("type").toString() == "note" ? noteMap : ccMap)[(size_t) k].store (p);
    }
    sendChangeMessage();
}

juce::File MidiLearn::getDefaultFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Dubasco").getChildFile ("Tape Echo").getChildFile ("DefaultMidiMap.xml");
}

bool MidiLearn::saveAsDefault() const
{
    auto f = getDefaultFile();
    f.getParentDirectory().createDirectory();
    if (auto xml = toValueTree().createXml())
        return xml->writeTo (f);
    return false;
}

bool MidiLearn::loadDefault()
{
    auto f = getDefaultFile();
    if (! f.existsAsFile()) return false;
    if (auto xml = juce::XmlDocument::parse (f))
    {
        fromValueTree (juce::ValueTree::fromXml (*xml));
        return true;
    }
    return false;
}
