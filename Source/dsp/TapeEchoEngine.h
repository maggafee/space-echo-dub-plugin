#pragma once
// Tape echo engine modelled on the Roland RE-201 Space Echo.
//
// Tape model: the tape loop is a buffer indexed by *tape position*, not by
// time. The motor moves the tape with speed v; the record head writes at the
// current position, the three playback heads read at fixed distances behind it
// (1 : 2 : 3). Changing the repeat rate therefore changes the tape speed, and
// everything on the tape is pitch-shifted exactly like on the real machine
// (with motor inertia), wow & flutter are speed modulations, and switching the
// power off makes the tape run down to a stop.
//
// Pure C++ (no JUCE), so it can be rendered offline in tests.

#include "Filters.h"
#include "SpringReverb.h"
#include <vector>
#include <array>
#include <atomic>

namespace dub
{
struct EngineParams
{
    float inputGain   = 1.0f;     // linear
    bool  micMode     = false;
    float delaySec    = 0.3f;     // head 1 time (already tempo synced if needed)
    float feedback    = 0.45f;    // knob 0..1
    float bassDb      = 0.0f;     // -10..10
    float trebleDb    = 0.0f;     // -10..10
    float echoGain    = 0.7f;     // linear
    float tapeSat     = 0.4f;     // 0..1
    int   mode        = 5;        // 1..12
    float reverbGain  = 0.3f;     // linear
    float springDecay = 0.5f;     // 0..1
    bool  kick        = false;
    float wowFlutter  = 0.3f;     // 0..1
    float tapeAge     = 0.3f;     // 0..1
    float inertia     = 0.4f;     // 0..1
    int   stereoMode  = 1;        // 0 mono, 1 wide, 2 stereo, 3 ping-pong
    float lowCutHz    = 60.0f;
    float duck        = 0.0f;     // 0..1
    float dryGain     = 1.0f;     // linear
    float outGain     = 1.0f;     // linear
    bool  throwOn     = false;
    bool  hold        = false;
    bool  throwOnly   = false;    // send mode: false = all input to echo, true = only while THROW pressed
    bool  power       = true;
    bool  bypass      = false;
};

class TapeEchoEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    void setParams (const EngineParams& p) { params = p; }

    // in/out may alias. inR may equal inL for mono hosts.
    void process (const float* inL, const float* inR, float* outL, float* outR, int numSamples);

    // --- state for the GUI (written on the audio thread)
    std::atomic<float> meterIn { 0 }, meterOut { 0 }, meterEcho { 0 };
    std::atomic<float> reelSpeed { 0 };      // tape speed relative to the "300 ms" reference, 0 when stopped
    std::atomic<float> headDelayMs { 0 };    // current effective head 1 time

    static void modeHeads (int mode, bool& h1, bool& h2, bool& h3, bool& rev) noexcept;

private:
    struct Tape
    {
        std::vector<float> buf;
        int64_t mask = 0;
        double P = 0;
        float prevIn = 0;

        void init (int size) { buf.assign ((size_t) size, 0.0f); mask = size - 1; P = (double) size; prevIn = 0; }
        void clear() { std::fill (buf.begin(), buf.end(), 0.0f); prevIn = 0; }

        inline void write (float x, double v) noexcept
        {
            const double P0 = P, P1 = P + v;
            if (v > 1.0e-9)
            {
                const double inv = 1.0 / v;
                for (int64_t j = (int64_t) std::floor (P0) + 1; j <= (int64_t) std::floor (P1); ++j)
                {
                    const float fr = (float) (((double) j - P0) * inv);
                    buf[(size_t) (j & mask)] = prevIn + (x - prevIn) * fr;
                }
            }
            P = P1;
            const double wrap = (double) (mask + 1) * 4096.0;
            if (P > wrap) P -= wrap - (double) (mask + 1) * 8.0;   // subtract a multiple of the size
            prevIn = x;
        }

        inline float read (double dist) const noexcept
        {
            const double pos = P - dist;
            const double fl = std::floor (pos);
            const int64_t i = (int64_t) fl;
            const float t = (float) (pos - fl);
            const float xm1 = buf[(size_t) ((i - 1) & mask)], x0 = buf[(size_t) (i & mask)];
            const float x1 = buf[(size_t) ((i + 1) & mask)], x2 = buf[(size_t) ((i + 2) & mask)];
            // 4-point Hermite
            const float c1 = 0.5f * (x1 - xm1);
            const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
            const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            return ((c3 * t + c2) * t + c1) * t + x0;
        }
    };

    // playback / tone chain (shared design for the loop and for the outputs)
    struct ToneChain
    {
        std::array<Biquad, 8> bq;
        void reset() { for (auto& b : bq) b.reset(); }
        inline float process (float x) noexcept { for (auto& b : bq) x = b.process (x); return x; }
        void copyCoeffs (const ToneChain& o) { for (size_t i = 0; i < bq.size(); ++i) { bq[i].b0 = o.bq[i].b0; bq[i].b1 = o.bq[i].b1; bq[i].b2 = o.bq[i].b2; bq[i].a1 = o.bq[i].a1; bq[i].a2 = o.bq[i].a2; } }
    };

    struct Channel
    {
        Tape tape;
        Biquad recLP, preEmph;
        ToneChain loop;
        float fbIn = 0, echo = 0;
        // dropouts
        float dropGain = 1, dropTarget = 1; int dropCount = 0;
        // wow/flutter
        float flutPhase = 0, flut2Phase = 0, drift = 0, driftTarget = 0;
        Noise noise;
    };

    void updateCoefficients();
    float wowFlutterMod (Channel& c, float wowPhaseOffset) noexcept;

    EngineParams params;
    double sr = 44100;
    int subBlock = 32;
    double D1 = 11025;           // tape distance record head -> head 1 (in tape samples)

    std::array<Channel, 2> ch;
    ToneChain outChainL, outChainR;
    SpringReverb spring;

    // smoothed state
    double logV = 0;             // log of motor speed
    double motorV = 1;
    float powerSpeed = 1;        // 0..1 motor run-up / run-down
    float wowPhase = 0;
    Smoother sendS, fbS, echoS, revS, dryS, outS, bypassS, powerWetS, revWidthS, duckS, inGainS;
    std::array<Smoother, 3> headG;
    // stereo routing (smoothed)
    Smoother cAL, cAR, cBL, cBR, selfFb, crossFb;
    std::array<Smoother, 3> wLA, wRA, wLB, wRB;
    float duckEnv = 0, duckAtk = 0, duckRel = 0;
    float meterInAcc = 0, meterOutAcc = 0, meterEchoAcc = 0, meterCoeff = 0;
    bool lastKick = false;
    float hissLevel = 0, dropRate = 0, wfDepth = 0;

    // cached values for coefficient updates
    float cBass = 99, cTreble = 99, cAge = -1, cLowCut = -1, cDecay = -1;
    double lastRecFc = -1;
};
} // namespace dub
