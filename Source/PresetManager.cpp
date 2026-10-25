#include "PresetManager.h"
#include "Parameters.h"

namespace
{
    struct FactoryPreset
    {
        const char* name;
        std::vector<std::pair<const char*, float>> values;   // plain values; choice params use the index
    };

    // Values are plain parameter values. Anything not listed falls back to the default.
    const std::vector<FactoryPreset>& factory()
    {
        static const std::vector<FactoryPreset> list = {
            { "Init - Classic Space", {} },
            { "Kingston Skank Echo", { { pid::sync, 1 }, { pid::syncNote, 6 }, { pid::mode, 4 }, { pid::feedback, 5.6f }, { pid::reverb, 4.0f },
                                       { pid::bass, -1.0f }, { pid::treble, 1.0f }, { pid::stereoMode, 1 } } },
            { "Roots Rockers Quarter", { { pid::sync, 1 }, { pid::syncNote, 8 }, { pid::mode, 0 }, { pid::feedback, 5.0f }, { pid::reverb, 2.5f },
                                         { pid::tapeAge, 4.0f }, { pid::stereoMode, 0 } } },
            { "Steppers Ping-Pong", { { pid::sync, 1 }, { pid::syncNote, 6 }, { pid::mode, 0 }, { pid::feedback, 5.8f }, { pid::stereoMode, 3 },
                                      { pid::reverb, 2.0f }, { pid::lowCut, 120.0f } } },
            { "Spiral Runaway", { { pid::delayTime, 260.0f }, { pid::mode, 3 }, { pid::feedback, 7.9f }, { pid::tapeSat, 7.0f }, { pid::wowFlutter, 4.0f },
                                  { pid::inertia, 6.5f }, { pid::reverb, 3.0f }, { pid::treble, -1.5f } } },
            { "Hazy Ark Session", { { pid::sync, 1 }, { pid::syncNote, 9 }, { pid::mode, 8 }, { pid::feedback, 5.2f }, { pid::tapeAge, 7.0f },
                                    { pid::wowFlutter, 5.0f }, { pid::treble, -2.0f }, { pid::tapeSat, 6.0f }, { pid::reverb, 5.0f }, { pid::springDecay, 6.5f } } },
            { "UK Sound System", { { pid::sync, 1 }, { pid::syncNote, 9 }, { pid::mode, 7 }, { pid::feedback, 6.0f }, { pid::bass, 2.0f },
                                   { pid::lowCut, 35.0f }, { pid::duck, 3.0f }, { pid::reverb, 3.0f }, { pid::stereoMode, 1 } } },
            { "Sci-Fi Space Sweep", { { pid::delayTime, 120.0f }, { pid::mode, 10 }, { pid::feedback, 6.4f }, { pid::reverb, 6.0f }, { pid::wowFlutter, 6.0f },
                                      { pid::inertia, 7.5f }, { pid::stereoMode, 1 }, { pid::treble, 2.0f } } },
            { "Throw The Snare", { { pid::sync, 1 }, { pid::syncNote, 6 }, { pid::mode, 4 }, { pid::feedback, 5.5f }, { pid::sendMode, 1 },
                                   { pid::reverb, 4.5f }, { pid::stereoMode, 1 } } },
            { "Vocal Dub Tail", { { pid::sync, 1 }, { pid::syncNote, 9 }, { pid::mode, 5 }, { pid::feedback, 5.8f }, { pid::duck, 5.5f },
                                  { pid::lowCut, 150.0f }, { pid::treble, -1.0f }, { pid::reverb, 3.5f } } },
            { "Melodica Chamber", { { pid::sync, 1 }, { pid::syncNote, 7 }, { pid::mode, 9 }, { pid::feedback, 4.8f }, { pid::reverb, 5.5f },
                                    { pid::springDecay, 7.0f }, { pid::stereoMode, 1 } } },
            { "Bass Heavy Echo", { { pid::sync, 1 }, { pid::syncNote, 8 }, { pid::mode, 1 }, { pid::feedback, 5.0f }, { pid::bass, 3.0f },
                                   { pid::treble, -2.5f }, { pid::lowCut, 20.0f }, { pid::stereoMode, 0 }, { pid::reverb, 1.5f } } },
            { "Worn Out Tape", { { pid::delayTime, 340.0f }, { pid::mode, 0 }, { pid::feedback, 5.5f }, { pid::tapeAge, 9.0f }, { pid::wowFlutter, 7.0f },
                                 { pid::tapeSat, 7.5f }, { pid::stereoMode, 0 } } },
            { "Clean Modern Tape", { { pid::delayTime, 375.0f }, { pid::mode, 7 }, { pid::feedback, 4.0f }, { pid::tapeAge, 0.0f }, { pid::wowFlutter, 0.5f },
                                     { pid::tapeSat, 1.5f }, { pid::stereoMode, 2 } } },
            { "Slapback 70s", { { pid::delayTime, 95.0f }, { pid::mode, 0 }, { pid::feedback, 1.5f }, { pid::reverb, 0.0f }, { pid::echoLevel, 7.0f },
                                { pid::stereoMode, 0 } } },
            { "Spring Tank Only", { { pid::mode, 11 }, { pid::reverb, 7.0f }, { pid::springDecay, 7.0f } } },
        };
        return list;
    }

