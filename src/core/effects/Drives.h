#pragma once

// Pre-amp pedals: compressor, boost, overdrive, distortion, fuzz.
// Original algorithms. Clipping stages run 8x oversampled to avoid aliasing.

#include "amp/ToneStack.h"
#include "effects/Effect.h"

#include <juce_dsp/juce_dsp.h>

namespace gsp::fx
{

class Compressor final : public Effect
{
public:
    Compressor();
private:
    void prepareEffect (double, int) override;
    void resetEffect() override;
    void processEffect (float* l, float* r, int n, bool) noexcept override;
    int pSustain, pAttack, pLevel, pBlend;
    float env = 0.0f, gainState = 1.0f;
};

class Boost final : public Effect
{
public:
    Boost();
private:
    void prepareEffect (double, int) override;
    void resetEffect() override;
    void processEffect (float* l, float* r, int n, bool) noexcept override;
    int pGain, pBright;
    juce::dsp::IIR::Filter<float> shelf;
    float lastBright = -1.0f;
};

// Shared machinery for the three clipping pedals.
class ClippingPedal : public Effect
{
public:
    ClippingPedal (std::string type, std::string displayName);
protected:
    void prepareEffect (double, int) override;
    void resetEffect() override;
    void processEffect (float* l, float* r, int n, bool) noexcept override;

    virtual void updateFilters() noexcept = 0;            // called once per block at base rate
    virtual float shape (float x) noexcept = 0;           // runs at 8x rate
    virtual float preFilter (float x) noexcept { return x; }   // base rate, before oversampling
    virtual float postFilter (float x) noexcept { return x; }  // base rate, after downsampling
    virtual float cleanBlend (float, float wet) noexcept { return wet; }

    int pGain = 0, pTone = 0, pLevel = 0;
    double osRate = 384000.0;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    std::vector<float> scratch;
    Smoothed gainSmooth;
    float lastTone = -1.0f; // forces filter updates after prepare()
};

class Overdrive final : public ClippingPedal
{
public:
    Overdrive();
private:
    void updateFilters() noexcept override;
    float preFilter (float x) noexcept override;
    float shape (float x) noexcept override;
    float postFilter (float x) noexcept override;
    float cleanBlend (float dry, float wet) noexcept override;
    OnePole inputHp, toneLp, cleanLp;
    juce::dsp::IIR::Filter<float> midHump;
    float drive = 1.0f;
};

// British-amp-in-a-box distortion: tightened input, a tube-like asymmetric stage into
// a silicon-diode stage (8x oversampled), and a passive bass/middle/treble tone stack.
class Distortion final : public ClippingPedal
{
public:
    Distortion();
private:
    void resetEffect() override;
    void updateFilters() noexcept override;
    float preFilter (float x) noexcept override;
    float shape (float x) noexcept override;
    float postFilter (float x) noexcept override;
    int pBass = 0, pMiddle = 0, pTreble = 0;
    OnePole tightHp, interLp, interDc, fizzLp1, fizzLp2;
    amp::ToneStack toneStack;
    float lastBass = -1.0f, lastMiddle = -1.0f, lastTreble = -1.0f;
    float drive1 = 1.0f, drive2 = 1.0f, makeup = 1.0f;
    double preparedRate = 0.0;
};

class Fuzz final : public ClippingPedal
{
public:
    Fuzz();
private:
    void updateFilters() noexcept override;
    float preFilter (float x) noexcept override;
    float shape (float x) noexcept override;
    float postFilter (float x) noexcept override;
    OnePole inputHp, interLp, toneLp, toneHp, dcBlock;
    float drive = 1.0f, toneMix = 0.5f;
};

} // namespace gsp::fx
