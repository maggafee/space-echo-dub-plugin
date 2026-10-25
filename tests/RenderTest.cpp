// Offline renderer / sanity test for the tape echo engine (no JUCE needed).
// Usage: DubascoRender <outdir>
// Writes a few WAV demos and prints measurements (echo timing, stability).

#include "dsp/TapeEchoEngine.h"
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <vector>
#include <string>
#include <functional>

using namespace dub;

static void writeWav (const std::string& path, const std::vector<float>& L, const std::vector<float>& R, int sr)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) { std::printf ("cannot write %s\n", path.c_str()); return; }
    const uint32_t n = (uint32_t) L.size(), dataBytes = n * 4;
    auto w32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto w16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); w32 (36 + dataBytes); std::fwrite ("WAVEfmt ", 1, 8, f);
    w32 (16); w16 (1); w16 (2); w32 ((uint32_t) sr); w32 ((uint32_t) sr * 4); w16 (4); w16 (16);
    std::fwrite ("data", 1, 4, f); w32 (dataBytes);
    for (uint32_t i = 0; i < n; ++i)
        for (float v : { L[i], R[i] })
        {
            const int16_t s = (int16_t) std::lround (std::clamp (v, -1.0f, 1.0f) * 32767.0f);
            std::fwrite (&s, 2, 1, f);
        }
    std::fclose (f);
}

struct Result { float peak = 0; bool finite = true; };

static Result render (TapeEchoEngine& e, std::vector<float>& L, std::vector<float>& R,
                      const std::function<void (int, EngineParams&)>& automate, EngineParams base, int block = 256)
{
    Result r;
    const int n = (int) L.size();
    for (int s = 0; s < n; s += block)
    {
        const int m = std::min (block, n - s);
        EngineParams p = base;
        if (automate) automate (s, p);
        e.setParams (p);
        e.process (L.data() + s, R.data() + s, L.data() + s, R.data() + s, m);
    }
    for (int i = 0; i < n; ++i)
    {
        if (! std::isfinite (L[i]) || ! std::isfinite (R[i])) r.finite = false;
        r.peak = std::max (r.peak, std::max (std::abs (L[i]), std::abs (R[i])));
    }
    return r;
}

// simple dub groove: offbeat skank chords + rimshot snare on 3, 75 bpm, 2 bars, then silence
static void makeGroove (std::vector<float>& L, std::vector<float>& R, int sr, double seconds, double playSeconds)
{
    const int n = (int) (seconds * sr);
    L.assign ((size_t) n, 0.0f); R.assign ((size_t) n, 0.0f);
    const double bpm = 75, beat = 60.0 / bpm;
    uint32_t rnd = 1;
    auto noise = [&] { rnd ^= rnd << 13; rnd ^= rnd >> 17; rnd ^= rnd << 5; return (float) ((int32_t) rnd) / 2147483648.0f; };
    const double chord[3] = { 220.0 * std::pow (2, 3 / 12.0), 220.0 * std::pow (2, 7 / 12.0), 220.0 * std::pow (2, 10 / 12.0) };
    for (double t0 = 0; t0 < playSeconds; t0 += beat)
    {
        // skank on the "and"
        const int s0 = (int) ((t0 + beat * 0.5) * sr);
        for (int i = 0; i < (int) (0.09 * sr) && s0 + i < n; ++i)
        {
            const double t = i / (double) sr, env = std::exp (-t * 30);
            double v = 0;
            for (double f : chord) v += std::sin (2 * kPi * f * t) + 0.3 * std::sin (4 * kPi * f * t);
            L[(size_t) (s0 + i)] += (float) (0.12 * env * v);
            R[(size_t) (s0 + i)] += (float) (0.12 * env * v);
        }
        // snare/rim on beat 3 of each bar
        const int beatIdx = (int) std::lround (t0 / beat);
        if (beatIdx % 4 == 2)
        {
            const int s1 = (int) (t0 * sr);
            for (int i = 0; i < (int) (0.15 * sr) && s1 + i < n; ++i)
            {
                const double t = i / (double) sr;
                const float v = (float) (0.5 * std::exp (-t * 28) * (0.7 * noise() + 0.6 * std::sin (2 * kPi * 330 * t)));
                L[(size_t) (s1 + i)] += v; R[(size_t) (s1 + i)] += v;
            }
        }
    }
}

