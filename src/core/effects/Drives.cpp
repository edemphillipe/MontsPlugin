#include "effects/Drives.h"

namespace gsp::fx
{

// ============================================================ Compressor
// Feed-forward, program-dependent release, soft knee. "Sustain" lowers the threshold.
Compressor::Compressor() : Effect ("compressor", "Compressor", Placement::PreAmp)
{
    pSustain = addParam ({ "sustain", "SUSTAIN", 0.0f, 1.0f, 0.5f, "" });
    pAttack  = addParam ({ "attack", "ATTACK", 1.0f, 50.0f, 10.0f, "ms" });
    pLevel   = addParam ({ "level", "LEVEL", -12.0f, 12.0f, 0.0f, "dB" });
    pBlend   = addParam ({ "blend", "BLEND", 0.0f, 1.0f, 1.0f, "" });
}
void Compressor::prepareEffect (double, int) {}
void Compressor::resetEffect() { env = 0.0f; gainState = 1.0f; }

void Compressor::processEffect (float* l, float* r, int n, bool) noexcept
{
    const float thresholdDb = -10.0f - 35.0f * get (pSustain);
    const float ratio = 4.0f;
    const float knee = 6.0f;
    const float attack = 1.0f - std::exp (-1.0f / (0.001f * get (pAttack) * (float) fs));
    const float releaseFast = 1.0f - std::exp (-1.0f / (0.08f * (float) fs));
    const float releaseSlow = 1.0f - std::exp (-1.0f / (0.6f * (float) fs));
    const float makeup = dbToGain (get (pLevel) + 0.5f * (-thresholdDb) * (1.0f - 1.0f / ratio) * 0.6f);
    const float blend = get (pBlend);

    for (int i = 0; i < n; ++i)
    {
        const float x = l[i];
        const float a = std::abs (x);
        env += (a > env ? attack : releaseFast) * (a - env);
        const float levelDb = juce::Decibels::gainToDecibels (env, -100.0f);
        const float over = levelDb - thresholdDb;
        float reductionDb = 0.0f;
        if (over > knee * 0.5f)       reductionDb = over * (1.0f - 1.0f / ratio);
        else if (over > -knee * 0.5f) reductionDb = (1.0f - 1.0f / ratio) * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.0f * knee);
        const float target = dbToGain (-reductionDb);
        // gain recovers slower than it falls (opto-like smoothness)
        gainState += (target < gainState ? 0.5f : releaseSlow * 4.0f) * (target - gainState);
        const float y = x * gainState * makeup;
        l[i] = x + blend * (y - x);
    }
    std::copy (l, l + n, r);
}

// ============================================================ Boost
Boost::Boost() : Effect ("boost", "Boost", Placement::PreAmp)
{
    pGain   = addParam ({ "gain", "GAIN", 0.0f, 20.0f, 6.0f, "dB" });
    pBright = addParam ({ "bright", "BRIGHT", -6.0f, 6.0f, 0.0f, "dB" });
}
void Boost::prepareEffect (double, int) { lastBright = -100.0f; }
void Boost::resetEffect() { shelf.reset(); }
void Boost::processEffect (float* l, float* r, int n, bool) noexcept
{
    const float b = get (pBright);
    if (! juce::exactlyEqual (b, lastBright))
    {
        *shelf.coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (fs, 2000.0, 0.6, dbToGain (b));
        lastBright = b;
    }
    const float g = dbToGain (get (pGain));
    for (int i = 0; i < n; ++i)
        l[i] = shelf.processSample (l[i]) * g;
    std::copy (l, l + n, r);
}

// ============================================================ Clipping pedals
ClippingPedal::ClippingPedal (std::string type, std::string displayName)
    : Effect (std::move (type), std::move (displayName), Placement::PreAmp) {}

void ClippingPedal::prepareEffect (double sampleRate, int maxBlock)
{
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        1, 3, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
    oversampler->initProcessing ((size_t) maxBlock);
    osRate = sampleRate * 8.0;
    scratch.assign ((size_t) maxBlock, 0.0f);
    gainSmooth.prepare (sampleRate, 30.0, get (pGain));
    lastTone = -1.0f;
    updateFilters();
}

