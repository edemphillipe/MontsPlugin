#include "Resampler.h"

#include <algorithm>
#include <cmath>

namespace gsp
{
namespace
{
constexpr double pi = 3.14159265358979323846;

double besselI0 (double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 32; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
    }
    return sum;
}
} // namespace

void StreamResampler::prepare (double inputRate, double outputRate, int maxInputBlock)
{
    ratio = inputRate / outputRate;
    // When downsampling, lower the cutoff to the output Nyquist (anti-aliasing), with a small margin.
    cutoff = std::min (1.0, 1.0 / ratio) * 0.95;

    const int taps = 2 * halfTaps;
    const double beta = 8.0;
    table.assign ((size_t) taps * (phases + 1), 0.0f);

    for (int p = 0; p <= phases; ++p)
    {
        const double frac = (double) p / phases;
        for (int t = 0; t < taps; ++t)
        {
            // distance from the output point to input tap t
            const double x = (double) (t - halfTaps + 1) - frac;
            const double sinc = (std::abs (x) < 1e-9) ? 1.0 : std::sin (pi * cutoff * x) / (pi * cutoff * x);
            const double w = x / halfTaps;
            const double window = (std::abs (w) >= 1.0) ? 0.0 : besselI0 (beta * std::sqrt (1.0 - w * w)) / besselI0 (beta);
            table[(size_t) p * taps + (size_t) t] = (float) (cutoff * sinc * window);
        }
    }

    buffer.assign ((size_t) (maxInputBlock + 4 * halfTaps + 8), 0.0f);
    reset();
}

void StreamResampler::reset()
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    // Start with halfTaps zeros of history so the first outputs have a full kernel.
    buffered = halfTaps;
    position = (double) (halfTaps - 1);
}

int StreamResampler::maxOutputFor (int nIn) const noexcept
{
    return (int) std::ceil ((nIn + 2.0 * halfTaps) / ratio) + 2;
}

float StreamResampler::kernelAt (int tap, double frac) const noexcept
{
    const double scaled = frac * phases;
    const int p = std::min ((int) scaled, phases - 1);
    const float a = (float) (scaled - p);
    const int taps = 2 * halfTaps;
    const float k0 = table[(size_t) p * taps + (size_t) tap];
    const float k1 = table[(size_t) (p + 1) * taps + (size_t) tap];
    return k0 + a * (k1 - k0);
}

int StreamResampler::process (const float* in, int nIn, float* out, int maxOut) noexcept
{
    const int capacity = (int) buffer.size();
    nIn = std::min (nIn, capacity - buffered);
    std::copy (in, in + nIn, buffer.begin() + buffered);
    buffered += nIn;

    int produced = 0;
    // Need taps up to floor(position) + halfTaps
    while (produced < maxOut && (int) std::floor (position) + halfTaps < buffered)
    {
        const int base = (int) std::floor (position);
        const double frac = position - base;
        const int first = base - halfTaps + 1;

        float acc = 0.0f;
        for (int t = 0; t < 2 * halfTaps; ++t)
            acc += buffer[(size_t) (first + t)] * kernelAt (t, frac);

        out[produced++] = acc;
        position += ratio;
    }

    // Drop samples no longer needed, keeping the history for the next kernel.
    const int keepFrom = std::max (0, (int) std::floor (position) - halfTaps + 1);
    if (keepFrom > 0)
    {
        std::copy (buffer.begin() + keepFrom, buffer.begin() + buffered, buffer.begin());
        buffered -= keepFrom;
        position -= keepFrom;
    }

    return produced;
}

} // namespace gsp
