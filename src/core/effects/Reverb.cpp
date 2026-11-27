#include "effects/Reverb.h"

namespace gsp::fx
{
namespace
{
// Mutually prime line lengths in ms (spread 30..90 ms at size 1).
constexpr float lineMs[8] = { 29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 61.7f, 71.3f, 83.9f };
constexpr float diffMs[4] = { 4.7f, 3.6f, 12.7f, 9.3f };
}

const std::vector<std::string>& Reverb::modeNames()
{
    static const std::vector<std::string> names { "Hall", "Plate", "Room", "Spring", "Ambient", "Cloud", "Shimmer" };
    return names;
}

Reverb::Reverb() : Effect ("reverb", "Reverb", Placement::PostAmp)
{
    pMode     = addParam ({ "mode", "MODO", 0.0f, 6.0f, 0.0f, "", modeNames() });
    pDecay    = addParam ({ "decay", "DECAY", 0.2f, 20.0f, 2.5f, "s", {}, true });
    pPreDelay = addParam ({ "predelay", "PRE-DELAY", 0.0f, 250.0f, 20.0f, "ms" });
    pTone     = addParam ({ "tone", "TONE", 0.0f, 1.0f, 0.5f, "" });
    pMix      = addParam ({ "mix", "MIX", 0.0f, 1.0f, 0.3f, "" });
    pMod      = addParam ({ "mod", "MOD", 0.0f, 1.0f, 0.4f, "" });
    pParam    = addParam ({ "param", "SHIMMER", 0.0f, 1.0f, 0.4f, "" }); // shimmer amount / spring drip / cloud density
}

float Reverb::OctaveUp::process (float x) noexcept
{
    line.push (x);
    // Each grain's read pointer moves twice as fast as the write pointer -> one octave up.
    float out = 0.0f;
    for (int k = 0; k < 2; ++k)
    {
        double p = phase + 0.5 * k;
        p -= std::floor (p);
        const float d = 2.0f + window * (1.0f - (float) p);
        const float w = (float) std::sin (3.14159265358979 * p);
        out += line.read (d) * w * w;
    }
    phase += 1.0 / window;
    if (phase >= 1.0) phase -= 1.0;
    return out;
}

void Reverb::prepareEffect (double sampleRate, int)
{
    const float scale = (float) sampleRate / 1000.0f;
    preDelay.prepare ((int) (0.3 * sampleRate));
    for (int i = 0; i < numDiffusers; ++i)
    {
        diffL[i].line.prepare ((int) (diffMs[i] * 2.0f * scale) + 8);
        diffR[i].line.prepare ((int) (diffMs[i] * 2.0f * scale) + 8);
    }
    for (int i = 0; i < numSpringAllpasses; ++i)
        spring[i].line.prepare (16);
    for (int i = 0; i < numLines; ++i)
    {
        baseLen[i] = lineMs[i] * scale;
        lines[i].prepare ((int) (baseLen[i] * 3.0f) + 64);
        lineLfo[i].setRate (sampleRate, 0.1 + 0.07 * i);
        lineLfo[i].phase = i / (double) numLines;
    }
    shimmerL.line.prepare (8192);
    shimmerR.line.prepare (8192);
    shimmerL.window = shimmerR.window = (float) (0.045 * sampleRate);
    shimmerR.phase = 0.37;
    lastMode = -1;
}

void Reverb::resetEffect()
{
    preDelay.clear();
    for (auto& a : diffL) a.line.clear();
    for (auto& a : diffR) a.line.clear();
    for (auto& a : spring) a.line.clear();
    for (int i = 0; i < numLines; ++i) { lines[i].clear(); damp[i].reset(); lowCut[i].reset(); state[i] = 0.0f; }
    shimmerL.line.clear(); shimmerR.line.clear();
    shimmerFbL = shimmerFbR = 0.0f;
}

void Reverb::configure (int mode) noexcept
{
    earlyGain = 0.0f;
    switch (mode)
    {
        case Hall:    sizeScale = 1.6f; diffusion = 0.70f; modDepth = 10.0f; highDamp = 7000.0f; lowDampHz = 50.0f; decayScale = 1.0f; break;
        case Plate:   sizeScale = 0.9f; diffusion = 0.78f; modDepth = 4.0f;  highDamp = 11000.0f; lowDampHz = 120.0f; decayScale = 1.0f; break;
        case Room:    sizeScale = 0.5f; diffusion = 0.60f; modDepth = 2.0f;  highDamp = 6000.0f; lowDampHz = 80.0f; decayScale = 0.5f; earlyGain = 0.4f; break;
        case Spring:  sizeScale = 0.45f; diffusion = 0.50f; modDepth = 1.0f; highDamp = 4500.0f; lowDampHz = 200.0f; decayScale = 0.8f; break;
        case Ambient: sizeScale = 2.2f; diffusion = 0.75f; modDepth = 14.0f; highDamp = 4500.0f; lowDampHz = 80.0f; decayScale = 1.3f; break;
        case Cloud:   sizeScale = 2.8f; diffusion = 0.85f; modDepth = 22.0f; highDamp = 5500.0f; lowDampHz = 70.0f; decayScale = 1.6f; break;
        case Shimmer: sizeScale = 2.4f; diffusion = 0.80f; modDepth = 12.0f; highDamp = 8000.0f; lowDampHz = 150.0f; decayScale = 1.3f; break;
        default: break;
    }

    const float scale = (float) fs / 1000.0f;
    for (int i = 0; i < numDiffusers; ++i)
    {
        diffL[i].delay = diffMs[i] * scale * std::min (sizeScale, 1.5f);
        diffR[i].delay = diffMs[i] * scale * std::min (sizeScale, 1.5f) * 1.07f;
        diffL[i].g = diffR[i].g = diffusion;
    }
    for (int i = 0; i < numSpringAllpasses; ++i)
    {
        spring[i].delay = 1.0f + (float) (i % 3);
        spring[i].g = 0.62f; // dispersive chain -> the "boing" chirp
    }
    for (int i = 0; i < numLines; ++i)
    {
        lineLen[i] = std::min (baseLen[i] * sizeScale, (float) lines[i].capacity() - modDepth * 2.0f - 8.0f);
        lowCut[i].setLowpass (fs, lowDampHz);
    }
}

void Reverb::processEffect (float* l, float* r, int n, bool inputMuted) noexcept
{
    const int mode = choice (pMode);
    if (mode != lastMode)
    {
        configure (mode);
        if (lastMode >= 0)
            resetEffect(); // avoid a burst when the voicing changes
        lastMode = mode;
    }

    const float t60 = get (pDecay) * decayScale;
    const float tone = get (pTone);
    const float mix = get (pMix);
    const float modAmt = get (pMod) * modDepth * (float) fs / 48000.0f;
    const float param = get (pParam);
    const float pre = get (pPreDelay) * 0.001f * (float) fs;

    float gains[numLines];
    for (int i = 0; i < numLines; ++i)
    {
        gains[i] = std::pow (10.0f, -3.0f * (lineLen[i] / (float) fs) / std::max (0.05f, t60));
        damp[i].setLowpass (fs, highDamp * (0.25 + 1.5 * tone));
    }
    shimmerLp.setLowpass (fs, 7000.0);
    const float shimmerAmount = mode == Shimmer ? 0.55f * param : 0.0f;
    if (mode == Spring)
        for (auto& a : spring) a.g = 0.5f + 0.25f * param;

    for (int s = 0; s < n; ++s)
    {
        float inL = inputMuted ? 0.0f : l[s];
        float inR = inputMuted ? 0.0f : r[s];

        // pre-delay on the mono-ish sum keeps the image stable
        preDelay.push (0.5f * (inL + inR));
        float x = pre >= 1.0f ? preDelay.read (pre) : 0.5f * (inL + inR);
        float xl = x + 0.25f * (inL - inR) + shimmerFbL;
        float xr = x - 0.25f * (inL - inR) + shimmerFbR;

        if (mode == Spring)
            for (auto& a : spring) { xl = a.process (xl); }

        for (int d = 0; d < numDiffusers; ++d)
        {
            xl = diffL[d].process (xl);
            xr = diffR[d].process (xr);
        }
        if (mode == Spring)
            xr = xl;

        // read the network
        float out[numLines];
        for (int i = 0; i < numLines; ++i)
        {
            const float m = modAmt * lineLfo[i].sine();
            out[i] = lines[i].read (lineLen[i] + m + modAmt + 1.0f);
        }

        // Householder feedback matrix: y = x - (2/N) * sum(x)
        float sum = 0.0f;
        for (int i = 0; i < numLines; ++i) sum += out[i];
        const float k = 2.0f / numLines * sum;

        for (int i = 0; i < numLines; ++i)
        {
            float v = out[i] - k;
            v = damp[i].lp (v);
            v = v - lowCut[i].lp (v) * 0.6f;               // gentle low-frequency damping
            const float input = (i % 2 == 0) ? xl : xr;
            lines[i].push (input * 0.6f + v * gains[i]);
        }

        // decorrelated stereo taps
        float wetL = out[0] + out[2] + out[4] + out[6] - 0.5f * (out[1] + out[5]);
        float wetR = out[1] + out[3] + out[5] + out[7] - 0.5f * (out[2] + out[6]);
        wetL *= 0.35f;
        wetR *= 0.35f;
        if (earlyGain > 0.0f)
        {
            wetL += earlyGain * xl * 0.5f;
            wetR += earlyGain * xr * 0.5f;
        }

        if (shimmerAmount > 0.0f)
        {
            // soft-limited so a long decay can never run away
            shimmerFbL = std::tanh (shimmerLp.lp (shimmerL.process (wetL)) * shimmerAmount);
            shimmerFbR = std::tanh (shimmerR.process (wetR) * shimmerAmount);
        }
        else
        {
            shimmerFbL = shimmerFbR = 0.0f;
        }

        if (inputMuted)
        {
            l[s] += mix * wetL;
            r[s] += mix * wetR;
        }
        else
        {
            const float dryGain = 1.0f - std::max (0.0f, mix - 0.5f);
            l[s] = inL * dryGain + mix * wetL;
            r[s] = inR * dryGain + mix * wetR;
        }
    }
}

} // namespace gsp::fx
