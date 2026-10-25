#pragma once
#include <JuceHeader.h>
#include <array>

// MIDI learn for every parameter.
//  - The editor arms a parameter (right-click > MIDI Learn, or the LEARN mode in the top bar).
//  - The next CC or note that arrives on the audio thread is bound to it.
//  - Incoming mapped messages are applied immediately to the DSP (via overrides)
//    and forwarded to the host/GUI on the message thread.
// Mappings are stored in the plugin state and can be saved as the default map for new instances.
//  - Besides the MIDI the host routes to the plugin, controllers can be received directly from the
//    MIDI devices of the computer ("direct input"). Hosts often send no MIDI to an audio effect
//    (Logic: audio tracks, Ableton: without a routed MIDI track), so MIDI learn works out of the box.
class MidiLearn : private juce::Timer,
                  private juce::MidiInputCallback,
                  public juce::ChangeBroadcaster
{
public:
    enum class Kind { continuous, toggle, momentary, stepped };
    enum Source { fromHost = 0, fromDevice = 1 };

    MidiLearn (juce::AudioProcessorValueTreeState& state, double& sampleRateRef);
    ~MidiLearn() override;

    // --- audio thread
    void processMidi (const juce::MidiBuffer& midi, int numSamples);

    // --- direct device input (message thread)
    void setDirectInputAvailable (bool available);      // false in the standalone app (it has its own MIDI input)
    void setDirectInputEnabled (bool enabled);           // user setting, shared by all instances
    bool isDirectInputEnabled() const noexcept { return directEnabled; }
    bool isDirectInputAvailable() const noexcept { return directAvailable; }
    juce::StringArray getOpenDeviceNames() const;
    juce::String getLastMessageText() const;             // last received CC / note, for the MIDI menu
    juce::uint32 getActivityCounter() const noexcept { return activity.load (std::memory_order_relaxed); }
    void injectDeviceMessage (const juce::MidiMessage& m) { handle (m, fromDevice); }   // tests
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
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void handle (const juce::MidiMessage&, Source);
    bool isDuplicate (int slot, int value, Source) noexcept;
    void updateDevices();
    void apply (int paramIndex, float normValue);
    static juce::File getSettingsFile();
    static int key (int channel, int number) noexcept { return ((channel & 15) << 7) | (number & 127); }

    juce::AudioProcessorValueTreeState& apvts;
    double& sampleRate;
    juce::StringArray paramIds;
    std::vector<juce::RangedAudioParameter*> params;
    std::vector<Kind> kinds;

    std::array<std::atomic<int>, 2048> ccMap, noteMap;   // value = param index, -1 = none
    std::atomic<int> armed { -1 };
    std::atomic<bool> mappingChanged { false };

    // one single-producer fifo per source (audio thread / MIDI device thread)
    struct Event { int param; float norm; };
    struct Queue { juce::AbstractFifo fifo { 512 }; std::array<Event, 512> events; };
    std::array<Queue, 2> queues;

    // the same controller can arrive from the host and directly from the device: drop the echo
    std::array<std::atomic<juce::uint64>, 3 * 2048> lastSeen {};   // (time ms << 9) | (source << 8) | value
    std::atomic<juce::uint32> activity { 0 };
    std::atomic<juce::uint32> lastMsg { 0 };                         // packed: valid, source, type, ch, num, value

    bool directAvailable = true, directEnabled = true;
    std::vector<std::unique_ptr<juce::MidiInput>> devices;
    juce::CriticalSection deviceLock;
    int rescanCountdown = 0;

    struct Override { std::atomic<float> plain { 0 }; std::atomic<int> samplesLeft { 0 }; };
    std::vector<std::unique_ptr<Override>> overrides;
};