void ClippingPedal::resetEffect()
{
    if (oversampler)
        oversampler->reset();
}

void ClippingPedal::processEffect (float* l, float* r, int n, bool) noexcept
{
    updateFilters();
    std::copy (l, l + n, scratch.begin());

    for (int i = 0; i < n; ++i)
        l[i] = preFilter (l[i]);

    float* ch[] = { l };
    juce::dsp::AudioBlock<float> block (ch, 1, (size_t) n);
    auto up = oversampler->processSamplesUp (block);
    float* u = up.getChannelPointer (0);
    const int un = (int) up.getNumSamples();
    for (int i = 0; i < un; ++i)
        u[i] = shape (u[i]);
    oversampler->processSamplesDown (block);

    const float level = dbToGain (get (pLevel));
    for (int i = 0; i < n; ++i)
        l[i] = cleanBlend (scratch[(size_t) i], postFilter (l[i])) * level;
    std::copy (l, l + n, r);
}

// ------------------------------------------------------------ Overdrive
// Mid-focused soft clipper: low end is kept out of the clipper (tight) and a clean
// low/mid path is blended back, like classic green overdrives, but with our own curve.
Overdrive::Overdrive() : ClippingPedal ("overdrive", "Overdrive")
{
    pGain  = addParam ({ "drive", "DRIVE", 0.0f, 1.0f, 0.4f, "" });
    pTone  = addParam ({ "tone", "TONE", 0.0f, 1.0f, 0.5f, "" });
    pLevel = addParam ({ "level", "LEVEL", -24.0f, 12.0f, -6.0f, "dB" });
}
void Overdrive::updateFilters() noexcept
{
    const float t = get (pTone);
    if (! juce::exactlyEqual (t, lastTone))
    {
        inputHp.setLowpass (fs, 720.0);
        cleanLp.setLowpass (fs, 720.0);
        toneLp.setLowpass (fs, 900.0 * std::pow (8.0, (double) t));
        *midHump.coefficients = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (fs, 900.0, 0.7, 1.6f);
        lastTone = t;
    }
    drive = 2.0f + 120.0f * std::pow (get (pGain), 2.0f);
}
float Overdrive::preFilter (float x) noexcept { return inputHp.hp (x) * drive; }
float Overdrive::shape (float x) noexcept
{
    // asymmetric soft clip (diode pair with slightly different knees)
    return x >= 0.0f ? std::tanh (x) : std::tanh (0.85f * x) / 0.85f * 0.92f;
}
float Overdrive::postFilter (float x) noexcept { return toneLp.lp (midHump.processSample (x)) * 0.6f; }
float Overdrive::cleanBlend (float dry, float wet) noexcept { return wet + 0.5f * cleanLp.lp (dry); }

// ------------------------------------------------------------ Distortion
namespace
{
// passive tone stack with British-amp part values (same circuit model as the amps)
constexpr amp::ToneStackParts crunchStack { 470e-12, 22e-9, 22e-9, 220e3, 1e6, 22e3, 33e3 };
}

Distortion::Distortion() : ClippingPedal ("distortion", "Distortion")
{
    pGain   = addParam ({ "gain", "GAIN", 0.0f, 1.0f, 0.55f, "" });
    pBass   = addParam ({ "bass", "BASS", 0.0f, 1.0f, 0.5f, "" });
    pMiddle = addParam ({ "middle", "MIDDLE", 0.0f, 1.0f, 0.65f, "" });
    pTreble = addParam ({ "treble", "TREBLE", 0.0f, 1.0f, 0.55f, "" });
    pLevel  = addParam ({ "level", "LEVEL", -30.0f, 6.0f, -8.0f, "dB" });
}

void Distortion::resetEffect()
{
    ClippingPedal::resetEffect();
    for (auto* f : { &tightHp, &interLp, &interDc, &fizzLp1, &fizzLp2 })
        f->reset();
    toneStack.reset();
}

