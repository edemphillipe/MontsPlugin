// Tuner (pitch detection) and whole-board presets.

#include "TestUtil.h"

#include "engine/SignalChain.h"
#include "presets/PresetStore.h"
#include "tuner/PitchDetector.h"

#include <random>

using namespace gsptest;

namespace
{
constexpr double fs = 48000.0;
constexpr double pi = 3.14159265358979323846;

std::vector<float> note (double hz, int n, bool rich, double centsOff = 0.0)
{
    const double f = hz * std::pow (2.0, centsOff / 1200.0);
    std::vector<float> x ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        double s = 0.0;
        const int harmonics = rich ? 10 : 1;
        for (int h = 1; h <= harmonics; ++h)
            s += std::sin (2.0 * pi * f * h * i / fs + h * 0.7) / (h * (rich ? 0.8 : 1.0));
        x[(size_t) i] = (float) (0.2 * s);
    }
    return x;
}

void testTunerStrings()
{
    gsp::PitchDetector d;
    struct S { const char* name; double hz; int octave; };
    const S strings[] = { { "E", 82.41, 2 }, { "A", 110.0, 2 }, { "D", 146.83, 3 }, { "G", 196.0, 3 }, { "B", 246.94, 3 }, { "E", 329.63, 4 },
                          { "D", 73.42, 2 } /* drop D */, { "B", 61.74, 1 } /* 7-string low B */ };
    double worst = 0;
    for (const auto& s : strings)
        for (bool rich : { false, true })
        {
            const auto x = note (s.hz, 4096, rich);
            const auto r = d.detect (x.data(), (int) x.size(), fs);
            CHECK (r.valid, "%s%d not detected", s.name, s.octave);
            CHECK (r.note == s.name && r.octave == s.octave, "%s%d detected as %s%d", s.name, s.octave, r.note.c_str(), r.octave);
            worst = std::max (worst, std::abs (r.cents));
        }
    std::printf ("  8 strings (E2..E4, drop D, low B), pure and harmonic-rich tones: worst error %.2f cents\n", worst);
    CHECK (worst < 1.0, "tuner error %.2f cents", worst);

    // detuned strings read the right offset
    for (double off : { -30.0, -8.0, 5.0, 25.0 })
    {
        const auto x = note (110.0, 4096, true, off);
        const auto r = d.detect (x.data(), (int) x.size(), fs);
        CHECK (r.valid && std::abs (r.cents - off) < 1.0, "A2 %+.0f cents read as %+.2f", off, r.cents);
    }
    std::printf ("  detuned A2 (-30, -8, +5, +25 cents) read within 1 cent\n");

    // silence and noise give no reading
    std::vector<float> silence (4096, 0.0f);
    CHECK (! d.detect (silence.data(), 4096, fs).valid, "pitch in silence");
    std::mt19937 rng (1);
    std::normal_distribution<float> nd (0.0f, 0.2f);
    std::vector<float> noise (4096);
    for (auto& v : noise) v = nd (rng);
    CHECK (! d.detect (noise.data(), 4096, fs).valid, "pitch in white noise");

    // 44.1 kHz works too
    auto x441 = std::vector<float> (4096);
    for (int i = 0; i < 4096; ++i) x441[(size_t) i] = (float) (0.3 * std::sin (2.0 * pi * 82.41 * i / 44100.0));
    const auto r441 = d.detect (x441.data(), 4096, 44100.0);
    CHECK (r441.valid && r441.note == "E" && std::abs (r441.cents) < 1.0, "44.1 kHz low E: %s %.2f", r441.note.c_str(), r441.cents);
}

void testTunerTapAndMute()
{
    gsp::SignalChain chain;
    chain.prepare (fs, 256);
    const auto in = note (196.0, 48000, true);
    std::vector<float> l (256), r (256);
    float* outs[] = { l.data(), r.data() };
    chain.muteOutput = true;
    float lastPeak = 1.0f;
    for (size_t s = 0; s + 256 <= in.size(); s += 256)
    {
        chain.process (in.data() + s, outs, 2, 256);
        lastPeak = 0;
        for (float v : l) lastPeak = std::max (lastPeak, std::abs (v));
    }
    std::vector<float> tail (4096);
    chain.getTunerTap().latest (tail.data(), 4096);
    gsp::PitchDetector d;
    const auto res = d.detect (tail.data(), 4096, chain.getSampleRate());
    std::printf ("  tuner reads the chain input while the output is muted: %s%d %+.2f cents, output peak %.6f\n",
                 res.note.c_str(), res.octave, res.cents, lastPeak);
    CHECK (res.valid && res.note == "G" && res.octave == 3, "tuner tap: %s%d", res.note.c_str(), res.octave);
    CHECK (lastPeak < 1e-6f, "output not muted while tuning (%.6f)", lastPeak);
}

void testPresets()
{
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("gsp_preset_test");
    folder.deleteRecursively();
    gsp::PresetStore store (folder);

    auto* state = new juce::DynamicObject();
    state->setProperty ("x", 42);
    juce::String err;
    CHECK (store.save (juce::String::fromUTF8 ("Clean Worship / Ré"), juce::var (state), err), "save failed: %s", err.toRawUTF8());
    CHECK (store.save ("Lead", juce::var (new juce::DynamicObject()), err), "save failed");
    CHECK (! store.save ("   ", juce::var (), err), "empty name accepted");
    const auto names = store.names();
    CHECK (names.size() == 2 && names[0] == juce::String::fromUTF8 ("Clean Worship  Ré") && names[1] == "Lead",
           "names: %s", names.joinIntoString (", ").toRawUTF8());
    const auto loaded = store.load (names[0]);
    CHECK ((int) loaded["x"] == 42, "content not restored");
    CHECK (store.load ("missing").isVoid(), "missing preset returned data");
    folder.getChildFile ("broken.json").replaceWithText ("{not json");
    CHECK (store.load ("broken").isVoid(), "broken file accepted");
    CHECK (store.remove ("Lead") && store.names().size() == 2, "remove failed"); // broken.json still listed
    std::printf ("  save / list / load / delete, unsafe characters removed, broken files rejected\n");
    folder.deleteRecursively();
}
} // namespace

void runStudioTests()
{
    runTest ("Tuner: guitar strings, detuning, silence and noise", testTunerStrings);
    runTest ("Tuner: reads the chain input, output muted", testTunerTapAndMute);
    runTest ("Presets: save, list, load, delete", testPresets);
}
