#include "TapeEchoEngine.h"
#include <cmath>

namespace dub
{
namespace
{
    constexpr double kRefDelay = 0.25;      // head 1 time at tape speed 1.0 (seconds)
    constexpr double kMaxDelay = 2.0;       // longest head 1 time we allow
    constexpr double kMinDelay = 0.025;

    // rational tanh approximation (accurate to ~1e-4, monotonic, clamps at +-1)
    inline float fastTanh (float x) noexcept
    {
        if (x > 4.97f) return 1.0f;
        if (x < -4.97f) return -1.0f;
        const float x2 = x * x;
        const float a = x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)));
        const float b = 135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f));
        return std::clamp (a / b, -1.0f, 1.0f);
    }

    inline float tapeSaturate (float x, float drive, float tb) noexcept
    {
        // asymmetric tanh with unity small-signal gain: soft compression + a touch of
        // even harmonics like real tape. tb = tanh (drive * bias), precomputed.
        constexpr float b = 0.08f;
        return (fastTanh (drive * (x + b)) - tb) / (drive * (1.0f - tb * tb));
    }

    inline float softClipOut (float x) noexcept
    {
        const float a = std::abs (x);
        if (a < 0.9f) return x;
        const float y = 0.9f + 0.1f * std::tanh ((a - 0.9f) * 10.0f);
        return x < 0 ? -y : y;
    }

    inline void equalPowerPan (float p, float& l, float& r) noexcept
    {
        const float a = (p + 1.0f) * 0.25f * (float) kPi;
        l = std::cos (a) * 1.41421356f;
        r = std::sin (a) * 1.41421356f;
    }

    int nextPow2 (int n) { int p = 1; while (p < n) p <<= 1; return p; }
}

void TapeEchoEngine::modeHeads (int mode, bool& h1, bool& h2, bool& h3, bool& rev) noexcept
{
    // RE-201 mode selector: 1-4 echo only, 5-11 echo + reverb, 12 reverb only
    static constexpr unsigned char table[12][4] = {
        { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 1, 1, 0 },
        { 1, 0, 0, 1 }, { 0, 1, 0, 1 }, { 0, 0, 1, 1 }, { 1, 1, 0, 1 },
        { 0, 1, 1, 1 }, { 1, 0, 1, 1 }, { 1, 1, 1, 1 }, { 0, 0, 0, 1 } };
    const int m = std::clamp (mode, 1, 12) - 1;
    h1 = table[m][0]; h2 = table[m][1]; h3 = table[m][2]; rev = table[m][3];
}

void TapeEchoEngine::prepare (double sampleRate, int /*maxBlockSize*/)
{
    sr = sampleRate;
    D1 = kRefDelay * sr;
    const int tapeLen = nextPow2 ((int) (3.0 * D1) + 256);
    uint32_t seed = 0x9E3779B9u;
    for (auto& c : ch)
    {
        c.tape.init (tapeLen);
        c.noise = Noise (seed); seed = seed * 1664525u + 1013904223u;
        c.flutPhase = c.noise.uni(); c.flut2Phase = c.noise.uni();
    }
    spring.prepare (sr);

    const double fast = 0.02;
    for (Smoother* s : { &sendS, &echoS, &revS, &dryS, &outS, &inGainS, &cAL, &cAR, &cBL, &cBR, &selfFb, &crossFb })
        s->setTime (sr, fast);
    for (auto* arr : { &wLA, &wRA, &wLB, &wRB, &headG })
        for (auto& s : *arr) s.setTime (sr, 0.03);
    fbS.setTime (sr, 0.05);
    bypassS.setTime (sr, 0.015);
    powerWetS.setTime (sr, 0.15);
    revWidthS.setTime (sr, 0.05);
    duckS.setTime (sr, 0.05);
    duckAtk = 1.0f - (float) std::exp (-1.0 / (0.008 * sr));
    duckRel = 1.0f - (float) std::exp (-1.0 / (0.30 * sr));
    meterCoeff = 1.0f - (float) std::exp (-1.0 / (0.05 * sr));
    reset();
}