void Distortion::updateFilters() noexcept
{
    const float b = get (pBass), m = get (pMiddle), t = get (pTreble);
    if (! juce::exactlyEqual (preparedRate, fs))
    {
        preparedRate = fs;
        lastBass = -1.0f; // recompute the tone stack for the new rate
        tightHp.setLowpass (fs, 380.0);   // lows mostly kept out of the clipping: tight, no flub
        interLp.setLowpass (osRate, 7000.0);
        interDc.setLowpass (osRate, 25.0);
        fizzLp1.setLowpass (fs, 6500.0);  // two poles above the tone stack: no fizz
        fizzLp2.setLowpass (fs, 9000.0);
        // the tone stack loses ~-15 dB in the mids; make it unity at noon / 1 kHz
        const auto a = amp::ToneStack::analog (crunchStack, 0.5, 0.5, 0.5);
        // ... and bring the clipped signal (about +13 dB hotter than a guitar) back near the dry level
        makeup = (float) (0.23 / juce::jmax (1.0e-3, amp::ToneStack::analogMagnitude (a, 1000.0)));
    }
    if (! juce::exactlyEqual (b, lastBass) || ! juce::exactlyEqual (m, lastMiddle) || ! juce::exactlyEqual (t, lastTreble))
    {
        toneStack.setup (crunchStack, fs, t, m, b);
        lastBass = b;
        lastMiddle = m;
        lastTreble = t;
    }
    const float g = get (pGain);
    drive1 = 1.5f + 40.0f * g * g;   // first (tube-like) stage
    drive2 = 2.0f + 18.0f * g;       // second (diode) stage
}

float Distortion::preFilter (float x) noexcept
{
    // shelf: highs pass fully, lows at about -10 dB, so palm mutes stay tight
    const float high = tightHp.hp (x);
    return (high + 0.3f * (x - high)) * drive1;
}

float Distortion::shape (float x) noexcept
{
    // stage 1: asymmetric soft saturation (even harmonics, like a triode)
    constexpr float bias = 0.25f;
    float y = std::tanh (x + bias) - std::tanh (bias);
    y = interDc.hp (interLp.lp (y)) * drive2;
    // stage 2: silicon diodes to ground, firm with a rounded knee
    const float a = std::abs (y);
    const float c = a < 0.5f ? a : 0.5f + 0.35f * std::tanh ((a - 0.5f) / 0.35f);
    return std::copysign (c, y);
}

float Distortion::postFilter (float x) noexcept
{
    x = fizzLp2.lp (fizzLp1.lp (x));
    return (float) toneStack.process ((double) x) * makeup;
}

// ------------------------------------------------------------ Fuzz
// Two cascaded transistor-like stages with a scooped passive tone stack.
Fuzz::Fuzz() : ClippingPedal ("fuzz", "Fuzz")
{
    pGain  = addParam ({ "fuzz", "FUZZ", 0.0f, 1.0f, 0.6f, "" });
    pTone  = addParam ({ "tone", "TONE", 0.0f, 1.0f, 0.5f, "" });
    pLevel = addParam ({ "level", "LEVEL", -30.0f, 6.0f, -12.0f, "dB" });
}
void Fuzz::updateFilters() noexcept
{
    const float t = get (pTone);
    if (! juce::exactlyEqual (t, lastTone))
    {
        inputHp.setLowpass (fs, 90.0);
        interLp.setLowpass (osRate, 8000.0);
        toneLp.setLowpass (fs, 700.0);
        toneHp.setLowpass (fs, 1100.0);
        dcBlock.setLowpass (fs, 15.0);
        toneMix = t;
        lastTone = t;
    }
    drive = 1.0f + 900.0f * std::pow (get (pGain), 2.5f);
}
float Fuzz::preFilter (float x) noexcept { return inputHp.hp (x) * drive; }
float Fuzz::shape (float x) noexcept
{
    // stage 1: asymmetric transistor saturation (bias shifts with level)
    float y = std::tanh (x + 0.15f) - std::tanh (0.15f);
    y = interLp.lp (y) * 3.0f;
    // stage 2: harder clip
    return juce::jlimit (-1.0f, 1.0f, 1.5f * y - 0.5f * y * y * y);
}
float Fuzz::postFilter (float x) noexcept
{
    x = dcBlock.hp (x);
    const float low = toneLp.lp (x), high = toneHp.hp (x);
    return ((1.0f - toneMix) * low + toneMix * high) * 1.3f; // mid scoop between the two
}

} // namespace gsp::fx
