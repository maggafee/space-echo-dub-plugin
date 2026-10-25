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
    for (auto& t : lastSeen) t.store (0);
    loadDefault();

    if (auto xml = juce::XmlDocument::parse (getSettingsFile()))
        directEnabled = xml->getBoolAttribute ("directMidiInput", true);
    startTimerHz (60);
}

MidiLearn::~MidiLearn()
{
    stopTimer();
    const juce::ScopedLock sl (deviceLock);
    for (auto& d : devices) d->stop();
    devices.clear();
}

juce::File MidiLearn::getSettingsFile()
{
    return getDefaultFile().getSiblingFile ("Settings.xml");
}

void MidiLearn::setDirectInputAvailable (bool available)
{
    directAvailable = available;
    rescanCountdown = 0;
}

void MidiLearn::setDirectInputEnabled (bool enabled)
{
    directEnabled = enabled;
    rescanCountdown = 0;
    auto f = getSettingsFile();
    f.getParentDirectory().createDirectory();
    juce::XmlElement xml ("DUBASCO_SETTINGS");
    xml.setAttribute ("directMidiInput", enabled);
    xml.writeTo (f);
    sendChangeMessage();
}

void MidiLearn::updateDevices()
{
    // open every MIDI input of the computer (hot-plugged devices are picked up by the periodic rescan)
    const bool want = directAvailable && directEnabled;
    const auto available = want ? juce::MidiInput::getAvailableDevices() : juce::Array<juce::MidiDeviceInfo>();

    const juce::ScopedLock sl (deviceLock);
    for (auto it = devices.begin(); it != devices.end();)
    {
        const bool stillThere = std::any_of (available.begin(), available.end(),
                                             [&] (const juce::MidiDeviceInfo& i) { return i.identifier == (*it)->getIdentifier(); });
        if (! stillThere) { (*it)->stop(); it = devices.erase (it); }
        else ++it;
    }
    for (auto& info : available)
    {
        const bool open = std::any_of (devices.begin(), devices.end(),
                                       [&] (const std::unique_ptr<juce::MidiInput>& d) { return d->getIdentifier() == info.identifier; });
        if (open) continue;
        if (auto in = juce::MidiInput::openDevice (info.identifier, this))   // fails if another app holds it exclusively (Windows)
        {
            in->start();
            devices.push_back (std::move (in));
        }
    }
}

juce::StringArray MidiLearn::getOpenDeviceNames() const
{
    juce::StringArray names;
    const juce::ScopedLock sl (deviceLock);
    for (auto& d : devices) names.add (d->getName());
    return names;
}

juce::String MidiLearn::getLastMessageText() const
{
    const auto v = lastMsg.load();
    if ((v & 0x80000000u) == 0) return {};
    const int type = (int) ((v >> 25) & 3), ch = (int) ((v >> 21) & 15) + 1, num = (int) ((v >> 14) & 127), val = (int) ((v >> 7) & 127);
    const bool dev = ((v >> 27) & 1) != 0;
    juce::String t = type == 0 ? "CC " + juce::String (num) + " = " + juce::String (val)
                               : juce::MidiMessage::getMidiNoteName (num, true, true, 3) + (type == 1 ? " on" : " off");
    return t + "  (Ch " + juce::String (ch) + ", " + (dev ? "device" : "host") + ")";
}

void MidiLearn::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& msg)
{
    handle (msg, fromDevice);
}

bool MidiLearn::isDuplicate (int slot, int value, Source src) noexcept
{
    const auto now = (juce::uint64) juce::Time::getMillisecondCounter();
    const auto packed = (now << 9) | ((juce::uint64) src << 8) | (juce::uint64) (value & 127);
    const auto prev = lastSeen[(size_t) slot].exchange (packed);
    if (prev == 0) return false;
    const auto prevTime = prev >> 9;
    const auto prevSrc = (int) ((prev >> 8) & 1);
    const int prevVal = (int) (prev & 127);
    // the host delivers with one or two buffers of latency: treat the same value from the
    // other source within 150 ms as the same physical event
    if (prevSrc != (int) src && prevVal == (value & 127) && now - prevTime < 150)
    {
        lastSeen[(size_t) slot].store (prev);   // keep the first arrival as reference
        return true;
    }
    return false;
}

