#include "effects/Delay.h"

namespace gsp::fx
{
namespace
{
constexpr double maxDelaySeconds = 2.5;
constexpr double twoPi = 6.283185307179586;
}

const std::vector<std::string>& Delay::modeNames()
{
    static const std::vector<std::string> names { "Digital", "Analog", "Tape", "Reverse", "Ping Pong", "Modulado" };
    return names;
}

Delay::Delay() : Effect ("delay", "Delay", Placement::PostAmp)
{
    pMode     = addParam ({ "mode", "MODO", 0.0f, 5.0f, 0.0f, "", modeNames() });
    pTime     = addParam ({ "time", "TIME", 20.0f, 2000.0f, 400.0f, "ms", {}, true });
    pSync     = addParam ({ "sync", "SYNC", 0.0f, 1.0f, 0.0f, "", { "Livre", "TAP" } });
    pDivision = addParam ({ "division", "DIVISAO", 0.0f, (float) subdivisionNames().size() - 1, 2.0f, "", subdivisionNames() });
    pFeedback = addParam ({ "feedback", "REPEATS", 0.0f, 1.0f, 0.35f, "" });
    pMix      = addParam ({ "mix", "MIX", 0.0f, 1.0f, 0.3f, "" });
    pTone     = addParam ({ "tone", "TONE", 0.0f, 1.0f, 0.6f, "" });
    pMod      = addParam ({ "mod", "MOD", 0.0f, 1.0f, 0.2f, "" });
    pWidth    = addParam ({ "width", "WIDTH", 0.0f, 1.0f, 0.3f, "" });
}

double Delay::targetSeconds() const noexcept
{
    if (choice (pSync) == 1 && tempo != nullptr)
        return std::min (maxDelaySeconds, tempo->secondsFor (choice (pDivision)));
    return get (pTime) * 0.001;
}

void Delay::prepareEffect (double sampleRate, int)
{
    // Reverse reads back up to 2x the segment length.
    const int capacity = (int) (sampleRate * maxDelaySeconds * 2.0) + 4096;
    lineL.prepare (capacity);
    lineR.prepare (capacity);
    timeSmooth.prepare (sampleRate, 120.0, (float) (targetSeconds() * sampleRate));
    wow.setRate (sampleRate, 0.55);
    flutter.setRate (sampleRate, 6.3);
    lastMode = -1;
}

void Delay::resetEffect()
{
    lineL.clear(); lineR.clear();
    lpL.reset(); lpR.reset(); hpL.reset(); hpR.reset();
    fbL = fbR = 0.0f;
    reversePhase = 0.0;
    timeSmooth.current = (float) (targetSeconds() * fs);
}

float Delay::reverseRead (const DelayLine& line, float segment) noexcept
{
    // Two readers, half a segment apart, each sweeping backwards through the last segment,
    // cross-faded with sin^2 windows (sum = 1).
    float out = 0.0f;
    for (int k = 0; k < 2; ++k)
    {
        double p = reversePhase + 0.5 * k;
        p -= std::floor (p);
        const float d = 2.0f + 2.0f * (float) p * segment;
        const float w = (float) std::sin (3.14159265358979 * p);
        out += line.read (d) * w * w;
    }
    return out;
}

void Delay::processEffect (float* l, float* r, int n, bool inputMuted) noexcept
{
    const int mode = choice (pMode);
    if (mode != lastMode)
    {
        fbL = fbR = 0.0f;
        lastMode = mode;
    }

    const float target = (float) (targetSeconds() * fs);
    const float feedback = std::min (0.98f, get (pFeedback) * (mode == Analog || mode == Tape ? 1.05f : 1.0f));
    const float mix = get (pMix), tone = get (pTone), mod = get (pMod);
    const float widthSamples = get (pWidth) * 0.015f * (float) fs;

    // tone: darker for analog/tape by design
    double cutoff = 1500.0 * std::pow (10.0, (double) tone);       // 1.5 k .. 15 k
    if (mode == Analog) cutoff *= 0.45;
    if (mode == Tape) cutoff *= 0.6;
    lpL.setLowpass (fs, cutoff); lpR.a = lpL.a;
    hpL.setLowpass (fs, mode == Tape ? 90.0 : 40.0); hpR.a = hpL.a;
    modLfo.setRate (fs, 0.4 + 1.6 * mod);

    const float segment = target; // reverse segment length
    const double reverseInc = 1.0 / std::max (64.0f, segment);

    for (int i = 0; i < n; ++i)
    {
        const float inL = inputMuted ? 0.0f : l[i];
        const float inR = inputMuted ? 0.0f : r[i];
        const float t = timeSmooth.next (target);

        // modulation (in samples)
        float m = 0.0f;
        if (mode == Tape)
            m = (0.0025f * wow.sine() + 0.0006f * flutter.sine()) * (float) fs * (0.3f + mod);
        else if (mode == Modulated || mode == Analog)
            m = modLfo.sine() * (mode == Modulated ? 0.004f : 0.0008f) * (float) fs * mod;

        float wetL = 0.0f, wetR = 0.0f;
        if (mode == Reverse)
        {
            wetL = reverseRead (lineL, segment);
            wetR = reverseRead (lineR, segment + widthSamples * 0.5f);
            reversePhase += reverseInc;
            if (reversePhase >= 1.0) reversePhase -= 1.0;
        }
        else
        {
            wetL = lineL.read (std::max (2.0f, t + m));
            wetR = lineR.read (std::max (2.0f, t + m + (mode == PingPong ? 0.0f : widthSamples)));
        }

        // feedback path coloration
        auto colour = [&] (float x, OnePole& lp, OnePole& hp)
        {
            x = hp.hp (lp.lp (x));
            if (mode == Analog) x = std::tanh (1.2f * x) / 1.2f;
            if (mode == Tape)   x = std::tanh (1.6f * x) / 1.6f;
            return x;
        };
        fbL = colour (wetL, lpL, hpL);
        fbR = colour (wetR, lpR, hpR);

        if (mode == PingPong)
        {
            // mono input enters the left line; repeats bounce L -> R -> L
            lineL.push ((inL + inR) * 0.5f + feedback * fbR);
            lineR.push (feedback * fbL);
        }
        else
        {
            lineL.push (inL + feedback * fbL);
            lineR.push (inR + feedback * fbR);
        }

        if (inputMuted)
        {
            // bypassed: dry passes untouched, the repeats ring out on top
            l[i] += mix * fbL;
            r[i] += mix * fbR;
        }
        else
        {
            // dry stays at unity up to mix = 0.5, then trims slightly
            const float dryGain = 1.0f - std::max (0.0f, mix - 0.5f);
            l[i] = inL * dryGain + mix * fbL;
            r[i] = inR * dryGain + mix * fbR;
        }
    }
}

} // namespace gsp::fx
