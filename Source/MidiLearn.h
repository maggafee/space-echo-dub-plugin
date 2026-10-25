#pragma once
#include <JuceHeader.h>
#include <array>

// MIDI learn for every parameter.
//  - The editor arms a parameter (right-click > MIDI Learn, or the LEARN mode in the top bar).
//  - The next CC or note that arrives on the audio thread is bound to it.
//  - Incoming mapped messages are applied immediately to the DSP (via overrides)
//    and forwarded to the host/GUI on the message thread.
// Mappings are stored in the plugin state and can be saved as the default map for new instances.
class MidiLearn : private juce::Timer,
                  public juce::ChangeBroadcaster
{
public:
    enum class Kind { continuous, toggle, momentary, stepped };

    MidiLearn (juce::AudioProcessorValueTreeState& state, double& sampleRateRef);
    ~MidiLearn() override;

    // --- audio thread
    void processMidi (const juce::MidiBuffer& midi, int numSamples);
    // returns true and the plain value if a recent MIDI message overrides the parameter
    bool getOverride (int paramIndex, float& plainValue) const noexcept;

    // --- message thread
    int indexOf (const juce::String& paramId) const { return paramIds.indexOf (paramId); }
    const juce::StringArray& getParamIds() const { return paramIds; }
    void arm (int paramIndex);                 // -1 cancels
    int getArmed() const noexcept { return armed.load(); }
    void clear (int paramIndex);
    void clearAll();
    juce::String describe (int paramIndex) const;   // "CC 21 · Ch 1" or "" if unmapped
    bool isMapped (int paramIndex) const { return describe (paramIndex).isNotEmpty(); }

    juce::ValueTree toValueTree() const;
    void fromValueTree (const juce::ValueTree&);
    bool saveAsDefault() const;
    bool loadDefault();
    static juce::File getDefaultFile();

private:
    void timerCallback() override;
    void apply (int paramIndex, float normValue);
    static int key (int channel, int number) noexcept { return ((channel & 15) << 7) | (number & 127); }

    juce::AudioProcessorValueTreeState& apvts;
    double& sampleRate;
    juce::StringArray paramIds;
    std::vector<juce::RangedAudioParameter*> params;
    std::vector<Kind> kinds;

    std::array<std::atomic<int>, 2048> ccMap, noteMap;   // value = param index, -1 = none
    std::atomic<int> armed { -1 };
    std::atomic<bool> mappingChanged { false };

    struct Event { int param; float norm; };
    juce::AbstractFifo fifo { 512 };
    std::array<Event, 512> events;

    struct Override { std::atomic<float> plain { 0 }; std::atomic<int> samplesLeft { 0 }; };
    std::vector<std::unique_ptr<Override>> overrides;
};
