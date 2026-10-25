#pragma once
#include <JuceHeader.h>

namespace pid
{
    // main panel
    inline constexpr const char* input       = "input";
    inline constexpr const char* inputMode   = "inputMode";
    inline constexpr const char* delayTime   = "delayTime";
    inline constexpr const char* feedback    = "feedback";
    inline constexpr const char* bass        = "bass";
    inline constexpr const char* treble      = "treble";
    inline constexpr const char* echoLevel   = "echoLevel";
    inline constexpr const char* tapeSat     = "tapeSat";
    inline constexpr const char* power       = "power";
    // dub section
    inline constexpr const char* mode        = "mode";
    inline constexpr const char* reverb      = "reverb";
    inline constexpr const char* springDecay = "springDecay";
    inline constexpr const char* kick        = "kick";
    inline constexpr const char* wowFlutter  = "wowFlutter";
    inline constexpr const char* tapeAge     = "tapeAge";
    inline constexpr const char* inertia     = "inertia";
    inline constexpr const char* sync        = "sync";
    inline constexpr const char* syncNote    = "syncNote";
    inline constexpr const char* stereoMode  = "stereoMode";
    inline constexpr const char* lowCut      = "lowCut";
    inline constexpr const char* duck        = "duck";
    inline constexpr const char* dry         = "dry";
    inline constexpr const char* output      = "output";
    inline constexpr const char* throwBtn    = "throw";
    inline constexpr const char* hold        = "hold";
    inline constexpr const char* sendMode    = "sendMode";
    inline constexpr const char* bypass      = "bypass";
}

namespace params
{
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    const juce::StringArray& syncNoteNames();
    double syncNoteBeats (int index);      // length in quarter notes

    constexpr float delayMinMs = 50.0f, delayMaxMs = 600.0f;
}
