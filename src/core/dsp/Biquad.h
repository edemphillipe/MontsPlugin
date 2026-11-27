#pragma once

// RBJ cookbook biquads, processed in double precision (64-bit) to keep low-frequency
// shelves clean and stable at small buffer sizes.

#include <cmath>

namespace gsp
{

class Biquad
{
public:
    enum class Type { LowShelf, Peak, HighShelf, HighPass, LowPass };

    void setup (Type type, double sampleRate, double freq, double q, double gainDb) noexcept
    {
        constexpr double pi = 3.14159265358979323846;
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * pi * std::fmin (freq, sampleRate * 0.49) / sampleRate;
        const double cw = std::cos (w0), sw = std::sin (w0);
        const double alpha = sw / (2.0 * q);
        double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;

        switch (type)
        {
            case Type::Peak:
                b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A;
                a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A;
                break;
            case Type::LowShelf:
            {
                const double s = 2 * std::sqrt (A) * alpha;
                b0 = A * ((A + 1) - (A - 1) * cw + s);
                b1 = 2 * A * ((A - 1) - (A + 1) * cw);
                b2 = A * ((A + 1) - (A - 1) * cw - s);
                a0 = (A + 1) + (A - 1) * cw + s;
                a1 = -2 * ((A - 1) + (A + 1) * cw);
                a2 = (A + 1) + (A - 1) * cw - s;
                break;
            }
            case Type::HighShelf:
            {
                const double s = 2 * std::sqrt (A) * alpha;
                b0 = A * ((A + 1) + (A - 1) * cw + s);
                b1 = -2 * A * ((A - 1) + (A + 1) * cw);
                b2 = A * ((A + 1) + (A - 1) * cw - s);
                a0 = (A + 1) - (A - 1) * cw + s;
                a1 = 2 * ((A - 1) - (A + 1) * cw);
                a2 = (A + 1) - (A - 1) * cw - s;
                break;
            }
            case Type::HighPass:
                b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2;
                a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
                break;
            case Type::LowPass:
                b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2;
                a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
                break;
        }

        c0 = b0 / a0; c1 = b1 / a0; c2 = b2 / a0; c3 = a1 / a0; c4 = a2 / a0;
    }

    void reset() noexcept { z1 = z2 = 0.0; }

    float process (float in) noexcept
    {
        const double x = in;
        const double y = c0 * x + z1;
        z1 = c1 * x - c3 * y + z2;
        z2 = c2 * x - c4 * y;
        return (float) y;
    }

private:
    double c0 = 1, c1 = 0, c2 = 0, c3 = 0, c4 = 0;
    double z1 = 0, z2 = 0;
};

} // namespace gsp
