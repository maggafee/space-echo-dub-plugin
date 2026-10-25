#pragma once
// Small, dependency-free filter building blocks (no JUCE) so the DSP can be
// unit-rendered offline.

#include <cmath>
#include <algorithm>
#include <cstdint>

namespace dub
{
constexpr double kPi = 3.14159265358979323846;

inline float flushDenormal (float x) noexcept { return std::abs (x) < 1.0e-20f ? 0.0f : x; }

// RBJ cookbook biquad, transposed direct form II
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1 = 0, z2 = 0;

    void reset() noexcept { z1 = z2 = 0; }

    inline float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = flushDenormal (b1 * x - a1 * y + z2);
        z2 = flushDenormal (b2 * x - a2 * y);
        return y;
    }

    void setIdentity() noexcept { b0 = 1; b1 = b2 = a1 = a2 = 0; }

    void set (double nb0, double nb1, double nb2, double na0, double na1, double na2) noexcept
    {
        b0 = (float) (nb0 / na0); b1 = (float) (nb1 / na0); b2 = (float) (nb2 / na0);
        a1 = (float) (na1 / na0); a2 = (float) (na2 / na0);
    }

    static double clampF (double f, double sr) { return std::clamp (f, 10.0, sr * 0.49); }

    void lowPass (double sr, double f, double q = 0.7071)
    {
        const double w = 2 * kPi * clampF (f, sr) / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void highPass (double sr, double f, double q = 0.7071)
    {
        const double w = 2 * kPi * clampF (f, sr) / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void peak (double sr, double f, double q, double dB)
    {
        const double A = std::pow (10.0, dB / 40), w = 2 * kPi * clampF (f, sr) / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
        set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void lowShelf (double sr, double f, double dB, double S = 0.8)
    {
        const double A = std::pow (10.0, dB / 40), w = 2 * kPi * clampF (f, sr) / sr, c = std::cos (w), s = std::sin (w);
        const double al = s / 2 * std::sqrt ((A + 1 / A) * (1 / S - 1) + 2), sa = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) - (A - 1) * c + sa), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sa),
             (A + 1) + (A - 1) * c + sa, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sa);
    }
    void highShelf (double sr, double f, double dB, double S = 0.8)
    {
        const double A = std::pow (10.0, dB / 40), w = 2 * kPi * clampF (f, sr) / sr, c = std::cos (w), s = std::sin (w);
        const double al = s / 2 * std::sqrt ((A + 1 / A) * (1 / S - 1) + 2), sa = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) + (A - 1) * c + sa), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sa),
             (A + 1) - (A - 1) * c + sa, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sa);
    }
};

struct OnePoleLP
{
    float a = 0, z = 0;
    void setCutoff (double sr, double f) { a = (float) std::exp (-2 * kPi * std::clamp (f, 1.0, sr * 0.49) / sr); }
    void reset (float v = 0) { z = v; }
    inline float process (float x) noexcept { z = flushDenormal (x + a * (z - x)); return z; }
};

struct OnePoleHP
{
    OnePoleLP lp;
    void setCutoff (double sr, double f) { lp.setCutoff (sr, f); }
    void reset() { lp.reset(); }
    inline float process (float x) noexcept { return x - lp.process (x); }
};

// sine of a normalised phase (0..1), parabolic approximation with one refinement step (~0.1% error)
inline float fastSin01 (float phase) noexcept
{
    float x = phase - std::floor (phase);          // 0..1
    x = x * 2.0f - 1.0f;                            // -1..1  ->  sin(pi*x) has the shape we want, negated
    const float y = 4.0f * x * (1.0f - std::abs (x));
    return -(0.225f * (y * std::abs (y) - y) + y);
}

// one-pole smoother used for per-sample parameter glides
struct Smoother
{
    float coeff = 0, value = 0, target = 0;
    void setTime (double sr, double seconds) { coeff = seconds <= 0 ? 0.0f : (float) std::exp (-1.0 / (seconds * sr)); }
    void snap (float v) { value = target = v; }
    inline float next() noexcept { value = target + coeff * (value - target); return value; }
};

// cheap deterministic noise
struct Noise
{
    uint32_t s = 0x12345678u;
    explicit Noise (uint32_t seed = 0x12345678u) : s (seed ? seed : 1u) {}
    inline float white() noexcept   // -1..1
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (float) ((int32_t) s) * (1.0f / 2147483648.0f);
    }
    inline float uni() noexcept { return 0.5f * (white() + 1.0f); }
};
} // namespace dub