int main (int argc, char** argv)
{
    const std::string out = argc > 1 ? argv[1] : ".";
    const int sr = 48000;
    int failures = 0;

    // ---- 1: impulse timing, mode 1, no wow
    {
        TapeEchoEngine e; EngineParams p;
        p.delaySec = 0.3f; p.mode = 1; p.feedback = 0.3f; p.wowFlutter = 0; p.tapeAge = 0; p.dryGain = 0; p.reverbGain = 0; p.stereoMode = 0;
        p.inputGain = 1.0f;
        e.setParams (p); e.prepare (sr, 512);
        std::vector<float> L ((size_t) sr * 2, 0.0f), R;
        L[1000] = 0.5f; R = L;
        auto r = render (e, L, R, nullptr, p);
        // first peak after the impulse
        int best = 0; float bv = 0;
        for (int i = 1100; i < (int) (0.5 * sr) + 1000; ++i) if (std::abs (L[(size_t) i]) > bv) { bv = std::abs (L[(size_t) i]); best = i; }
        const double ms = (best - 1000) * 1000.0 / sr;
        std::printf ("[timing] first echo at %.2f ms (expected ~300), peak %.3f finite %d\n", ms, bv, (int) r.finite);
        if (std::abs (ms - 300.0) > 3.0 || ! r.finite) ++failures;

        int best2 = 0; float bv2 = 0;
        for (int i = best + 2000; i < best + (int) (0.5 * sr); ++i) if (std::abs (L[(size_t) i]) > bv2) { bv2 = std::abs (L[(size_t) i]); best2 = i; }
        std::printf ("[timing] second repeat at %.2f ms, level ratio %.3f\n", (best2 - 1000) * 1000.0 / sr, bv2 / bv);
    }

    // ---- 2: heads 2+3 (mode 4)
    {
        TapeEchoEngine e; EngineParams p;
        p.delaySec = 0.2f; p.mode = 4; p.feedback = 0.0f; p.wowFlutter = 0; p.tapeAge = 0; p.dryGain = 0; p.reverbGain = 0; p.stereoMode = 0;
        e.setParams (p); e.prepare (sr, 512);
        std::vector<float> L ((size_t) sr, 0.0f), R;
        L[0] = 0.5f; R = L;
        render (e, L, R, nullptr, p);
        std::vector<int> peaks;
        for (int i = 2; i < sr - 2; ++i)
            if (std::abs (L[(size_t) i]) > 0.05f && std::abs (L[(size_t) i]) >= std::abs (L[(size_t) i - 1]) && std::abs (L[(size_t) i]) > std::abs (L[(size_t) i + 1]))
                { peaks.push_back (i); i += 2000; }
        std::printf ("[mode4] echo peaks at:");
        for (int pk : peaks) std::printf (" %.1fms", pk * 1000.0 / sr);
        std::printf ("  (expected 400, 600)\n");
    }

    // ---- 3: self oscillation must stay bounded
    {
        TapeEchoEngine e; EngineParams p;
        p.delaySec = 0.25f; p.mode = 11; p.feedback = 1.0f; p.tapeSat = 1.0f; p.bassDb = 10; p.dryGain = 1; p.echoGain = 2.0f; p.reverbGain = 1.5f;
        e.setParams (p); e.prepare (sr, 512);
        std::vector<float> L, R;
        makeGroove (L, R, sr, 20.0, 4.0);
        auto r = render (e, L, R, nullptr, p);
        std::printf ("[runaway] peak %.3f finite %d (must be <= 1.0)\n", r.peak, (int) r.finite);
        if (! r.finite || r.peak > 1.0001f) ++failures;
        writeWav (out + "/demo_runaway_feedback.wav", L, R, sr);
    }

    // ---- 4: dub demos
    struct Demo { const char* name; std::function<void (EngineParams&)> set; std::function<void (int, EngineParams&)> autom; };
    const double beat = 60.0 / 75.0;
    std::vector<Demo> demos = {
        { "demo_classic_dub", [&] (EngineParams& p) { p.delaySec = (float) (beat * 0.75); p.mode = 5; p.feedback = 0.62f; p.reverbGain = 0.45f; }, nullptr },
        { "demo_ping_pong", [&] (EngineParams& p) { p.delaySec = (float) (beat * 0.5); p.mode = 1; p.feedback = 0.6f; p.stereoMode = 3; p.reverbGain = 0.2f; }, nullptr },
        { "demo_multi_head_wide", [&] (EngineParams& p) { p.delaySec = (float) (beat / 3.0); p.mode = 11; p.feedback = 0.55f; p.stereoMode = 1; p.reverbGain = 0.35f; p.wowFlutter = 0.45f; p.tapeAge = 0.5f; }, nullptr },
        { "demo_delay_sweep", [&] (EngineParams& p) { p.delaySec = (float) (beat * 0.75); p.mode = 1; p.feedback = 0.75f; p.inertia = 0.55f; },
          [&] (int s, EngineParams& p) { const double t = s / (double) sr; if (t > 5.0 && t < 9.0) p.delaySec = 0.09f; } },
        { "demo_tape_stop", [&] (EngineParams& p) { p.delaySec = (float) (beat * 0.75); p.mode = 9; p.feedback = 0.7f; p.reverbGain = 0.3f; },
          [&] (int s, EngineParams& p) { const double t = s / (double) sr; p.power = ! (t > 6.0 && t < 9.0); } },
        { "demo_throw_hold", [&] (EngineParams& p) { p.delaySec = (float) (beat * 0.75); p.mode = 5; p.feedback = 0.5f; p.throwOnly = true; p.reverbGain = 0.35f; },
          [&] (int s, EngineParams& p) { const double t = s / (double) sr; p.throwOn = (t > 2.3 && t < 2.6) || (t > 4.7 && t < 5.0); p.hold = t > 6.5 && t < 10.0; p.kick = t > 10.5 && t < 10.6; } },
    };
    for (auto& d : demos)
    {
        TapeEchoEngine e; EngineParams p;
        p.tapeAge = 0.3f; p.wowFlutter = 0.3f; p.inertia = 0.4f; p.stereoMode = 1; p.tapeSat = 0.45f;
        d.set (p);
        e.setParams (p); e.prepare (sr, 512);
        std::vector<float> L, R;
        makeGroove (L, R, sr, 14.0, 6.2);
        auto r = render (e, L, R, d.autom, p, 333);
        std::printf ("[%s] peak %.3f finite %d\n", d.name, r.peak, (int) r.finite);
        if (! r.finite) ++failures;
        writeWav (out + "/" + d.name + ".wav", L, R, sr);
    }

    // ---- 5: odd sample rates & block sizes
    for (int rate : { 44100, 88200, 192000 })
    {
        TapeEchoEngine e; EngineParams p; p.mode = 11; p.feedback = 0.8f; p.delaySec = 0.6f;
        e.setParams (p); e.prepare (rate, 4096);
        std::vector<float> L, R; makeGroove (L, R, rate, 4.0, 2.0);
        auto r = render (e, L, R, nullptr, p, 1);
        std::printf ("[sr %d, block 1] peak %.3f finite %d\n", rate, r.peak, (int) r.finite);
        if (! r.finite) ++failures;
    }

    std::printf (failures ? "FAILURES: %d\n" : "ALL OK\n", failures);
    return failures ? 1 : 0;
}
