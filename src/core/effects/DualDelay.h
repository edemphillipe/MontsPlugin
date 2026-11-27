#pragma once

// Dual delay: two independent delay lines, one per side (L and R).
// Each side has its own time (free in ms, or TAP with its own subdivision), voice
// (Digital / Analog / Tape), repeats, tone and level. CROSS sends part of each side's
// repeats into the other side, from 0 (fully separate) to 1 (repeats alternate sides).

#include "effects/Effect.h"

namespace gsp::fx
{

class DualDelay final : public Effect
{
public:
    enum Voice { Digital = 0, Analog, Tape };
    static const std::vector<std::string>& voiceNames();

    DualDelay();

    // Delay time in seconds for side 0 (L) or 1 (R), after tempo sync.
    double targetSeconds (int side) const noexcept;

private:
    void prepareEffect (double, int) override;
    void resetEffect() override;
    void processEffect (float* l, float* r, int n, bool inputMuted) noexcept override;
    bool tailWhenBypassed() const noexcept override { return true; }

    struct Side
    {
        int pVoice, pTime, pDivision, pFeedback, pTone, pLevel;
        DelayLine line;
        OnePole lp, hp;
        Lfo wow, flutter, drift;
        Smoothed timeSmooth;
        float fb = 0.0f;
    };

    int pSync, pMod, pCross;
    std::array<Side, 2> sides;
};

} // namespace gsp::fx
