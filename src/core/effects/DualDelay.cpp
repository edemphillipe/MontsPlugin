#include "effects/DualDelay.h"

namespace gsp::fx
{
namespace
{
constexpr double maxDelaySeconds = 2.5;
}

const std::vector<std::string>& DualDelay::voiceNames()
{
    static const std::vector<std::string> names { "Digital", "Analog", "Tape" };
    return names;
}

DualDelay::DualDelay() : Effect ("dualdelay", "Dual Delay", Placement::PostAmp)
{
    const auto divisions = subdivisionNames();
    const float lastDivision = (float) divisions.size() - 1;
    auto& L = sides[0];
    auto& R = sides[1];
    // order matters for the pedal layout: selectors in L|R pairs, then knobs as an L row and an R row
    L.pVoice    = addParam ({ "voiceL", "TIPO L", 0.0f, 2.0f, 0.0f, "", voiceNames() });
    R.pVoice    = addParam ({ "voiceR", "TIPO R", 0.0f, 2.0f, 2.0f, "", voiceNames() });
    L.pDivision = addParam ({ "divisionL", "DIVISAO L", 0.0f, lastDivision, 0.0f, "", divisions });
    R.pDivision = addParam ({ "divisionR", "DIVISAO R", 0.0f, lastDivision, 2.0f, "", divisions });
    pSync       = addParam ({ "sync", "SYNC", 0.0f, 1.0f, 0.0f, "", { "Livre", "TAP" } });
    L.pTime     = addParam ({ "timeL", "TIME L", 20.0f, 2000.0f, 400.0f, "ms", {}, true });
    L.pFeedback = addParam ({ "feedbackL", "REPEATS L", 0.0f, 1.0f, 0.35f, "" });
    L.pTone     = addParam ({ "toneL", "TONE L", 0.0f, 1.0f, 0.7f, "" });
    L.pLevel    = addParam ({ "levelL", "LEVEL L", 0.0f, 1.0f, 0.35f, "" });
    pMod        = addParam ({ "mod", "MOD", 0.0f, 1.0f, 0.2f, "" });
    R.pTime     = addParam ({ "timeR", "TIME R", 20.0f, 2000.0f, 300.0f, "ms", {}, true });
    R.pFeedback = addParam ({ "feedbackR", "REPEATS R", 0.0f, 1.0f, 0.35f, "" });
    R.pTone     = addParam ({ "toneR", "TONE R", 0.0f, 1.0f, 0.45f, "" });
    R.pLevel    = addParam ({ "levelR", "LEVEL R", 0.0f, 1.0f, 0.35f, "" });
    pCross      = addParam ({ "cross", "CROSS", 0.0f, 1.0f, 0.0f, "" });
    pLink       = addParam ({ "link", "LINK", 0.0f, 1.0f, 0.0f, "", { "Separado", "Link" } });
}

double DualDelay::targetSeconds (int side) const noexcept
{
    const auto& s = sides[(size_t) juce::jlimit (0, 1, side)];
    if (choice (pSync) == 1 && tempo != nullptr)
        return std::min (maxDelaySeconds, tempo->secondsFor (choice (s.pDivision)));
    return get (s.pTime) * 0.001;
}

void DualDelay::prepareEffect (double sampleRate, int)
{
    const int capacity = (int) (sampleRate * maxDelaySeconds) + 4096;
    for (int k = 0; k < 2; ++k)
    {
        auto& s = sides[(size_t) k];
        s.line.prepare (capacity);
        s.timeSmooth.prepare (sampleRate, 120.0, (float) (targetSeconds (k) * sampleRate));
        // slightly different rates per side so the two tapes don't wobble in lockstep
        s.wow.setRate (sampleRate, k == 0 ? 0.55 : 0.61);
        s.flutter.setRate (sampleRate, k == 0 ? 6.3 : 5.7);
    }
}

void DualDelay::resetEffect()
{
    for (int k = 0; k < 2; ++k)
    {
        auto& s = sides[(size_t) k];
        s.line.clear();
        s.lp.reset();
        s.hp.reset();
        s.fb = 0.0f;
        s.timeSmooth.current = (float) (targetSeconds (k) * fs);
    }
}

void DualDelay::processEffect (float* l, float* r, int n, bool inputMuted) noexcept
{
    const float mod = get (pMod), cross = get (pCross);
    const bool linked = choice (pLink) == 1; // REPEATS and LEVEL of R follow L
    float target[2], feedback[2], level[2];
    int voice[2];

    for (int k = 0; k < 2; ++k)
    {
        auto& s = sides[(size_t) k];
        voice[k] = choice (s.pVoice);
        target[k] = (float) (targetSeconds (k) * fs);
        const auto& src = linked ? sides[0] : s;
        feedback[k] = std::min (0.98f, get (src.pFeedback) * (voice[k] == Digital ? 1.0f : 1.05f));
        level[k] = get (src.pLevel);

        // TONE: 1.5 kHz .. 15 kHz, darker by design for Analog and Tape
        double cutoff = 1500.0 * std::pow (10.0, (double) get (s.pTone));
        if (voice[k] == Analog) cutoff *= 0.45;
        if (voice[k] == Tape) cutoff *= 0.6;
        s.lp.setLowpass (fs, cutoff);
        s.hp.setLowpass (fs, voice[k] == Tape ? 90.0 : 40.0);
        s.drift.setRate (fs, 0.3 + 1.2 * mod);
    }

    for (int i = 0; i < n; ++i)
    {
        const float in[2] = { inputMuted ? 0.0f : l[i], inputMuted ? 0.0f : r[i] };
        float wet[2];

        for (int k = 0; k < 2; ++k)
        {
            auto& s = sides[(size_t) k];
            const float t = s.timeSmooth.next (target[k]);
            float m = 0.0f;
            if (voice[k] == Tape)
                m = (0.0025f * s.wow.sine() + 0.0006f * s.flutter.sine()) * (float) fs * (0.3f + mod);
            else if (voice[k] == Analog)
                m = s.drift.sine() * 0.0008f * (float) fs * mod;
            else
                m = s.drift.sine() * 0.0003f * (float) fs * mod;
            wet[k] = s.line.read (std::max (2.0f, t + m));

            // feedback path colour
            float x = s.hp.hp (s.lp.lp (wet[k]));
            if (voice[k] == Analog) x = std::tanh (1.2f * x) / 1.2f;
            if (voice[k] == Tape)   x = std::tanh (1.6f * x) / 1.6f;
            s.fb = x;
        }

        // each side is fed by its own input plus its own and (CROSS) the other side's repeats
        for (int k = 0; k < 2; ++k)
        {
            const float own = sides[(size_t) k].fb, other = sides[(size_t) (1 - k)].fb;
            sides[(size_t) k].line.push (in[k] + feedback[k] * ((1.0f - cross) * own + cross * other));
        }

        // dry stays at unity; each side's repeats are added at its own level
        l[i] += level[0] * sides[0].fb;
        r[i] += level[1] * sides[1].fb;
    }
}

} // namespace gsp::fx

namespace gsp::fx
{
std::vector<std::pair<int, int>> DualDelay::linkedParams() const
{
    if (choice (pLink) != 1)
        return {};
    return { { sides[0].pFeedback, sides[1].pFeedback }, { sides[0].pLevel, sides[1].pLevel } };
}
} // namespace gsp::fx
