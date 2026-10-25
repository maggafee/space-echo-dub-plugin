// Renders the editor to PNG files without a host (used to check the GUI layout).
// Usage: DubascoSnapshot <out.png> [scale] [learn]
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String out = argc > 1 ? argv[1] : "snapshot.png";
    const float scale = argc > 2 ? juce::String (argv[2]).getFloatValue() : 1.0f;

    DubascoProcessor proc;
    proc.editorScale.store (scale);
    proc.prepareToPlay (48000, 512);
    if (argc > 3 && juce::String (argv[3]) == "preset")
        proc.presets.loadFactory (5);
    if (argc > 3 && juce::String (argv[3]) == "off")
        proc.apvts.getParameter (pid::power)->setValueNotifyingHost (0.0f);
    if (argc > 3 && juce::String (argv[3]) == "miditest")
    {
        auto& ml = proc.midiLearn;
        juce::AudioBuffer<float> b (2, 256); b.clear();
        auto send = [&] (juce::MidiMessage m) { juce::MidiBuffer mb; mb.addEvent (m, 0); proc.processBlock (b, mb); };
        ml.clearAll();
        ml.arm (ml.indexOf (pid::feedback));
        send (juce::MidiMessage::controllerEvent (1, 21, 64));      // learn
        send (juce::MidiMessage::controllerEvent (1, 21, 127));     // move
        float v = -1; const bool ov = ml.getOverride (ml.indexOf (pid::feedback), v);
        std::printf ("feedback mapping: '%s', override %d value %.2f (expect 10.00)\n", ml.describe (ml.indexOf (pid::feedback)).toRawUTF8(), (int) ov, v);
        ml.arm (ml.indexOf (pid::throwBtn));
        send (juce::MidiMessage::noteOn (10, 36, (juce::uint8) 100)); // learn pad
        send (juce::MidiMessage::noteOn (10, 36, (juce::uint8) 100));
        ml.getOverride (ml.indexOf (pid::throwBtn), v);
        std::printf ("throw mapping: '%s', after note on %.0f", ml.describe (ml.indexOf (pid::throwBtn)).toRawUTF8(), v);
        send (juce::MidiMessage::noteOff (10, 36));
        ml.getOverride (ml.indexOf (pid::throwBtn), v);
        std::printf (", after note off %.0f (expect 1 then 0)\n", v);
        ml.arm (ml.indexOf (pid::mode));
        send (juce::MidiMessage::controllerEvent (2, 1, 0));
        send (juce::MidiMessage::controllerEvent (2, 1, 127));
        ml.getOverride (ml.indexOf (pid::mode), v);
        std::printf ("mode mapping: '%s', CC127 -> index %.0f (expect 11)\n", ml.describe (ml.indexOf (pid::mode)).toRawUTF8(), v);
        // direct device input: learning, and the host echo of the same controller is ignored
        ml.arm (ml.indexOf (pid::echoLevel));
        ml.injectDeviceMessage (juce::MidiMessage::controllerEvent (1, 30, 10));
        const auto a0 = ml.getActivityCounter();
        ml.injectDeviceMessage (juce::MidiMessage::controllerEvent (1, 30, 127));
        send (juce::MidiMessage::controllerEvent (1, 30, 127));   // same event via the host
        ml.getOverride (ml.indexOf (pid::echoLevel), v);
        std::printf ("device mapping: '%s', value %.1f (expect 10.0), counted %u (expect 1)\n",
                     ml.describe (ml.indexOf (pid::echoLevel)).toRawUTF8(), v, (unsigned) (ml.getActivityCounter() - a0));
        ml.arm (ml.indexOf (pid::hold));
        ml.injectDeviceMessage (juce::MidiMessage::noteOn (1, 40, (juce::uint8) 100));
        juce::Thread::sleep (200);
        ml.injectDeviceMessage (juce::MidiMessage::noteOn (1, 40, (juce::uint8) 100));
        send (juce::MidiMessage::noteOn (1, 40, (juce::uint8) 100));   // echo: must not toggle back
        ml.getOverride (ml.indexOf (pid::hold), v);
        std::printf ("hold toggled by device note, host echo ignored: %.0f (expect 1)\n", v);
        std::printf ("last message: %s\n", ml.getLastMessageText().toRawUTF8());
        juce::MemoryBlock st; proc.getStateInformation (st);
        DubascoProcessor p2; p2.setStateInformation (st.getData(), (int) st.getSize());
        std::printf ("restored mapping in new instance: '%s'\n", p2.midiLearn.describe (p2.midiLearn.indexOf (pid::feedback)).toRawUTF8());
        return 0;
    }
    {
        // run some audio so meters / reels have state
        juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m;
        for (int i = 0; i < 200; ++i)
        {
            for (int c = 0; c < 2; ++c) for (int s = 0; s < 512; ++s) b.setSample (c, s, 0.3f * std::sin ((float) (i * 512 + s) * 0.05f));
            proc.processBlock (b, m);
        }
    }
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());

    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    juce::File f (juce::File::getCurrentWorkingDirectory().getChildFile (out));
    f.deleteFile();
    juce::FileOutputStream os (f);
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::printf ("wrote %s (%dx%d)\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    ed.reset();
    return 0;
}
