#pragma once
#include <JuceHeader.h>

// Factory presets (compiled in) + user presets (XML files in ~/Documents/Dubasco/Tape Echo Presets)
// plus an A/B compare slot.
class PresetManager : public juce::ChangeBroadcaster
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState&);

    struct Entry { juce::String name; bool factory = true; juce::File file; int factoryIndex = -1; };

    void refresh();
    const juce::Array<Entry>& getEntries() const { return entries; }
    int getNumFactory() const;
    juce::String getFactoryName (int i) const;

    bool load (int entryIndex);
    void loadFactory (int factoryIndex);
    bool saveUser (const juce::String& name);
    bool removeUser (int entryIndex);
    void next();
    void previous();

    int getCurrentIndex() const { return current; }
    juce::String getCurrentName() const { return currentName; }
    void setCurrentName (const juce::String& n) { currentName = n; sendChangeMessage(); }

    // A/B compare
    void toggleAB();
    void copyCurrentToOther();
    bool isOnB() const { return onB; }

    static juce::File getUserFolder();
    static bool isPresetParam (const juce::String& id);

private:
    juce::ValueTree captureParams() const;
    void applyParams (const juce::ValueTree& t, bool resetMissingToDefault);

    juce::AudioProcessorValueTreeState& apvts;
    juce::Array<Entry> entries;
    int current = 0;
    juce::String currentName { "Kingston Skank Echo" };
    juce::ValueTree slotA, slotB;
    bool onB = false;
};
