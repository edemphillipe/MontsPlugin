#include "effects/Modulation.h"

namespace gsp::fx
{
namespace
{
constexpr double twoPi = 6.283185307179586;
}

// ============================================================ Chorus
// Two modulated voices, LFO in quadrature between L and R for width.
Chorus::Chorus() : Effect ("chorus", "Chorus", Placement::PostAmp)
{
    pRate  = addParam ({ "rate", "RATE", 0.05f, 5.0f, 0.6f, "Hz", {}, true });
    pDepth = addParam ({ "depth", "DEPTH", 0.0f, 1.0f, 0.5f, "" });
    pMix   = addParam ({ "mix", "MIX", 0.0f, 1.0f, 0.5f, "" });
    pTone  = addParam ({ "tone", "TONE", 0.0f, 1.0f, 0.6f, "" });
}
void Chorus::prepareEffect (double sampleRate, int)
{
    dl.prepare ((int) (0.05 * sampleRate));
    dr.prepare ((int) (0.05 * sampleRate));
}
void Chorus::resetEffect() { dl.clear(); dr.clear(); lfo.phase = 0.0; wetLpL.reset(); wetLpR.reset(); }
void Chorus::processEffect (float* l, float* r, int n, bool) noexcept
{
    lfo.setRate (fs, get (pRate));
    const float depthMs = 0.3f + 4.0f * get (pDepth), centreMs = 7.0f + depthMs;
    const float mix = get (pMix);
    wetLpL.setLowpass (fs, 2500.0 + 12000.0 * get (pTone));
    wetLpR.a = wetLpL.a;
    const float spms = (float) fs * 0.001f;
    for (int i = 0; i < n; ++i)
    {
        const double ph = lfo.phase;
        lfo.advance();
        const float ml = (float) std::sin (twoPi * ph), mr = (float) std::cos (twoPi * ph);
        dl.push (l[i]);
        dr.push (r[i]);
        const float wl = wetLpL.lp (dl.read ((centreMs + depthMs * ml) * spms));
        const float wr = wetLpR.lp (dr.read ((centreMs + depthMs * mr) * spms));
        l[i] = l[i] * (1.0f - 0.5f * mix) + wl * mix;
        r[i] = r[i] * (1.0f - 0.5f * mix) + wr * mix;
    }
}

// ============================================================ Flanger
Flanger::Flanger() : Effect ("flanger", "Flanger", Placement::PostAmp)
{
    pRate   = addParam ({ "rate", "RATE", 0.02f, 4.0f, 0.25f, "Hz", {}, true });
    pDepth  = addParam ({ "depth", "DEPTH", 0.0f, 1.0f, 0.7f, "" });
    pManual = addParam ({ "manual", "MANUAL", 0.0f, 1.0f, 0.4f, "" });
    pRegen  = addParam ({ "regen", "REGEN", -0.95f, 0.95f, 0.5f, "" });
    pMix    = addParam ({ "mix", "MIX", 0.0f, 1.0f, 0.5f, "" });
}
void Flanger::prepareEffect (double sampleRate, int)
{
    dl.prepare ((int) (0.02 * sampleRate));
    dr.prepare ((int) (0.02 * sampleRate));
}
void Flanger::resetEffect() { dl.clear(); dr.clear(); fbL = fbR = 0.0f; lfo.phase = 0.0; }
void Flanger::processEffect (float* l, float* r, int n, bool) noexcept
{
    lfo.setRate (fs, get (pRate));
    const float spms = (float) fs * 0.001f;
    const float minMs = 0.15f + 2.0f * get (pManual), sweepMs = 6.0f * get (pDepth);
    const float regen = get (pRegen), mix = get (pMix);
    for (int i = 0; i < n; ++i)
    {
        const double ph = lfo.phase;
        lfo.advance();
        const float ml = 0.5f + 0.5f * (float) std::sin (twoPi * ph);
        const float mr = 0.5f + 0.5f * (float) std::sin (twoPi * (ph + 0.25));
        dl.push (l[i] + regen * fbL);
        dr.push (r[i] + regen * fbR);
        fbL = std::tanh (dl.read ((minMs + sweepMs * ml) * spms + 1.0f));
        fbR = std::tanh (dr.read ((minMs + sweepMs * mr) * spms + 1.0f));
        l[i] = l[i] * (1.0f - 0.5f * mix) + fbL * mix;
        r[i] = r[i] * (1.0f - 0.5f * mix) + fbR * mix;
    }
}

// ============================================================ Phaser
// Cascade of first-order all-pass stages swept exponentially, with feedback.
Phaser::Phaser() : Effect ("phaser", "Phaser", Placement::PostAmp)
{
    pRate     = addParam ({ "rate", "RATE", 0.02f, 6.0f, 0.4f, "Hz", {}, true });
    pDepth    = addParam ({ "depth", "DEPTH", 0.0f, 1.0f, 0.8f, "" });
    pFeedback = addParam ({ "feedback", "RESO", 0.0f, 0.9f, 0.4f, "" });
    pStages   = addParam ({ "stages", "STAGES", 0.0f, 2.0f, 1.0f, "", { "4", "6", "8" } });
    pMix      = addParam ({ "mix", "MIX", 0.0f, 1.0f, 0.5f, "" });
}
void Phaser::prepareEffect (double, int) {}
void Phaser::resetEffect()
{
    std::fill (std::begin (zl), std::end (zl), 0.0f);
    std::fill (std::begin (zr), std::end (zr), 0.0f);
    fbL = fbR = 0.0f;
    lfo.phase = 0.0;
}
void Phaser::processEffect (float* l, float* r, int n, bool) noexcept
{
    lfo.setRate (fs, get (pRate));
    const int stages = 4 + 2 * choice (pStages);
    const float depth = get (pDepth), fb = get (pFeedback), mix = get (pMix);
    const double fMin = 150.0, fMax = 150.0 * std::pow (2.0, 1.0 + 4.5 * depth);

    auto allpassChain = [&] (float x, float* z, float coeff)
    {
        for (int s = 0; s < stages; ++s)
        {
            const float y = -coeff * x + z[s];
            z[s] = x + coeff * y;
            x = y;
        }
        return x;
    };

    for (int i = 0; i < n; ++i)
    {
        const double ph = lfo.phase;
        lfo.advance();
        const double ml = 0.5 + 0.5 * std::sin (twoPi * ph), mr = 0.5 + 0.5 * std::sin (twoPi * (ph + 0.2));
        auto coeffFor = [&] (double m)
        {
            const double f = fMin * std::pow (fMax / fMin, m);
            const double t = std::tan (3.14159265358979 * std::min (f, fs * 0.45) / fs);
            return (float) ((1.0 - t) / (1.0 + t));
        };
        const float yl = allpassChain (l[i] + fb * fbL, zl, coeffFor (ml));
        const float yr = allpassChain (r[i] + fb * fbR, zr, coeffFor (mr));
        fbL = yl; fbR = yr;
        l[i] = l[i] * (1.0f - 0.5f * mix) + yl * mix;
        r[i] = r[i] * (1.0f - 0.5f * mix) + yr * mix;
    }
}

// ============================================================ Tremolo
Tremolo::Tremolo() : Effect ("tremolo", "Tremolo", Placement::PostAmp)
{
    pRate     = addParam ({ "rate", "RATE", 0.5f, 15.0f, 5.0f, "Hz", {}, true });
    pDepth    = addParam ({ "depth", "DEPTH", 0.0f, 1.0f, 0.6f, "" });
    pShape    = addParam ({ "shape", "SHAPE", 0.0f, 1.0f, 0.3f, "" }); // 0 = sine, 1 = square-ish
    pSync     = addParam ({ "sync", "SYNC", 0.0f, 1.0f, 0.0f, "", { "Livre", "TAP" } });
    pDivision = addParam ({ "division", "DIVISAO", 0.0f, (float) subdivisionNames().size() - 1, 1.0f, "", subdivisionNames() });
    pStereo   = addParam ({ "stereo", "STEREO", 0.0f, 1.0f, 0.0f, "", { "Mono", "Pan" } });
}
void Tremolo::prepareEffect (double sampleRate, int) { shapeSmooth.prepare (sampleRate, 30.0, get (pShape)); }
void Tremolo::resetEffect() { lfo.phase = 0.0; }
void Tremolo::processEffect (float* l, float* r, int n, bool) noexcept
{
    double rate = get (pRate);
    if (choice (pSync) == 1 && tempo != nullptr)
        rate = 1.0 / tempo->secondsFor (choice (pDivision));
    lfo.setRate (fs, rate);
    const float depth = get (pDepth);
    const bool pan = choice (pStereo) == 1;
    for (int i = 0; i < n; ++i)
    {
        const float sh = shapeSmooth.next (get (pShape));
        const float s = (float) std::sin (twoPi * lfo.phase);
        lfo.advance();
        // morph sine -> square with a soft limiter
        const float k = 1.0f + 12.0f * sh;
        const float wave = std::tanh (k * s) / std::tanh (k);
        const float gl = 1.0f - depth * 0.5f * (1.0f + wave);
        const float gr = pan ? 1.0f - depth * 0.5f * (1.0f - wave) : gl;
        l[i] *= gl;
        r[i] *= gr;
    }
}

} // namespace gsp::fx