void MidiLearn::processMidi (const juce::MidiBuffer& midi, int numSamples)
{
    for (auto& o : overrides)
    {
        const int left = o->samplesLeft.load (std::memory_order_relaxed);
        if (left > 0) o->samplesLeft.store (std::max (0, left - numSamples), std::memory_order_relaxed);
    }

    for (const auto meta : midi)
        handle (meta.getMessage(), fromHost);
}

void MidiLearn::handle (const juce::MidiMessage& msg, Source src)
{
    const bool isCC = msg.isController();
    const bool isNoteOn = msg.isNoteOn();
    const bool isNoteOff = msg.isNoteOff();
    if (! (isCC || isNoteOn || isNoteOff))
        return;

    const int ch = msg.getChannel() - 1;
    const int num = isCC ? msg.getControllerNumber() : msg.getNoteNumber();
    const int value = isCC ? msg.getControllerValue() : (isNoteOn ? msg.getVelocity() : 0);
    const int type = isCC ? 0 : (isNoteOn ? 1 : 2);
    if (isDuplicate (type * 2048 + key (ch, num), isNoteOn ? 1 : value, src))
        return;

    activity.fetch_add (1, std::memory_order_relaxed);
    lastMsg.store (0x80000000u | ((juce::uint32) src << 27) | ((juce::uint32) type << 25) | ((juce::uint32) ch << 21)
                   | ((juce::uint32) num << 14) | ((juce::uint32) (value & 127) << 7));

    auto& map = isCC ? ccMap : noteMap;

    // learning: bind the first CC / note-on to the armed parameter
    const int target = armed.load();
    if (target >= 0 && (isCC || isNoteOn))
    {
        int expected = target;
        if (armed.compare_exchange_strong (expected, -1))
        {
            for (auto& m : ccMap) if (m.load() == target) m.store (-1);
            for (auto& m : noteMap) if (m.load() == target) m.store (-1);
            map[(size_t) key (ch, num)].store (target);
            mappingChanged.store (true);
        }
        return;
    }

    const int pIndex = map[(size_t) key (ch, num)].load();
    if (pIndex < 0 || pIndex >= (int) params.size())
        return;

    const auto kind = kinds[(size_t) pIndex];
    auto* p = params[(size_t) pIndex];
    auto& o = *overrides[(size_t) pIndex];
    // the newest value: a pending override (not yet seen by the host) or the parameter itself
    float norm = o.samplesLeft.load (std::memory_order_acquire) > 0 ? p->convertTo0to1 (o.plain.load()) : p->getValue();

    if (isCC)
    {
        switch (kind)
        {
            case Kind::continuous: norm = (float) value / 127.0f; break;
            case Kind::stepped:    norm = (float) value / 127.0f; break;
            case Kind::toggle:
            case Kind::momentary:  norm = value >= 64 ? 1.0f : 0.0f; break;
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
        if (kind != Kind::momentary) return;
        norm = 0.0f;
    }

    o.plain.store (p->convertFrom0to1 (norm), std::memory_order_relaxed);
    o.samplesLeft.store ((int) (sampleRate * 0.25), std::memory_order_release);

    auto& q = queues[(size_t) src];
    int start1, size1, start2, size2;
    q.fifo.prepareToWrite (1, start1, size1, start2, size2);
    if (size1 > 0) q.events[(size_t) start1] = { pIndex, norm };
    else if (size2 > 0) q.events[(size_t) start2] = { pIndex, norm };
    q.fifo.finishedWrite (size1 + size2);
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
    for (auto& q : queues)
    {
        int start1, size1, start2, size2;
        q.fifo.prepareToRead (q.fifo.getNumReady(), start1, size1, start2, size2);
        for (int i = 0; i < size1; ++i) apply (q.events[(size_t) (start1 + i)].param, q.events[(size_t) (start1 + i)].norm);
        for (int i = 0; i < size2; ++i) apply (q.events[(size_t) (start2 + i)].param, q.events[(size_t) (start2 + i)].norm);
        q.fifo.finishedRead (size1 + size2);
    }

    if (--rescanCountdown <= 0)
    {
        rescanCountdown = 120;   // every 2 s
        updateDevices();
    }

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
