#include "amp/AmpSim.h"

#include <cmath>

namespace gsp::amp
{
namespace
{
constexpr double pF = 1e-12, nF = 1e-9, k = 1e3, M = 1e6;

// Tone stack part values per amp family (classic published values for each circuit type).
constexpr ToneStackParts americanStack { 250 * pF, 20 * nF, 20 * nF, 250 * k, 1 * M, 25 * k, 56 * k };
constexpr ToneStackParts britishStack  { 470 * pF, 22 * nF, 22 * nF, 220 * k, 1 * M, 22 * k, 33 * k };
constexpr ToneStackParts chimeStack    { 470 * pF, 22 * nF, 22 * nF, 220 * k, 1 * M, 22 * k, 56 * k };
constexpr ToneStackParts thickStack    { 470 * pF, 47 * nF, 22 * nF, 250 * k, 1 * M, 25 * k, 33 * k };
constexpr ToneStackParts hotRodStack   { 470 * pF, 20 * nF, 20 * nF, 250 * k, 1 * M, 25 * k, 47 * k };
constexpr ToneStackParts modernStack   { 250 * pF, 100 * nF, 47 * nF, 250 * k, 250 * k, 25 * k, 100 * k };

// id, name, description, stages, inHpf, bright, g1, g, maxGainDb, bias, isHpf, isLpf, stack,
// powerDrive, powerAsym, sag, resHz, resDb, presenceHz, trimDb
const Voicing voicings[] = {
    { "american_clean", "AMERICANO LIMPO", "Limpo cristalino, graves cheios e m\xc3\xa9" "dios recortados",
      2, 30.0f, 6.0f, 1.5f, 3.0f, 6.0f, 0.15f, 20.0f, 9000.0f, americanStack, 1.2f, 0.0f, 0.6f, 90.0f, 3.0f, 3500.0f, -5.7f },
    { "brit_crunch", "BRIT CRUNCH", "Crunch brit\xc3\xa2nico com m\xc3\xa9" "dios na cara",
      3, 60.0f, 3.0f, 2.0f, 4.0f, 6.0f, 0.25f, 40.0f, 7000.0f, britishStack, 2.0f, 0.0f, 0.4f, 100.0f, 2.0f, 3000.0f, -16.1f },
    { "chime_class_a", "CHIME CLASSE A", "Brilho e compress\xc3\xa3o de classe A",
      2, 50.0f, 4.0f, 2.0f, 4.0f, 6.0f, 0.3f, 30.0f, 10000.0f, chimeStack, 3.0f, 0.6f, 0.8f, 110.0f, 1.0f, 4000.0f, -10.9f },
    { "brit_thick", "BRIT ESPESSO", "Satura\xc3\xa7\xc3\xa3o grossa e escura, muitos m\xc3\xa9" "dios-graves",
      4, 45.0f, 0.0f, 2.0f, 4.5f, 6.0f, 0.35f, 35.0f, 5500.0f, thickStack, 2.5f, 0.2f, 0.5f, 90.0f, 3.0f, 2500.0f, -16.9f },
    { "hotrod_lead", "LEAD HOT-ROD", "Lead cantado, ganho alto com defini\xc3\xa7\xc3\xa3o",
      4, 90.0f, 2.0f, 2.5f, 5.5f, 9.0f, 0.3f, 60.0f, 6500.0f, hotRodStack, 1.8f, 0.0f, 0.3f, 100.0f, 3.0f, 3200.0f, -17.5f },
    { "modern_high_gain", "MODERNO ALTO GANHO", "Alto ganho moderno, graves apertados e agressivo",
      5, 120.0f, 0.0f, 2.5f, 3.5f, 9.0f, 0.25f, 80.0f, 6000.0f, modernStack, 1.6f, 0.0f, 0.25f, 85.0f, 4.0f, 3500.0f, -15.2f },
};

inline float dbToGain (float db) noexcept { return std::pow (10.0f, db / 20.0f); }

// Asymmetric triode-like transfer: soft limit at +1 (grid conduction), later and harder at -1.6 (cutoff).
inline float triode (float v) noexcept
{
    return v >= 0.0f ? std::tanh (v) : 1.6f * std::tanh (v * (1.0f / 1.6f));
}
} // namespace

int numVoicings() noexcept { return (int) (sizeof (voicings) / sizeof (voicings[0])); }
const Voicing& voicing (int index) noexcept { return voicings[juce::jlimit (0, numVoicings() - 1, index)]; }
int voicingIndexForId (const juce::String& id) noexcept
{
    for (int i = 0; i < numVoicings(); ++i)
        if (id == voicings[i].id)
            return i;
    return -1;
}

AmpSim::AmpSim() = default;
AmpSim::~AmpSim() = default;

void AmpSim::prepare (double sampleRate, int maxBlockSize)
{
    fs = sampleRate;
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        1, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
    oversampler->initProcessing ((size_t) maxBlockSize);
    osFs = fs * 4.0;
    sagAttack = 1.0f - std::exp (-1.0f / (0.01f * (float) osFs));
    sagRelease = 1.0f - std::exp (-1.0f / (0.15f * (float) osFs));
    activeVoicing = -1;
    reset();
}

void AmpSim::reset()
{
    if (oversampler)
        oversampler->reset();
    for (auto& s : stages)
    {
        s.hpf.reset();
        s.lpf.reset();
    }
    for (auto* b : { &inputHpf, &bright, &resonance, &presenceShelf, &outputLpf, &outputHpf })
        b->reset();
    toneStack.reset();
    sagEnv = 0.0f;
}

int AmpSim::latencySamples() const noexcept
{
    return oversampler ? (int) std::lround (oversampler->getLatencyInSamples()) : 0;
}

void AmpSim::updateCoefficients (const Voicing& v, bool force) noexcept
{
    using T = Biquad::Type;
    const float t = treble.load (std::memory_order_relaxed), m = middle.load (std::memory_order_relaxed),
                b = bass.load (std::memory_order_relaxed), p = presence.load (std::memory_order_relaxed),
                g = gain.load (std::memory_order_relaxed);

    if (force)
    {
        inputHpf.setup (T::HighPass, osFs, v.inputHpfHz, 0.707, 0.0);
        for (int i = 0; i < (int) stages.size(); ++i)
        {
            stages[(size_t) i].hpf.setup (T::HighPass, osFs, v.interstageHpfHz, 0.6, 0.0);
            stages[(size_t) i].lpf.setup (T::LowPass, osFs, v.interstageLpfHz * (i == 0 ? 1.5f : 1.0f), 0.6, 0.0);
            stages[(size_t) i].dcOffset = triode (v.bias);
        }
        resonance.setup (T::Peak, osFs, v.resonanceHz, 0.8, v.resonanceDb);
        outputHpf.setup (T::HighPass, osFs, 30.0, 0.707, 0.0);
        outputLpf.setup (T::LowPass, osFs, 14000.0, 0.707, 0.0);
    }
    if (force || ! juce::exactlyEqual (g, lastGain))
    {
        const float k01 = g / 10.0f;
        bright.setup (T::HighShelf, osFs, 2500.0, 0.6, v.brightDb * (1.0f - 0.7f * k01));
        gainPotTarget = juce::jmax (0.001f, k01 * k01 * k01) * dbToGain (v.maxGainDb);
        lastGain = g;
    }
    if (force || ! juce::exactlyEqual (p, lastPresence))
    {
        presenceShelf.setup (T::HighShelf, osFs, v.presenceHz, 0.7, -4.0 + 1.3 * p);
        lastPresence = p;
    }
    if (force || ! juce::exactlyEqual (t, lastTreble) || ! juce::exactlyEqual (m, lastMiddle) || ! juce::exactlyEqual (b, lastBass))
    {
        toneStack.setup (v.toneStack, osFs, t / 10.0, m / 10.0, b / 10.0);
        lastTreble = t; lastMiddle = m; lastBass = b;
    }
}

void AmpSim::process (float* x, int numSamples) noexcept
{
    process (x, numSamples, voicingIndex.load (std::memory_order_relaxed));
}

void AmpSim::process (float* x, int numSamples, int requestedVoicing) noexcept
{
    if (oversampler == nullptr || numSamples <= 0)
        return;

    const int vi = juce::jlimit (0, numVoicings() - 1, requestedVoicing);
    const Voicing& v = voicings[vi];
    const bool voicingChanged = vi != activeVoicing;
    if (voicingChanged)
    {
        reset();
        activeVoicing = vi;
    }
    updateCoefficients (v, voicingChanged);
    if (voicingChanged)
        gainPot = gainPotTarget;

    // midband loss of the passive tone stack is made up so the knobs only shape the tone
    const float stackMakeup = 10.0f; // +20 dB

    const float vol = volume.load (std::memory_order_relaxed) / 10.0f;
    const float volTarget = 1.6f * vol * vol;
    const float mas = master.load (std::memory_order_relaxed) / 10.0f;
    const float masterTarget = 4.0f * mas * mas * dbToGain (v.outputTrimDb);

    float* channels[] = { x };
    juce::dsp::AudioBlock<float> block (channels, 1, (size_t) numSamples);
    auto os = oversampler->processSamplesUp (block);
    float* y = os.getChannelPointer (0);
    const int n = (int) os.getNumSamples();

    const float smooth = 1.0f - std::exp (-1.0f / (0.02f * (float) osFs));
    const int numStages = juce::jlimit (1, (int) stages.size(), v.stages);

    for (int i = 0; i < n; ++i)
    {
        gainPot += smooth * (gainPotTarget - gainPot);
        volumeGain += smooth * (volTarget - volumeGain);

        float s = bright.process (inputHpf.process (y[i]));

        // preamp
        for (int st = 0; st < numStages; ++st)
        {
            auto& stage = stages[(size_t) st];
            const float drive = st == 0 ? v.firstStageGain : v.stageGain;
            s = triode (s * drive + v.bias) - stage.dcOffset;
            s = stage.hpf.process (stage.lpf.process (s));
            if (st == 0)
                s *= gainPot;
        }

        // tone stack + volume
        s = (float) toneStack.process (s) * stackMakeup * volumeGain;

        // power amp: presence/resonance shape what the feedback loop sees; supply sag lowers the drive
        float p = presenceShelf.process (resonance.process (s)) * v.powerDrive / (1.0f + v.sag * sagEnv);
        float out = p >= 0.0f ? std::tanh (p)
                              : std::tanh (p * (1.0f + v.powerAsymmetry)) / (1.0f + v.powerAsymmetry);
        const float a = std::abs (out);
        sagEnv += (a > sagEnv ? sagAttack : sagRelease) * (a - sagEnv);
        y[i] = outputLpf.process (outputHpf.process (out));
    }

    oversampler->processSamplesDown (block);

    const float masterSmooth = 1.0f - std::exp (-1.0f / (0.02f * (float) fs));
    for (int i = 0; i < numSamples; ++i)
    {
        masterGain += masterSmooth * (masterTarget - masterGain);
        x[i] *= masterGain;
    }
}

} // namespace gsp::amp
