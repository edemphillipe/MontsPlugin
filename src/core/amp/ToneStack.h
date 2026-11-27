#pragma once

// Passive Treble/Middle/Bass tone stack (the circuit used, with different part values,
// in most classic guitar amps), discretised with the bilinear transform.
//
// Circuit: input -> C1 -> treble pot (R1, wiper = output) -> bass pot (R2, variable resistor)
// -> mid pot (R3, variable resistor to ground); input -> slope resistor R4 -> node A;
// A -> C2 -> bottom of the treble pot; A -> C3 -> top of the mid pot.
// H(s) = (b1 s + b2 s^2 + b3 s^3) / (1 + a1 s + a2 s^2 + a3 s^3) was derived from the nodal
// equations of this circuit (symbolic solve); the tests check it against a numerical nodal
// solve of the same circuit. t, m and l are the treble, middle and bass pot positions (0..1).

#include <cmath>
#include <complex>

namespace gsp::amp
{

struct ToneStackParts
{
    double C1, C2, C3, R1, R2, R3, R4;
};

class ToneStack
{
public:
    struct Analog
    {
        double b1, b2, b3, a1, a2, a3;
    };

    // Analog polynomial coefficients for knob positions treble/middle/bass in 0..1.
    static Analog analog (const ToneStackParts& p, double treble, double middle, double bass) noexcept
    {
        const double t = treble, m = middle;
        const double l = std::exp ((bass - 1.0) * 3.4); // logarithmic bass pot
        const double C1 = p.C1, C2 = p.C2, C3 = p.C3, R1 = p.R1, R2 = p.R2, R3 = p.R3, R4 = p.R4;

        Analog a {};
        a.b1 = C1 * R1 * t + C1 * R2 * l + C1 * R3 * m + C2 * R2 * l + C2 * R3 * m + C3 * R3 * m;
        a.b2 = C1 * C2 * R1 * R2 * l + C1 * C2 * R1 * R3 * m + C1 * C2 * R1 * R4 * t + C1 * C2 * R2 * R4 * l
             + C1 * C2 * R3 * R4 * m + C1 * C3 * R1 * R3 * m + C1 * C3 * R1 * R4 * t + C1 * C3 * R2 * R3 * l * m
             + C1 * C3 * R2 * R4 * l + C1 * C3 * R3 * R4 * m + C2 * C3 * R2 * R3 * l * m;
        a.b3 = C1 * C2 * C3 * R2 * l * (R1 * R3 * m + R1 * R4 * t + R3 * R4 * m);
        a.a1 = C1 * R1 + C1 * R2 * l + C1 * R3 * m + C2 * R2 * l + C2 * R3 * m + C2 * R4 + C3 * R3 * m + C3 * R4;
        a.a2 = C1 * C2 * R1 * R2 * l + C1 * C2 * R1 * R3 * m + C1 * C2 * R1 * R4 + C1 * C2 * R2 * R4 * l
             + C1 * C2 * R3 * R4 * m + C1 * C3 * R1 * R3 * m + C1 * C3 * R1 * R4 + C1 * C3 * R2 * R3 * l * m
             + C1 * C3 * R2 * R4 * l + C1 * C3 * R3 * R4 * m + C2 * C3 * R2 * R3 * l * m + C2 * C3 * R2 * R4 * l;
        a.a3 = C1 * C2 * C3 * R1 * R2 * R3 * l * m + C1 * C2 * C3 * R1 * R2 * R4 * l + C1 * C2 * C3 * R2 * R3 * R4 * l * m;
        return a;
    }

    // |H(j 2 pi f)| of the analog circuit (used by the tests to check the digital filter).
    static double analogMagnitude (const Analog& a, double f) noexcept
    {
        const std::complex<double> s (0.0, 2.0 * 3.14159265358979323846 * f);
        const auto num = a.b1 * s + a.b2 * s * s + a.b3 * s * s * s;
        const auto den = 1.0 + a.a1 * s + a.a2 * s * s + a.a3 * s * s * s;
        return std::abs (num / den);
    }

    void setup (const ToneStackParts& parts, double sampleRate, double treble, double middle, double bass) noexcept
    {
        const auto a = analog (parts, treble, middle, bass);
        const double c = 2.0 * sampleRate, c2 = c * c, c3 = c2 * c;
        const double B0 = -a.b1 * c - a.b2 * c2 - a.b3 * c3;
        const double B1 = -a.b1 * c + a.b2 * c2 + 3.0 * a.b3 * c3;
        const double B2 =  a.b1 * c + a.b2 * c2 - 3.0 * a.b3 * c3;
        const double B3 =  a.b1 * c - a.b2 * c2 + a.b3 * c3;
        const double A0 = -1.0 - a.a1 * c - a.a2 * c2 - a.a3 * c3;
        const double A1 = -3.0 - a.a1 * c + a.a2 * c2 + 3.0 * a.a3 * c3;
        const double A2 = -3.0 + a.a1 * c + a.a2 * c2 - 3.0 * a.a3 * c3;
        const double A3 = -1.0 + a.a1 * c - a.a2 * c2 + a.a3 * c3;
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; b3 = B3 / A0;
        a1 = A1 / A0; a2 = A2 / A0; a3 = A3 / A0;
    }

    void reset() noexcept { z1 = z2 = z3 = 0.0; }

    // transposed direct form II, double precision
    double process (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y + z3;
        z3 = b3 * x - a3 * y;
        return y;
    }

    // magnitude of the digital filter at frequency f (tests)
    double digitalMagnitude (double f, double sampleRate) const noexcept
    {
        const std::complex<double> z1i = std::polar (1.0, -2.0 * 3.14159265358979323846 * f / sampleRate);
        const auto z2i = z1i * z1i, z3i = z2i * z1i;
        return std::abs ((b0 + b1 * z1i + b2 * z2i + b3 * z3i) / (1.0 + a1 * z1i + a2 * z2i + a3 * z3i));
    }

private:
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, a1 = 0, a2 = 0, a3 = 0;
    double z1 = 0, z2 = 0, z3 = 0;
};

} // namespace gsp::amp
