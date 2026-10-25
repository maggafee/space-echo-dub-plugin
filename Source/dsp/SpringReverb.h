#pragma once
// Spring tank model in the spirit of Välimäki/Parker/Abel ("Parametric spring
// reverberation effect"): every spring is a feedback delay loop with a long
// cascade of stretched all-pass filters inside, which gives the dispersive
// "chirp"/"drip" of a real spring. Three springs of different length, like the
// RE-201 tank, a little modulation so it never sounds static, and a "kick"
// input that excites the springs like a knock on the tank.

#include "Filters.h"
#include <vector>
#include <array>

namespace dub
{
class SpringReverb
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        // stretch factor puts the dispersion band at ~4.4 kHz (top of a spring's useful range)
        K = std::max (1, (int) std::round (sr / (2.0 * 4400.0)));
        const double lengthsMs[kSprings] = { 31.3, 37.1, 41.9 };
        for (int s = 0; s < kSprings; ++s)
        {
            auto& sp = springs[(size_t) s];
            sp.baseDelay = (float) (lengthsMs[s] * 0.001 * sr);
            sp.line.assign ((size_t) nextPow2 ((int) (sp.baseDelay * 1.1f) + 8), 0.0f);
            sp.mask = (int) sp.line.size() - 1;
            sp.w = 0;
            for (auto& ap : sp.chirp) { ap.buf.assign ((size_t) nextPow2 (K + 1), 0.0f); ap.mask = (int) ap.buf.size() - 1; ap.idx = 0; }
            sp.loopLP.lowPass (sr, 4300.0, 0.6);
            sp.loopHP.setCutoff (sr, 70.0);
            sp.lfoPhase = 0.31f * (float) s;
            sp.lfoInc = (float) ((0.37 + 0.13 * s) / sr);
            sp.fb = 0;
        }
        const double diffMs[2] = { 4.7, 7.3 };
        for (int i = 0; i < 2; ++i)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                auto& d = diff[(size_t) ch][(size_t) i];
                d.len = (int) (diffMs[i] * (ch ? 1.13 : 1.0) * 0.001 * sr);
                d.buf.assign ((size_t) nextPow2 (d.len + 1), 0.0f);
                d.mask = (int) d.buf.size() - 1;
                d.idx = 0;
            }
        }
        inHP.setCutoff (sr, 180.0);
        inLP.lowPass (sr, 5200.0, 0.7);
        outHP.highPass (sr, 90.0, 0.7);
        outHP2.highPass (sr, 90.0, 0.7);
        kickEnv = 0; kickPhase = 0;
        setDecay (0.5f);
    }

    // 0..1 -> roughly 1.2 s .. 5 s RT60
    void setDecay (float d01)
    {
        const double rt60 = 1.2 + 3.8 * std::clamp ((double) d01, 0.0, 1.0);
        for (auto& sp : springs)
            sp.g = (float) std::pow (10.0, -3.0 * (sp.baseDelay / sr) / rt60);
    }

    void kick() noexcept { kickEnv = 1.0f; kickPhase = 0; }

    // mono in, stereo out
    inline void process (float in, float& outL, float& outR) noexcept
    {
        float x = inLP.process (inHP.process (in));

        if (kickEnv > 1.0e-4f)
        {
            // knock on the tank: noisy click plus a low thump
            kickPhase += (float) (2 * kPi * 55.0 / sr);
            x += kickEnv * (0.9f * noise.white() + 0.8f * std::sin (kickPhase));
            kickEnv *= kickDecay();
        }

        float o[kSprings];
        for (int s = 0; s < kSprings; ++s)
        {
            auto& sp = springs[(size_t) s];
            float v = x * 0.6f + sp.fb;
            for (auto& ap : sp.chirp)
                v = ap.process (v, K, kChirpA);
            // write into the transmission line
            sp.line[(size_t) sp.w] = v;
            // modulated read (slightly drifting spring tension)
            sp.lfoPhase += sp.lfoInc; if (sp.lfoPhase >= 1.0f) sp.lfoPhase -= 1.0f;
            const float d = sp.baseDelay * (1.0f + 0.0025f * fastSin01 (sp.lfoPhase));
            float rp = (float) sp.w - d;
            if (rp < 0) rp += (float) sp.line.size();
            const int i0 = (int) rp; const float fr = rp - (float) i0;
            const float a = sp.line[(size_t) (i0 & sp.mask)], b = sp.line[(size_t) ((i0 + 1) & sp.mask)];
            float y = a + fr * (b - a);
            sp.w = (sp.w + 1) & sp.mask;
            y = sp.loopHP.process (sp.loopLP.process (y));
            sp.fb = flushDenormal (y * sp.g);
            o[s] = y;
        }

        float l = o[0] - 0.55f * o[2] + 0.35f * o[1];
        float r = o[1] + 0.55f * o[2] - 0.35f * o[0];
        for (auto& d : diff[0]) l = d.process (l, 0.6f);
        for (auto& d : diff[1]) r = d.process (r, 0.6f);
        outL = outHP.process (l) * 0.7f;
        outR = outHP2.process (r) * 0.7f;
    }

    void reset()
    {
        for (auto& sp : springs)
        {
            std::fill (sp.line.begin(), sp.line.end(), 0.0f);
            for (auto& ap : sp.chirp) std::fill (ap.buf.begin(), ap.buf.end(), 0.0f);
            sp.fb = 0; sp.loopLP.reset(); sp.loopHP.reset();
        }
        for (auto& c : diff) for (auto& d : c) std::fill (d.buf.begin(), d.buf.end(), 0.0f);
        inLP.reset(); outHP.reset(); outHP2.reset(); inHP.reset();
    }

private:
    static constexpr int kSprings = 3;
    static constexpr int kChirpStages = 36;
    static constexpr float kChirpA = 0.62f;

    static int nextPow2 (int n) { int p = 1; while (p < n) p <<= 1; return p; }
    float kickDecay() const noexcept { return (float) std::exp (-1.0 / (0.012 * sr)); }

    // stretched first-order all-pass: y[n] = a*x[n] + x[n-K] - a*y[n-K]
    struct StretchedAP
    {
        std::vector<float> buf;   // stores w[n] = x[n] - a*w[n-K]  (lattice form, one buffer)
        int mask = 0, idx = 0;
        inline float process (float x, int K, float a) noexcept
        {
            const float wd = buf[(size_t) ((idx - K) & mask)];
            const float w = x - a * wd;
            buf[(size_t) idx] = flushDenormal (w);
            idx = (idx + 1) & mask;
            return a * w + wd;
        }
    };

    struct SchroederAP
    {
        std::vector<float> buf; int mask = 0, idx = 0, len = 1;
        inline float process (float x, float g) noexcept
        {
            const float d = buf[(size_t) ((idx - len) & mask)];
            const float w = x + g * d;
            buf[(size_t) idx] = flushDenormal (w);
            idx = (idx + 1) & mask;
            return d - g * w;
        }
    };

    struct Spring
    {
        std::array<StretchedAP, kChirpStages> chirp;
        std::vector<float> line; int mask = 0, w = 0;
        float baseDelay = 1000, g = 0.7f, fb = 0, lfoPhase = 0, lfoInc = 0;
        Biquad loopLP; OnePoleHP loopHP;
    };

    double sr = 44100;
    int K = 5;
    std::array<Spring, kSprings> springs;
    std::array<std::array<SchroederAP, 2>, 2> diff;
    OnePoleHP inHP; Biquad inLP, outHP, outHP2;
    Noise noise { 0xBADC0FFEu };
    float kickEnv = 0, kickPhase = 0;
};
} // namespace dub