void TapeEchoEngine::reset()
{
    for (auto& c : ch)
    {
        c.tape.clear(); c.recLP.reset(); c.preEmph.reset(); c.loop.reset();
        c.fbIn = c.echo = 0; c.dropGain = c.dropTarget = 1; c.dropCount = 0; c.drift = c.driftTarget = 0;
    }
    outChainL.reset(); outChainR.reset();
    spring.reset();
    const double v = kRefDelay / std::clamp ((double) params.delaySec, kMinDelay, kMaxDelay);
    logV = std::log (v);
    motorV = v;
    powerSpeed = params.power ? 1.0f : 0.0f;
    sendS.snap (1); fbS.snap (0); echoS.snap (params.echoGain); revS.snap (params.reverbGain);
    dryS.snap (params.dryGain); outS.snap (params.outGain); bypassS.snap (params.bypass ? 1.0f : 0.0f);
    powerWetS.snap (params.power ? 1.0f : 0.0f); revWidthS.snap (1); duckS.snap (params.duck); inGainS.snap (params.inputGain);
    for (auto& h : headG) h.snap (0);
    cAL.snap (0.5f); cAR.snap (0.5f); cBL.snap (0); cBR.snap (0); selfFb.snap (1); crossFb.snap (0);
    for (auto* arr : { &wLA, &wRA, &wLB, &wRB }) for (auto& s : *arr) s.snap (0);
    duckEnv = 0;
    cBass = cTreble = 99; cAge = cLowCut = cDecay = -1; lastRecFc = -1;
}

void TapeEchoEngine::updateCoefficients()
{
    const auto& p = params;
    const bool toneChanged = p.bassDb != cBass || p.trebleDb != cTreble || p.tapeAge != cAge || p.lowCutHz != cLowCut;
    if (toneChanged)
    {
        cBass = p.bassDb; cTreble = p.trebleDb; cAge = p.tapeAge; cLowCut = p.lowCutHz;
        auto& c = ch[0].loop.bq;
        c[0].highShelf (sr, 2500.0, -5.0, 0.7);                         // playback de-emphasis
        c[1].highPass (sr, 38.0, 0.6);                                   // head low end roll-off
        c[2].peak (sr, 95.0, 0.9, 2.5);                                  // head bump
        c[3].peak (sr, 1300.0, 0.6, 1.2);                                // mid "honk" of the RE-201 preamps
        const double ageFc = 7200.0 * std::pow (0.38, (double) p.tapeAge);   // new 7.2k -> worn ~2.7k
        c[4].lowPass (sr, ageFc, 0.62);
        c[5].lowShelf (sr, 160.0, p.bassDb, 0.7);
        c[6].highShelf (sr, 3000.0, p.trebleDb, 0.7);
        c[7].highPass (sr, p.lowCutHz, 0.65);
        ch[1].loop.copyCoeffs (ch[0].loop);
        outChainL.copyCoeffs (ch[0].loop);
        outChainR.copyCoeffs (ch[0].loop);

        for (auto& cc : ch) cc.preEmph.highShelf (sr, 2500.0, 5.0, 0.7);
        hissLevel = (float) std::pow (10.0, (-84.0 + 26.0 * p.tapeAge) / 20.0);
        dropRate = p.tapeAge > 0.35f ? (p.tapeAge - 0.35f) * 1.6f : 0.0f;   // dropouts per second
    }
    if (p.springDecay != cDecay) { cDecay = p.springDecay; spring.setDecay (p.springDecay); }
}

float TapeEchoEngine::wowFlutterMod (Channel& c, float wowPhaseOffset) noexcept
{
    const float W = wfDepth;
    if (W <= 0.0f) return 0.0f;
    c.flutPhase += (float) (6.8 / sr);  if (c.flutPhase >= 1) c.flutPhase -= 1;   // capstan
    c.flut2Phase += (float) (13.9 / sr); if (c.flut2Phase >= 1) c.flut2Phase -= 1; // pinch roller
    const float wow = fastSin01 (wowPhase + wowPhaseOffset);
    const float fl = fastSin01 (c.flutPhase) * (0.75f + 0.25f * c.drift) + 0.35f * fastSin01 (c.flut2Phase);
    return W * (0.0040f * wow * 0.7f + 0.0022f * c.drift + 0.0011f * fl);
}

