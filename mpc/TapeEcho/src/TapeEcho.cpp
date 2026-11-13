// TapeEcho.cpp - Dubasco Tape Echo for Akai MPC standalone (MPC OS 3.10.1+, MPC Plugin SDK).
//
// This is a thin wrapper: the sound comes from exactly the same engine as the
// desktop VST3/AU build (Source/dsp/TapeEchoEngine.*, pure C++, no JUCE), so the
// MPC version sounds identical. Only the parameter plumbing lives here.
//
// Signal flow (see Source/dsp/TapeEchoEngine.h for the full model):
//   in -> input preamp -> record head -> tape loop (3 playback heads at 1:2:3)
//      -> tone chain (bass/treble/tape age/low cut) -> feedback back to the record head
//   heads + spring reverb (3 springs) -> ducking -> mix with dry -> output
//
// Differences to the desktop plugin:
//   * no MIDI learn (MPC effects receive no MIDI; use the Q-Links instead)
//   * Dub Throw / Hold are toggles (press once on, once off), Spring Kick fires on every press
//   * no bypass parameter: the MPC bypasses inserts itself
//   * no user preset files: the MPC saves presets itself; the 16 factory presets are built in

#include "mpc_plugin.h"

// The shared DSP engine from the desktop plugin (single source of truth).
#include "../../../Source/dsp/TapeEchoEngine.cpp"

#include <cmath>

class TapeEcho : public mpc::Plugin
{
public:
    // Parameter order is permanent (saved projects refer to ids, programs to the order).
    // Output first: the MPC nudges parameter 0 on every load, a volume tolerates that.
    enum Params
    {
        kOutput, kInput, kDelay, kFeedback, kEcho, kReverb, kBass, kTreble, kSat,
        kMode, kSync, kNote, kStereo,
        kWow, kAge, kInertia, kDecay, kLowCut, kDuck, kDry, kInputJack, kSendMode,
        kThrow, kHold, kPower, kKick,   // kKick stays last: programs leave it alone
        kNumParams
    };

    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo i;
        i.name = "Tape Echo";
        i.vendor = "Dubasco";
        i.uniqueId = MPC_FOURCC('D', 'b', 'T', 'e');   // == "uid" in plugin.json
        i.version = 1000;
        i.isInstrument = false;
        i.numInputs = 2;
        i.numOutputs = 2;
        i.tailSamples = 0;   // depends on feedback / hold
        return i;
    }

    TapeEcho() : mpc::Plugin (makeInfo())
    {
        using namespace mpc;
        // Output in dB, skewed so 0 dB sits in the middle of the fader.
        Param out = floatParam ("output", "Output", -24.0f, 12.0f, 0.0f, "dB", 1);
        out.skew = 1.709f;
        addParam (out);
        addParam (floatParam ("input",     "Input",       0.0f, 10.0f, 5.0f, "", 1));   // 5 = unity, 10 = +12 dB
        addParam (logParam   ("delayTime", "Delay Time", 50.0f, 600.0f, 320.0f, "ms", 0));
        addParam (floatParam ("feedback",  "Feedback",    0.0f, 10.0f, 4.5f, "", 1));
        addParam (floatParam ("echoLevel", "Echo Level",  0.0f, 10.0f, 6.0f, "", 1));
        addParam (floatParam ("reverb",    "Reverb",      0.0f, 10.0f, 3.5f, "", 1));
        addParam (floatParam ("bass",      "Bass",       -5.0f,  5.0f, 0.0f, "", 1));
        addParam (floatParam ("treble",    "Treble",     -5.0f,  5.0f, 0.0f, "", 1));
        addParam (floatParam ("tapeSat",   "Tape Sat",    0.0f, 10.0f, 4.0f, "", 1));
        // RE-201 mode selector: which playback heads are on, and whether the spring reverb is on
        addParam (choiceParam ("mode", "Echo Mode",
                               { "1 H1", "2 H2", "3 H3", "4 H2+3", "5 H1+R", "6 H2+R", "7 H3+R",
                                 "8 H12+R", "9 H23+R", "10 H13+R", "11 All+R", "12 Rev" }, 4));
        addParam (choiceParam ("sync",     "Tempo Sync", { "Off", "On" }, 0));
        addParam (choiceParam ("syncNote", "Sync Note",
                               { "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D",
                                 "1/4T", "1/4", "1/4D", "1/2T", "1/2", "1/2D", "1/1" }, 6));
        addParam (choiceParam ("stereoMode", "Stereo", { "Mono", "Wide", "Stereo", "PingPong" }, 1));
        addParam (floatParam ("wowFlutter", "Wow Flutter", 0.0f, 10.0f, 3.0f, "", 1));
        addParam (floatParam ("tapeAge",    "Tape Age",    0.0f, 10.0f, 3.0f, "", 1));
        addParam (floatParam ("inertia",    "Inertia",     0.0f, 10.0f, 4.0f, "", 1));
        addParam (floatParam ("springDecay","Spring Decay",0.0f, 10.0f, 5.0f, "", 1));
        addParam (logParam   ("lowCut",     "Low Cut",    20.0f, 500.0f, 60.0f, "Hz", 0));
        addParam (floatParam ("duck",       "Ducking",     0.0f, 10.0f, 0.0f, "", 1));
        addParam (floatParam ("dry",        "Dry Level",   0.0f, 10.0f, 10.0f, "", 1));
        addParam (choiceParam ("inputMode", "Input Jack", { "Mic", "Line" }, 1));
        addParam (choiceParam ("sendMode",  "Send Mode",  { "All", "Throw" }, 0));
        // performance switches (not part of the presets' sound)
        addParam (boolParam ("throw", "Dub Throw",   false));
        addParam (boolParam ("hold",  "Hold",        false));
        addParam (boolParam ("power", "Power",       true));
        addParam (boolParam ("kick",  "Spring Kick", false));
        if (numParams() != kNumParams) mpc::log ("TapeEcho: parameter count mismatch");

        addFactoryPrograms();
    }

    void prepare (double sampleRate, int maxBlockSize) override
    {
        dub::EngineParams p;
        collect (p, 120.0);
        engine.setParams (p);
        engine.prepare (sampleRate, maxBlockSize);   // the only allocation (tape loop + springs)
        lastKick = get (kKick);
        kickPrimed = false;
    }

    void reset() override
    {
        engine.reset();
    }

    void process (mpc::ProcessContext& ctx) override
    {
        const double bpm = ctx.transport.tempoValid ? ctx.transport.bpm : 120.0;
        dub::EngineParams p;
        collect (p, bpm);

        // Spring Kick is a toggle on the MPC: every press (either direction) knocks the tank.
        const float k = get (kKick);
        if (! kickPrimed) { lastKick = k; kickPrimed = true; }
        p.kick = (k != lastKick);
        lastKick = k;

        engine.setParams (p);

        if (ctx.numOutputs < 1 || ctx.numFrames <= 0) return;
        const float* inL = ctx.numInputs > 0 ? ctx.inputs[0] : nullptr;
        const float* inR = ctx.numInputs > 1 ? ctx.inputs[1] : inL;
        float* outL = ctx.outputs[0];
        float* outR = ctx.numOutputs > 1 ? ctx.outputs[1] : ctx.outputs[0];
        if (inL == nullptr) { inL = outL; inR = outL; }   // outputs are cleared: silence in

        if (outR == outL)
        {
            // mono output (the MPC normally gives 2): render stereo into a small stack buffer
            // and fold it down.
            float tmpL[64], tmpR[64];
            for (int s = 0; s < ctx.numFrames; s += 64)
            {
                const int n = std::min (64, ctx.numFrames - s);
                engine.process (inL + s, inR + s, tmpL, tmpR, n);
                for (int i = 0; i < n; ++i) outL[s + i] = 0.5f * (tmpL[i] + tmpR[i]);
            }
        }
        else
        {
            // In place is fine: the engine reads inL[i]/inR[i] before writing outL[i]/outR[i].
            engine.process (inL, inR, outL, outR, ctx.numFrames);
        }
    }

