#include "Parameters.h"

namespace params
{
namespace
{
    using APF = juce::AudioParameterFloat;
    using APB = juce::AudioParameterBool;
    using APC = juce::AudioParameterChoice;
    using Attr = juce::AudioParameterFloatAttributes;

    juce::NormalisableRange<float> logRange (float lo, float hi)
    {
        return { lo, hi,
                 [] (float a, float b, float n) { return a * std::pow (b / a, n); },
                 [] (float a, float b, float v) { return std::log (v / a) / std::log (b / a); },
                 [] (float a, float b, float v) { return juce::jlimit (a, b, v); } };
    }

    std::unique_ptr<APF> knob010 (const char* id, const juce::String& name, float def)
    {
        return std::make_unique<APF> (juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (0.0f, 10.0f, 0.0f), def,
                                      Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1); }));
    }
}

const juce::StringArray& syncNoteNames()
{
    static const juce::StringArray names { "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D", "1/4T", "1/4", "1/4D", "1/2T", "1/2", "1/2D", "1/1" };
    return names;
}

double syncNoteBeats (int index)
{
    static const double beats[] = { 0.125, 1.0 / 6.0, 0.25, 0.375, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0, 1.0, 1.5, 4.0 / 3.0, 2.0, 3.0, 4.0 };
    return beats[juce::jlimit (0, 13, index)];
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;

    // ---- main panel (as on the front of the machine)
    l.add (knob010 (pid::input, "Input", 5.0f));
    l.add (std::make_unique<APC> (juce::ParameterID { pid::inputMode, 1 }, "Input Jack", juce::StringArray { "Mic", "Line" }, 1));
    l.add (std::make_unique<APF> (juce::ParameterID { pid::delayTime, 1 }, "Delay Time", logRange (delayMinMs, delayMaxMs), 320.0f,
                                  Attr().withLabel ("ms").withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " ms"; })
                                        .withValueFromStringFunction ([] (const juce::String& s) { return s.getFloatValue(); })));
    l.add (knob010 (pid::feedback, "Feedback", 4.5f));
    for (auto [id, name] : { std::pair { pid::bass, "Bass" }, std::pair { pid::treble, "Treble" } })
        l.add (std::make_unique<APF> (juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (-5.0f, 5.0f, 0.0f), 0.0f,
                                      Attr().withStringFromValueFunction ([] (float v, int) { return (v > 0.04f ? "+" : "") + juce::String (v, 1); })));
    l.add (knob010 (pid::echoLevel, "Echo Level", 6.0f));
    l.add (knob010 (pid::tapeSat, "Tape Saturation", 4.0f));
    l.add (std::make_unique<APB> (juce::ParameterID { pid::power, 1 }, "Power", true));

    // ---- dub section
    juce::StringArray modes;
    for (int i = 1; i <= 12; ++i) modes.add ("Mode " + juce::String (i));
    l.add (std::make_unique<APC> (juce::ParameterID { pid::mode, 1 }, "Echo Mode", modes, 4));
    l.add (knob010 (pid::reverb, "Reverb Level", 3.5f));
    l.add (knob010 (pid::springDecay, "Spring Decay", 5.0f));
    l.add (std::make_unique<APB> (juce::ParameterID { pid::kick, 1 }, "Spring Kick", false));
    l.add (knob010 (pid::wowFlutter, "Wow & Flutter", 3.0f));
    l.add (knob010 (pid::tapeAge, "Tape Age", 3.0f));
    l.add (knob010 (pid::inertia, "Motor Inertia", 4.0f));
    l.add (std::make_unique<APB> (juce::ParameterID { pid::sync, 1 }, "Tempo Sync", false));
    l.add (std::make_unique<APC> (juce::ParameterID { pid::syncNote, 1 }, "Sync Note", syncNoteNames(), 6));
    l.add (std::make_unique<APC> (juce::ParameterID { pid::stereoMode, 1 }, "Stereo Mode", juce::StringArray { "Mono", "Wide", "Stereo", "Ping-Pong" }, 1));
    l.add (std::make_unique<APF> (juce::ParameterID { pid::lowCut, 1 }, "Low Cut", logRange (20.0f, 500.0f), 60.0f,
                                  Attr().withLabel ("Hz").withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " Hz"; })
                                        .withValueFromStringFunction ([] (const juce::String& s) { return s.getFloatValue(); })));
    l.add (knob010 (pid::duck, "Ducking", 0.0f));
    l.add (knob010 (pid::dry, "Dry Level", 10.0f));
    juce::NormalisableRange<float> outRange (-24.0f, 12.0f);
    outRange.setSkewForCentre (0.0f);
    l.add (std::make_unique<APF> (juce::ParameterID { pid::output, 1 }, "Output", outRange, 0.0f,
                                  Attr().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return (v > 0.04f ? "+" : "") + juce::String (v, 1) + " dB"; })
                                        .withValueFromStringFunction ([] (const juce::String& s) { return s.getFloatValue(); })));
    l.add (std::make_unique<APB> (juce::ParameterID { pid::throwBtn, 1 }, "Dub Throw", false));
    l.add (std::make_unique<APB> (juce::ParameterID { pid::hold, 1 }, "Hold (Infinite)", false));
    l.add (std::make_unique<APC> (juce::ParameterID { pid::sendMode, 1 }, "Send Mode", juce::StringArray { "All", "Throw Only" }, 0));
    l.add (std::make_unique<APB> (juce::ParameterID { pid::bypass, 1 }, "Bypass", false));
    return l;
}
} // namespace params