void TapeEchoEngine::process (const float* inL, const float* inR, float* outL, float* outR, int numSamples)
{
    const auto& p = params;
    updateCoefficients();

    // ---- block level targets
    bool h[3], revOn;
    modeHeads (p.mode, h[0], h[1], h[2], revOn);
    const int nActive = (int) h[0] + (int) h[1] + (int) h[2];
    const float hg = nActive > 0 ? std::pow ((float) nActive, -0.75f) : 0.0f;
    for (int i = 0; i < 3; ++i) headG[(size_t) i].target = h[i] ? hg : 0.0f;

    float fbT = 1.10f * std::pow (std::clamp (p.feedback, 0.0f, 1.0f), 1.15f);
    if (p.hold) fbT = std::max (fbT, 1.01f);
    if (p.throwOn && ! p.throwOnly) fbT = std::max (fbT, 1.12f);     // THROW in ALL mode = feedback swell
    fbS.target = fbT;

    float send = p.throwOnly ? (p.throwOn ? 1.0f : 0.0f) : 1.0f;
    if (p.hold || ! p.power) send = 0.0f;
    sendS.target = send;
    echoS.target = p.echoGain * (nActive > 0 ? 1.0f : 0.0f);
    revS.target = revOn ? p.reverbGain : 0.0f;
    dryS.target = p.dryGain;
    outS.target = p.outGain;
    inGainS.target = p.inputGain * (p.micMode ? 2.8f : 1.0f);
    bypassS.target = p.bypass ? 1.0f : 0.0f;
    duckS.target = p.duck;

    // stereo routing
    float pl[3], pr[3];
    equalPowerPan (-0.75f, pl[0], pr[0]);
    equalPowerPan (0.75f, pl[1], pr[1]);
    equalPowerPan (0.0f, pl[2], pr[2]);
    const int sm = std::clamp (p.stereoMode, 0, 3);
    cAL.target = sm == 2 ? 1.0f : 0.5f;  cAR.target = sm == 2 ? 0.0f : 0.5f;
    cBL.target = 0.0f;                   cBR.target = sm == 2 ? 1.0f : 0.0f;
    selfFb.target = sm == 3 ? 0.0f : 1.0f;
    crossFb.target = sm == 3 ? 1.0f : 0.0f;
    revWidthS.target = sm == 0 ? 0.0f : 1.0f;
    for (int i = 0; i < 3; ++i)
    {
        const float g = headG[(size_t) i].target;
        wLA[(size_t) i].target = sm == 1 ? g * pl[i] : g;
        wRA[(size_t) i].target = sm == 1 ? g * pr[i] : (sm <= 1 ? g : 0.0f);
        wLB[(size_t) i].target = 0.0f;
        wRB[(size_t) i].target = sm >= 2 ? g : 0.0f;
    }

    if (p.kick && ! lastKick) spring.kick();
    lastKick = p.kick;

    const double vTarget = kRefDelay / std::clamp ((double) p.delaySec, kMinDelay, kMaxDelay);
    const double logVT = std::log (vTarget);
    const double tau = 0.02 * std::pow (100.0, (double) std::clamp (p.inertia, 0.0f, 1.0f));
    const double kV = 1.0 - std::exp (-1.0 / (tau * sr));
    const float powerStep = p.power ? (float) (1.0 / (0.45 * sr)) : -(float) (1.0 / (1.1 * sr));
    const float satDrive = 0.55f + 2.6f * p.tapeSat;
    const float satBias = fastTanh (satDrive * 0.08f);
    const float preDrive = p.micMode ? 1.5f : 0.6f;
    const float wowInc = (float) (0.55 / sr);
    wfDepth = std::pow (std::clamp (p.wowFlutter, 0.0f, 1.0f), 1.5f) * 2.5f + p.tapeAge * 0.25f;

    float inPk = 0;

    for (int start = 0; start < numSamples; start += subBlock)
    {
        const int n = std::min (subBlock, numSamples - start);

        // per sub-block: drift targets, record LP tracking the tape speed
        for (auto& c : ch)
        {
            if (c.noise.uni() < 0.02f) c.driftTarget = c.noise.white();
            c.drift += (c.driftTarget - c.drift) * 0.02f;
        }
        const double vNow = std::exp (logV) * powerSpeed;
        const double recFc = std::clamp (0.42 * sr * std::max (vNow, 0.05), 1500.0, std::min (16000.0, 0.45 * sr));
        if (std::abs (recFc - lastRecFc) > 1.0)
        {
            lastRecFc = recFc;
            for (auto& c : ch) c.recLP.lowPass (sr, recFc, 0.6);
        }

        for (int k = 0; k < n; ++k)
        {
            const int idx = start + k;
            const float xL = inL[idx];
            const float xR = inR[idx];

            // ducking envelope
            const float e = std::max (std::abs (xL), std::abs (xR));
            duckEnv += (e > duckEnv ? duckAtk : duckRel) * (e - duckEnv);

            // motor
            if (std::abs (logVT - logV) > 1.0e-9) { logV += (logVT - logV) * kV; motorV = std::exp (logV); }
            powerSpeed = std::clamp (powerSpeed + powerStep, 0.0f, 1.0f);
            const double motor = motorV * (double) powerSpeed;
            wowPhase += wowInc; if (wowPhase >= 1) wowPhase -= 1;

            const float sendG = sendS.next();
            const float inG = inGainS.next();
            const float fbG = fbS.next();
            const float selfG = selfFb.next(), crossG = crossFb.next();
            const float aL = cAL.next(), aR = cAR.next(), bL = cBL.next(), bR = cBR.next();
            float g3[3];
            for (int i = 0; i < 3; ++i) g3[i] = headG[(size_t) i].next();
            float wla[3], wra[3], wlb[3], wrb[3];
            for (int i = 0; i < 3; ++i)
            {
                wla[i] = wLA[(size_t) i].next(); wra[i] = wRA[(size_t) i].next();
                wlb[i] = wLB[(size_t) i].next(); wrb[i] = wRB[(size_t) i].next();
            }

            // input preamp (both tape channels)
            float pre[2];
            const float srcA = (aL * xL + aR * xR) * inG;
            const float srcB = (bL * xL + bR * xR) * inG;
            pre[0] = fastTanh (preDrive * srcA) / preDrive;
            pre[1] = fastTanh (preDrive * srcB) / preDrive;
            inPk = std::max (inPk, std::abs (pre[0]) + std::abs (pre[1]));

            float heads[2][3];
            for (int c = 0; c < 2; ++c)
            {
                auto& C = ch[(size_t) c];
                // record path
                float rec = pre[c] * sendG + C.fbIn + C.noise.white() * hissLevel;
                rec = C.recLP.process (rec);
                rec = C.preEmph.process (rec);
                rec = tapeSaturate (rec, satDrive, satBias);

                const double v = motor * (1.0 + (double) wowFlutterMod (C, c ? 0.23f : 0.0f));
                C.tape.write (rec, std::max (0.0, v));

                // dropouts on worn tape
                if (C.dropCount > 0) { if (--C.dropCount == 0) C.dropTarget = 1.0f; }
                else if (dropRate > 0 && C.noise.uni() < dropRate / (float) sr)
                {
                    C.dropTarget = 0.25f + 0.55f * C.noise.uni();
                    C.dropCount = (int) ((0.02 + 0.12 * C.noise.uni()) * sr);
                }
                C.dropGain += (C.dropTarget - C.dropGain) * 0.004f;

                for (int i = 0; i < 3; ++i)
                    heads[c][i] = C.tape.read (D1 * (i + 1)) * C.dropGain;

                const float loopIn = g3[0] * heads[c][0] + g3[1] * heads[c][1] + g3[2] * heads[c][2];
                C.echo = C.loop.process (loopIn);
            }
            // feedback routing (self / ping-pong)
            ch[0].fbIn = fbG * (selfG * ch[0].echo + crossG * ch[1].echo);
            ch[1].fbIn = fbG * (selfG * ch[1].echo + crossG * ch[0].echo);

            float oL = 0, oR = 0;
            for (int i = 0; i < 3; ++i)
            {
                oL += wla[i] * heads[0][i] + wlb[i] * heads[1][i];
                oR += wra[i] * heads[0][i] + wrb[i] * heads[1][i];
            }
            oL = outChainL.process (oL);
            oR = outChainR.process (oR);

            // spring reverb gets the input and part of the echoes
            float rL, rR;
            spring.process ((pre[0] + pre[1]) * sendG * 0.8f + (oL + oR) * 0.35f, rL, rR);
            const float rw = revWidthS.next();
            const float rm = 0.5f * (rL + rR);
            rL = rm + rw * (rL - rm); rR = rm + rw * (rR - rm);

            const float powerWet = powerWetS.next();
            powerWetS.target = (p.power || powerSpeed > 0.0f) ? 1.0f : 0.0f;
            const float duckG = 1.0f - 0.92f * duckS.next() * std::min (1.0f, duckEnv * 4.0f);
            const float eg = echoS.next(), rg = revS.next();
            const float wetL = (oL * eg + rL * rg) * duckG * powerWet;
            const float wetR = (oR * eg + rR * rg) * duckG * powerWet;

            const float dg = dryS.next(), og = outS.next();
            const float yL = softClipOut ((xL * dg + wetL) * og);
            const float yR = softClipOut ((xR * dg + wetR) * og);
            const float bp = bypassS.next();
            outL[idx] = yL + bp * (xL - yL);
            outR[idx] = yR + bp * (xR - yR);

            meterEchoAcc += meterCoeff * (0.5f * (oL * oL + oR * oR) * eg * eg - meterEchoAcc);
            meterOutAcc += meterCoeff * (0.5f * (outL[idx] * outL[idx] + outR[idx] * outR[idx]) - meterOutAcc);
            meterInAcc += meterCoeff * ((pre[0] * pre[0] + pre[1] * pre[1]) - meterInAcc);
        }
    }

    meterIn.store (std::sqrt (meterInAcc));
    meterOut.store (std::sqrt (meterOutAcc));
    meterEcho.store (std::sqrt (meterEchoAcc));
    const double vNow = std::exp (logV) * powerSpeed;
    reelSpeed.store ((float) vNow);
    headDelayMs.store (vNow > 1.0e-4 ? (float) (1000.0 * kRefDelay / vNow) : 0.0f);
    (void) inPk;
}
} // namespace dub
