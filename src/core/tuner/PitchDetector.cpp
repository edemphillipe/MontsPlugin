#include "tuner/PitchDetector.h"

#include <algorithm>

namespace gsp
{

PitchResult PitchDetector::detect (const float* x, int n, double sampleRate, double a4)
{
    PitchResult r;
    const int maxLag = std::min (n / 2, (int) (sampleRate / 30.0));
    const int minLag = std::max (2, (int) (sampleRate / 1200.0));
    const int window = n - maxLag;
    if (maxLag <= minLag || window < 64)
        return r;

    // ignore silence / noise floor
    double energy = 0.0;
    for (int i = 0; i < n; ++i)
        energy += (double) x[i] * x[i];
    if (energy / n < 1e-7) // about -70 dBFS RMS
        return r;

    diff.assign ((size_t) maxLag + 2, 0.0f);
    cmnd.assign ((size_t) maxLag + 2, 1.0f);
    for (int tau = 1; tau <= maxLag + 1; ++tau)
    {
        double s = 0.0;
        for (int i = 0; i < window; ++i)
        {
            const double d = (double) x[i] - x[i + tau];
            s += d * d;
        }
        diff[(size_t) tau] = (float) s;
    }
    double running = 0.0;
    for (int tau = 1; tau <= maxLag + 1; ++tau)
    {
        running += diff[(size_t) tau];
        cmnd[(size_t) tau] = running > 0.0 ? (float) (diff[(size_t) tau] * tau / running) : 1.0f;
    }

    // first dip below the threshold, then walk to its minimum
    constexpr float threshold = 0.12f;
    int tau = -1;
    for (int t = minLag; t <= maxLag; ++t)
        if (cmnd[(size_t) t] < threshold)
        {
            while (t + 1 <= maxLag && cmnd[(size_t) t + 1] < cmnd[(size_t) t])
                ++t;
            tau = t;
            break;
        }
    if (tau < 0)
        return r;

    // parabolic interpolation around the minimum
    const double a = cmnd[(size_t) tau - 1], b = cmnd[(size_t) tau], c = cmnd[(size_t) tau + 1];
    const double denom = a - 2.0 * b + c;
    const double shift = std::abs (denom) > 1e-12 ? 0.5 * (a - c) / denom : 0.0;
    const double period = tau + std::clamp (shift, -1.0, 1.0);

    r.frequency = sampleRate / period;
    const double midi = 69.0 + 12.0 * std::log2 (r.frequency / a4);
    const int nearest = (int) std::lround (midi);
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    r.note = names[((nearest % 12) + 12) % 12];
    r.octave = nearest / 12 - 1;
    r.cents = (midi - nearest) * 100.0;
    r.valid = true;
    return r;
}

} // namespace gsp