private:
    static double syncNoteBeats (int index)
    {
        static const double beats[] = { 0.125, 1.0 / 6.0, 0.25, 0.375, 1.0 / 3.0, 0.5, 0.75,
                                        2.0 / 3.0, 1.0, 1.5, 4.0 / 3.0, 2.0, 3.0, 4.0 };
        return beats[std::clamp (index, 0, 13)];
    }

    // Same mapping as DubascoProcessor::collectParams in the desktop plugin.
    void collect (dub::EngineParams& e, double bpm) const
    {
        const float in = get (kInput) / 5.0f;
        e.inputGain = in * in;
        e.micMode = getInt (kInputJack) == 0;

        if (getInt (kSync) == 1)
            e.delaySec = (float) (syncNoteBeats (getInt (kNote)) * 60.0 / std::clamp (bpm, 30.0, 300.0));
        else
            e.delaySec = get (kDelay) * 0.001f;

        e.feedback = get (kFeedback) / 10.0f;
        e.bassDb = get (kBass) * 2.0f;
        e.trebleDb = get (kTreble) * 2.0f;
        const float el = get (kEcho) / 10.0f;
        e.echoGain = el * el * 1.6f;
        e.tapeSat = get (kSat) / 10.0f;
        e.power = getBool (kPower);
        e.mode = getInt (kMode) + 1;
        const float rv = get (kReverb) / 10.0f;
        e.reverbGain = rv * rv * 1.4f;
        e.springDecay = get (kDecay) / 10.0f;
        e.kick = false;
        e.wowFlutter = get (kWow) / 10.0f;
        e.tapeAge = get (kAge) / 10.0f;
        e.inertia = get (kInertia) / 10.0f;
        e.stereoMode = getInt (kStereo);
        e.lowCutHz = get (kLowCut);
        e.duck = get (kDuck) / 10.0f;
        const float dr = get (kDry) / 10.0f;
        e.dryGain = dr * dr;
        e.outGain = std::pow (10.0f, get (kOutput) / 20.0f);
        e.throwOn = getBool (kThrow);
        e.hold = getBool (kHold);
        e.throwOnly = getInt (kSendMode) == 1;
        e.bypass = false;
    }

    // The 16 factory presets of the desktop plugin (Source/PresetManager.cpp).
    // Anything a preset does not name is set to its default; performance switches are reset
    // (Throw/Hold off, Power on).
    void addFactoryPrograms()
    {
        struct Preset { const char* name; std::vector<std::pair<int, float>> v; };
        const std::vector<Preset> list = {
            { "Init - Classic Space", {} },
            { "Kingston Skank Echo",  { { kSync, 1 }, { kNote, 6 }, { kMode, 4 }, { kFeedback, 5.6f }, { kReverb, 4.0f },
                                        { kBass, -1.0f }, { kTreble, 1.0f }, { kStereo, 1 } } },
            { "Roots Rockers Quarter",{ { kSync, 1 }, { kNote, 8 }, { kMode, 0 }, { kFeedback, 5.0f }, { kReverb, 2.5f },
                                        { kAge, 4.0f }, { kStereo, 0 } } },
            { "Steppers Ping-Pong",   { { kSync, 1 }, { kNote, 6 }, { kMode, 0 }, { kFeedback, 5.8f }, { kStereo, 3 },
                                        { kReverb, 2.0f }, { kLowCut, 120.0f } } },
            { "Spiral Runaway",       { { kDelay, 260.0f }, { kMode, 3 }, { kFeedback, 7.9f }, { kSat, 7.0f }, { kWow, 4.0f },
                                        { kInertia, 6.5f }, { kReverb, 3.0f }, { kTreble, -1.5f } } },
            { "Hazy Ark Session",     { { kSync, 1 }, { kNote, 9 }, { kMode, 8 }, { kFeedback, 5.2f }, { kAge, 7.0f },
                                        { kWow, 5.0f }, { kTreble, -2.0f }, { kSat, 6.0f }, { kReverb, 5.0f }, { kDecay, 6.5f } } },
            { "UK Sound System",      { { kSync, 1 }, { kNote, 9 }, { kMode, 7 }, { kFeedback, 6.0f }, { kBass, 2.0f },
                                        { kLowCut, 35.0f }, { kDuck, 3.0f }, { kReverb, 3.0f }, { kStereo, 1 } } },
            { "Sci-Fi Space Sweep",   { { kDelay, 120.0f }, { kMode, 10 }, { kFeedback, 6.4f }, { kReverb, 6.0f }, { kWow, 6.0f },
                                        { kInertia, 7.5f }, { kStereo, 1 }, { kTreble, 2.0f } } },
            { "Throw The Snare",      { { kSync, 1 }, { kNote, 6 }, { kMode, 4 }, { kFeedback, 5.5f }, { kSendMode, 1 },
                                        { kReverb, 4.5f }, { kStereo, 1 } } },
            { "Vocal Dub Tail",       { { kSync, 1 }, { kNote, 9 }, { kMode, 5 }, { kFeedback, 5.8f }, { kDuck, 5.5f },
                                        { kLowCut, 150.0f }, { kTreble, -1.0f }, { kReverb, 3.5f } } },
            { "Melodica Chamber",     { { kSync, 1 }, { kNote, 7 }, { kMode, 9 }, { kFeedback, 4.8f }, { kReverb, 5.5f },
                                        { kDecay, 7.0f }, { kStereo, 1 } } },
            { "Bass Heavy Echo",      { { kSync, 1 }, { kNote, 8 }, { kMode, 1 }, { kFeedback, 5.0f }, { kBass, 3.0f },
                                        { kTreble, -2.5f }, { kLowCut, 20.0f }, { kStereo, 0 }, { kReverb, 1.5f } } },
            { "Worn Out Tape",        { { kDelay, 340.0f }, { kMode, 0 }, { kFeedback, 5.5f }, { kAge, 9.0f }, { kWow, 7.0f },
                                        { kSat, 7.5f }, { kStereo, 0 } } },
            { "Clean Modern Tape",    { { kDelay, 375.0f }, { kMode, 7 }, { kFeedback, 4.0f }, { kAge, 0.0f }, { kWow, 0.5f },
                                        { kSat, 1.5f }, { kStereo, 2 } } },
            { "Slapback 70s",         { { kDelay, 95.0f }, { kMode, 0 }, { kFeedback, 1.5f }, { kReverb, 0.0f }, { kEcho, 7.0f },
                                        { kStereo, 0 } } },
            { "Spring Tank Only",     { { kMode, 11 }, { kReverb, 7.0f }, { kDecay, 7.0f } } },
        };
        for (const auto& pr : list)
        {
            // one value per parameter except Spring Kick (last), which a preset never touches,
            // so loading a preset cannot knock the spring tank
            std::vector<float> vals ((size_t) kKick);
            for (int i = 0; i < kKick; ++i) vals[(size_t) i] = param (i).def;
            for (const auto& [idx, val] : pr.v) vals[(size_t) idx] = val;
            addProgram (pr.name, vals);
        }
    }

    dub::TapeEchoEngine engine;
    float lastKick = 0.0f;
    bool kickPrimed = false;
};

MPC_PLUGIN_ENTRY(TapeEcho)
