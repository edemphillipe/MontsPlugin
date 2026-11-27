#pragma once

// Stereo modulation pedals: chorus, flanger, phaser, tremolo. Rates can follow the TAP tempo.

#include "effects/Effect.h"

namespace gsp::fx
{

class Chorus final : public Effect
{
public:
    Chorus();
private:
    void prepareEffect (double, int) override;
    void resetEffect() override;
    void processEffect (float* l, float* r, int n, bool) noexcept override;
    int pRate, pDepth, pMix, pTone;
    DelayLine dl, dr;
    Lfo lfo;
    OnePole wetLpL, wetLpR;
};

class Flanger final : public Effect
{
public:
    Flanger();
private:
    void prepareEffect (double, int) override;
    void resetEffect() override;
    void processEffect (float* l, float* r, int n, bool) noexcept override;
    int pRate, pDepth, pManual, pRegen, pMix;
    DelayLine dl, dr;
    Lfo lfo;
    float fbL = 0.0f, fbR = 0.0f;
};

class Phaser final : public Effect
{
public:
    Phaser();
private:
    void prepareEffect (double, int) override;
    void resetEffect() override;
    void processEffect (float* l, float* r, int n, bool) noexcept override;
    int pRate, pDepth, pFeedback, pStages, pMix;
    static constexpr int maxStages = 8;
    float zl[maxStages] {}, zr[maxStages] {};
    float fbL = 0.0f, fbR = 0.0f;
    Lfo lfo;
};

class Tremolo final : public Effect
{
public:
    Tremolo();
private:
    void prepareEffect (double, int) override;
    void resetEffect() override;
    void processEffect (float* l, float* r, int n, bool) noexcept override;
    int pRate, pDepth, pShape, pSync, pDivision, pStereo;
    Lfo lfo;
    Smoothed shapeSmooth;
};

} // namespace gsp::fx
