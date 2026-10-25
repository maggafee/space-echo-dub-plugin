#pragma once
#include <JuceHeader.h>

class DubascoProcessor;

namespace gui
{
class TopBarLookAndFeel : public juce::LookAndFeel_V4
{
public:
    TopBarLookAndFeel();
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
};

// Preset browser, A/B compare, MIDI learn and settings strip above the machine.
class TopBar : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit TopBar (DubascoProcessor&);
    ~TopBar() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void (bool)> onLearnToggled;
    std::function<void (float)> onScaleChosen;
    std::function<void()> onHelp;
    void setLearnActive (bool a);

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;          // MIDI activity LED on the MIDI button
    void showPresetMenu();
    void showSaveDialog();
    void showMidiMenu();
    void showSettingsMenu();
    void refreshLabels();

    DubascoProcessor& proc;
    TopBarLookAndFeel lnf;
    juce::TextButton prev { "<" }, next { ">" }, presetName, save { "SAVE" }, ab { "A" }, copyAB { "A>B" },
        learn { "MIDI LEARN" }, midiMenu { "MIDI" }, settings { "SIZE" }, help { "?" };
    std::unique_ptr<juce::AlertWindow> saveWindow;
    juce::uint32 lastActivity = 0;
    float midiLed = 0;
};
} // namespace gui
