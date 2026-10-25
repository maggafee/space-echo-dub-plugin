#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "gui/Controls.h"
#include "gui/TopBar.h"

class HelpOverlay;

class DubascoEditor : public juce::AudioProcessorEditor,
                      private juce::ChangeListener
{
public:
    explicit DubascoEditor (DubascoProcessor&);
    ~DubascoEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void tick (double timestampSec);
    void showParamMenu (gui::ParamControl&);
    void showValue (gui::ParamControl&, bool show);
    void place (juce::Component& c, juce::Rectangle<float> designRect);
    void setScale (float s);
    template <typename T> T* registerControl (std::unique_ptr<T> c, juce::Rectangle<float> rect);

    DubascoProcessor& proc;
    gui::TopBar topBar;
    gui::ScaledImage background;

    std::vector<std::unique_ptr<juce::Component>> owned;
    std::vector<std::pair<juce::Component*, juce::Rectangle<float>>> placements;
    std::vector<gui::ParamControl*> paramControls;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAttachments;

    gui::FilmKnob* delayKnob = nullptr;
    gui::Reel* reelL = nullptr; gui::Reel* reelR = nullptr;
    gui::VUMeter* vu = nullptr;
    gui::Lamp* powerLamp = nullptr;
    std::array<gui::Lamp*, 4> headLamps {};
    std::vector<gui::LeverSwitch*> levers;
    std::vector<gui::Lamp*> lamps;
    gui::BatToggle* powerToggle = nullptr;
    gui::Readout* tapeReadout = nullptr; gui::Readout* noteReadout = nullptr;

    gui::ValueBubble bubble;
    gui::MidiLearnOverlay learnOverlay;
    std::unique_ptr<HelpOverlay> help;
    juce::TooltipWindow tooltips { this, 600 };

    std::unique_ptr<juce::VBlankAttachment> vblank;
    double lastTick = 0, readoutTimer = 0;
    float flutterVis = 0;
    double motorLog = 0.0, motorRamp = 1.0;
    bool sizeInitialised = false;
    gui::ParamControl* bubbleOwner = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DubascoEditor)
};
