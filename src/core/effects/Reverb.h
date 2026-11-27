#pragma once

// Stereo algorithmic reverb: 8-line feedback delay network with input diffusion,
// per-line damping and modulation. Voicings: Hall, Plate, Room, Spring, Ambient,
// Cloud and Shimmer (octave-up pitch shifter in the feedback path).

#include "effects/Effect.h"

namespace gsp::fx
{

class Reverb final : public Effect
{
public:
    enum Mode { Hall = 0, Plate, Room, Spring, Ambient, Cloud, Shimmer };
    static const std::vector<std::string>& modeNames();

    Reverb();

private:
    static constexpr int numLines = 8;
    static constexpr int numDiffusers = 4;
    static constexpr int numSpringAllpasses = 16;

    struct Allpass
    {
        DelayLine line;
        float delay = 100.0f, g = 0.6f;
        float process (float x) noexcept
        {
            const float d = line.read (delay);
            const float v = x + g * d;
            line.push (v);
            return d - g * v;
        }
    };

    // Two-grain pitch shifter (+12 semitones) used by Shimmer.
    struct OctaveUp
    {
        DelayLine line;
        double phase = 0.0;
        float window = 2048.0f;
        float process (float x) noexcept;
    };

    void prepareEffect (double, int) override;
    void resetEffect() override;
    void processEffect (float* l, float* r, int n, bool inputMuted) noexcept override;
    bool tailWhenBypassed() const noexcept override { return true; }
    void configure (int mode) noexcept;

    int pMode, pDecay, pPreDelay, pTone, pMix, pMod, pParam;

    DelayLine preDelay;
    Allpass diffL[numDiffusers], diffR[numDiffusers];
    Allpass spring[numSpringAllpasses];
    DelayLine lines[numLines];
    float baseLen[numLines] {};
    float lineLen[numLines] {};
    OnePole damp[numLines];
    OnePole lowCut[numLines];
    Lfo lineLfo[numLines];
    float state[numLines] {};
    OctaveUp shimmerL, shimmerR;
    OnePole shimmerLp;
    float shimmerFbL = 0.0f, shimmerFbR = 0.0f;

    // per-mode voicing
    float sizeScale = 1.0f, diffusion = 0.7f, modDepth = 8.0f, highDamp = 6000.0f, lowDampHz = 60.0f;
    float decayScale = 1.0f, earlyGain = 0.0f;
    int lastMode = -1;
};

} // namespace gsp::fx