    constexpr const char* presetExt = ".dubpreset";
}

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& s) : apvts (s)
{
    currentName = factory()[0].name;
    refresh();
}

bool PresetManager::isPresetParam (const juce::String& id)
{
    // performance switches are not part of presets
    return id != pid::power && id != pid::bypass && id != pid::kick && id != pid::throwBtn && id != pid::hold;
}

juce::File PresetManager::getUserFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("Dubasco").getChildFile ("Tape Echo Presets");
}

int PresetManager::getNumFactory() const { return (int) factory().size(); }
juce::String PresetManager::getFactoryName (int i) const { return juce::isPositiveAndBelow (i, getNumFactory()) ? factory()[(size_t) i].name : ""; }

void PresetManager::refresh()
{
    entries.clear();
    for (int i = 0; i < getNumFactory(); ++i)
        entries.add ({ factory()[(size_t) i].name, true, {}, i });

    auto folder = getUserFolder();
    if (folder.isDirectory())
    {
        auto files = folder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + presetExt);
        files.sort();
        for (auto& f : files)
            entries.add ({ f.getFileNameWithoutExtension(), false, f, -1 });
    }
    sendChangeMessage();
}

juce::ValueTree PresetManager::captureParams() const
{
    juce::ValueTree t ("DUBASCO_PRESET");
    t.setProperty ("name", currentName, nullptr);
    t.setProperty ("version", 1, nullptr);
    for (auto* p : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (isPresetParam (rp->getParameterID()))
            {
                juce::ValueTree c ("PARAM");
                c.setProperty ("id", rp->getParameterID(), nullptr);
                c.setProperty ("value", rp->convertFrom0to1 (rp->getValue()), nullptr);
                t.appendChild (c, nullptr);
            }
    return t;
}

void PresetManager::applyParams (const juce::ValueTree& t, bool resetMissingToDefault)
{
    for (auto* p : apvts.processor.getParameters())
    {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p);
        if (rp == nullptr || ! isPresetParam (rp->getParameterID()))
            continue;
        const auto child = t.getChildWithProperty ("id", rp->getParameterID());
        float norm;
        if (child.isValid())
            norm = rp->convertTo0to1 ((float) child.getProperty ("value"));
        else if (resetMissingToDefault)
            norm = rp->getDefaultValue();
        else
            continue;
        rp->beginChangeGesture();
        rp->setValueNotifyingHost (norm);
        rp->endChangeGesture();
    }
}

void PresetManager::loadFactory (int i)
{
    if (! juce::isPositiveAndBelow (i, getNumFactory())) return;
    juce::ValueTree t ("DUBASCO_PRESET");
    for (auto& [id, value] : factory()[(size_t) i].values)
    {
        juce::ValueTree c ("PARAM");
        c.setProperty ("id", juce::String (id), nullptr);
        c.setProperty ("value", value, nullptr);
        t.appendChild (c, nullptr);
    }
    applyParams (t, true);
    current = i;
    currentName = factory()[(size_t) i].name;
    sendChangeMessage();
}

bool PresetManager::load (int entryIndex)
{
    if (! juce::isPositiveAndBelow (entryIndex, entries.size())) return false;
    const auto& e = entries.getReference (entryIndex);
    if (e.factory)
    {
        loadFactory (e.factoryIndex);
        current = entryIndex;
        return true;
    }
    if (auto xml = juce::XmlDocument::parse (e.file))
    {
        applyParams (juce::ValueTree::fromXml (*xml), true);
        current = entryIndex;
        currentName = e.name;
        sendChangeMessage();
        return true;
    }
    return false;
}

bool PresetManager::saveUser (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty()) return false;
    auto folder = getUserFolder();
    folder.createDirectory();
    currentName = clean;
    auto xml = captureParams().createXml();
    if (xml == nullptr || ! xml->writeTo (folder.getChildFile (clean + presetExt)))
        return false;
    refresh();
    for (int i = 0; i < entries.size(); ++i)
        if (! entries[i].factory && entries[i].name == clean) current = i;
    sendChangeMessage();
    return true;
}

bool PresetManager::removeUser (int entryIndex)
{
    if (! juce::isPositiveAndBelow (entryIndex, entries.size()) || entries[entryIndex].factory) return false;
    const bool ok = entries[entryIndex].file.deleteFile();
    refresh();
    return ok;
}

void PresetManager::next()     { if (! entries.isEmpty()) load ((current + 1) % entries.size()); }
void PresetManager::previous() { if (! entries.isEmpty()) load ((current + entries.size() - 1) % entries.size()); }

void PresetManager::toggleAB()
{
    auto now = captureParams();
    if (onB) { slotB = now; if (slotA.isValid()) applyParams (slotA, false); }
    else     { slotA = now; if (slotB.isValid()) applyParams (slotB, false); }
    onB = ! onB;
    sendChangeMessage();
}

void PresetManager::copyCurrentToOther()
{
    (onB ? slotA : slotB) = captureParams();
    sendChangeMessage();
}
