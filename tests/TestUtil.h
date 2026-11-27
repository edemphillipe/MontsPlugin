#pragma once

// Tiny test harness shared by the test files.

#include <juce_core/juce_core.h>

#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <vector>

namespace gsptest
{
inline int failures = 0, checks = 0, skipped = 0;

#define CHECK(cond, ...)                                                        \
    do {                                                                        \
        ++gsptest::checks;                                                      \
        if (! (cond)) {                                                         \
            ++gsptest::failures;                                                \
            std::printf ("  FAIL %s:%d  %s  ", __FILE__, __LINE__, #cond);      \
            std::printf (__VA_ARGS__);                                          \
            std::printf ("\n");                                                \
        }                                                                       \
    } while (0)

inline std::vector<float> guitarLikeSignal (int n, double sr, unsigned seed = 1)
{
    // Plucked-string-ish test signal: decaying harmonics plus a little noise.
    std::mt19937 rng (seed);
    std::normal_distribution<float> noise (0.0f, 0.002f);
    std::vector<float> x ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        const double env = std::exp (-3.0 * std::fmod (t, 0.5));
        double v = 0.0;
        for (int h = 1; h <= 6; ++h)
            v += std::sin (2.0 * juce::MathConstants<double>::pi * 110.0 * h * t) / h;
        x[(size_t) i] = (float) (0.25 * env * v) + noise (rng);
    }
    return x;
}

inline bool allFinite (const std::vector<float>& v)
{
    for (float s : v)
        if (! std::isfinite (s))
            return false;
    return true;
}

inline double rms (const std::vector<float>& v, size_t from = 0, size_t to = 0)
{
    if (to == 0 || to > v.size()) to = v.size();
    double s = 0;
    for (size_t i = from; i < to; ++i)
        s += (double) v[i] * v[i];
    return std::sqrt (s / (double) std::max ((size_t) 1, to - from));
}

inline float maxAbsDiff (const std::vector<float>& a, const std::vector<float>& b)
{
    float d = 0;
    for (size_t i = 0; i < std::min (a.size(), b.size()); ++i)
        d = std::max (d, std::abs (a[i] - b[i]));
    return d;
}

inline void runTest (const char* name, const std::function<void()>& fn)
{
    const int before = failures;
    std::printf ("[ %s ]\n", name);
    fn();
    std::printf ("  %s\n", failures == before ? "ok" : "FAILED");
}
} // namespace gsptest
