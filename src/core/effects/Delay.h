#pragma once

// Stereo delay with six voicings: Digital, Analog, Tape, Reverse, Ping Pong, Modulated.
// Time is free (ms) or locked to the TAP tempo with a subdivision.

#include "effects/Effect.h"

namespace gsp::fx
{

class Delay final : public Effect
{
public:
    enum Mode { Digital = 0, Analog, Tape, Reverse, PingPong, Modulated };
    static const std::vector<std::string>& modeNames();

    Delay();

    // Delay time in seconds currently targeted (after tempo sync), for the UI and tests.
    double targetSeconds() const noexcept;

private:
    void prepareEffect (double, int) override;
    void resetEffect() override;
    void processEffect (float* l, float* r, int n, bool inputMuted) noexcept override;
    bool tailWhenBypassed() const noexcept override { return true; }

    float reverseRead (const DelayLine& line, float segment) noexcept;

    int pMode, pTime, pSync, pDivision, pFeedback, pMix, pTone, pMod, pWidth;
    DelayLine lineL, lineR;
    OnePole lpL, lpR, hpL, hpR;
    Lfo wow, flutter, modLfo;
    Smoothed timeSmooth;
    double reversePhase = 0.0;
    float fbL = 0.0f, fbR = 0.0f;
    int lastMode = -1;
};

} // namespace gsp::fx
