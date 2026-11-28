// Phase 2 tests: pedals, delay timing and tempo sync, reverb decay, stability, aliasing,
// bypass, pedal order and presets, CPU budget.

#include "TestUtil.h"

#include "effects/Delay.h"
#include "effects/DualDelay.h"
#include "effects/Drives.h"
#include "effects/Modulation.h"
#include "effects/Reverb.h"
#include "engine/Pedalboard.h"

#include <juce_dsp/juce_dsp.h>

#include <chrono>

using namespace gsptest;
using gsp::fx::Effect;

namespace
{
constexpr double fs = 48000.0;
constexpr int block = 128;

struct Stereo
{
    std::vector<float> l, r;
};

Stereo run (Effect& fx, const std::vector<float>& in, int blockSize = block)
{
    Stereo out { in, in };
    for (size_t s = 0; s < in.size(); s += (size_t) blockSize)
    {
        const int n = (int) std::min ((size_t) blockSize, in.size() - s);
        fx.process (out.l.data() + s, out.r.data() + s, n);
    }
    return out;
}

std::vector<float> impulse (int length, int at = 0)
{
    std::vector<float> x ((size_t) length, 0.0f);
    x[(size_t) at] = 1.0f;
    return x;
}

std::vector<float> sine (int length, double hz, float amp)
{
    std::vector<float> x ((size_t) length);
    for (int i = 0; i < length; ++i)
        x[(size_t) i] = amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * i / fs);
    return x;
}

float peak (const std::vector<float>& v, size_t from = 0, size_t to = 0)
{
    if (to == 0 || to > v.size()) to = v.size();
    float p = 0;
    for (size_t i = from; i < to; ++i) p = std::max (p, std::abs (v[i]));
    return p;
}

size_t argmaxAbs (const std::vector<float>& v, size_t from, size_t to)
{
    size_t best = from;
    for (size_t i = from; i < std::min (to, v.size()); ++i)
        if (std::abs (v[i]) > std::abs (v[best])) best = i;
    return best;
}

// Harmonic-distortion proxy: energy left after removing the best-fit fundamental.
double residualRatioDb (const std::vector<float>& y, double hz, size_t from)
{
    double ss = 0, cc = 0, sc = 0, ys = 0, yc = 0, mean = 0;
    for (size_t i = from; i < y.size(); ++i) mean += y[i];
    mean /= (double) (y.size() - from);
    for (size_t i = from; i < y.size(); ++i)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * hz * (double) i / fs;
        const double s = std::sin (w), c = std::cos (w), v = y[i] - mean;
        ss += s * s; cc += c * c; sc += s * c; ys += v * s; yc += v * c;
    }
    const double det = ss * cc - sc * sc;
    const double A = (ys * cc - yc * sc) / det, B = (yc * ss - ys * sc) / det;
    double e = 0, f = 0;
    for (size_t i = from; i < y.size(); ++i)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * hz * (double) i / fs;
        const double fit = A * std::sin (w) + B * std::cos (w);
        e += (y[i] - mean - fit) * (y[i] - mean - fit);
        f += fit * fit;
    }
    return 10.0 * std::log10 (std::max (e, 1e-30) / std::max (f, 1e-30));
}

// Energy at non-harmonic frequencies (aliasing) relative to total, via FFT.
double aliasRatioDb (const std::vector<float>& y, double hz, size_t from)
{
    const int order = 15, size = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> buf ((size_t) size * 2, 0.0f);
    for (int i = 0; i < size; ++i)
    {
        const float w = 0.5f - 0.5f * (float) std::cos (2.0 * juce::MathConstants<double>::pi * i / (size - 1));
        buf[(size_t) i] = y[from + (size_t) i] * w;
    }
    fft.performFrequencyOnlyForwardTransform (buf.data());
    double harmonic = 0, alias = 0;
    for (int k = 2; k < size / 2; ++k)
    {
        const double f = k * fs / size;
        const double ratio = f / hz;
        const double dist = std::abs (ratio - std::round (ratio)) * hz; // Hz from nearest harmonic
        const double e = (double) buf[(size_t) k] * buf[(size_t) k];
        if (f < 20.0) continue;
        (dist < 30.0 ? harmonic : alias) += e;
    }
    return 10.0 * std::log10 (std::max (alias, 1e-30) / std::max (harmonic, 1e-30));
}

// Schroeder backward integration; returns T60 extrapolated from the -5..-25 dB range.
double measureT60 (const std::vector<float>& l, const std::vector<float>& r, size_t from)
{
    std::vector<double> e (l.size() - from);
    double acc = 0;
    for (size_t i = l.size(); i-- > from;)
    {
        acc += (double) l[i] * l[i] + (double) r[i] * r[i];
        e[i - from] = acc;
    }
    const double total = e[0];
    int t5 = -1, t25 = -1;
    for (size_t i = 0; i < e.size(); ++i)
    {
        const double db = 10.0 * std::log10 (e[i] / total);
        if (t5 < 0 && db <= -5.0) t5 = (int) i;
        if (t25 < 0 && db <= -25.0) { t25 = (int) i; break; }
    }
    if (t5 < 0 || t25 < 0) return -1.0;
    return (t25 - t5) / fs * 3.0; // 20 dB span -> 60 dB
}

template <typename T>
std::unique_ptr<T> make()
{
    auto p = std::make_unique<T>();
    p->prepare (fs, block);
    return p;
}

// --------------------------------------------------------------------------------------------
void testAllPedalsStable()
{
    gsp::Pedalboard board;
    board.prepare (fs, block);
    const auto in = guitarLikeSignal ((int) fs * 2, fs);

    for (int i = 0; i < board.size(); ++i)
    {
        auto& fx = board.pedal (i);
        // every selectable mode of every selector
        int modeParam = fx.indexOf ("mode");
        const int modes = modeParam >= 0 ? (int) fx.getParams()[(size_t) modeParam].choices.size() : 1;
        for (int m = 0; m < modes; ++m)
        {
            if (modeParam >= 0) fx.set (modeParam, (float) m);
            fx.enabled = true;
            fx.prepare (fs, block);
            const auto out = run (fx, in);
            const float pk = std::max (peak (out.l), peak (out.r));
            const std::string label = fx.getName() + (modeParam >= 0 ? " / " + fx.getParams()[(size_t) modeParam].choices[(size_t) m] : "");
            CHECK (allFinite (out.l) && allFinite (out.r), "%s: NaN/Inf", label.c_str());
            CHECK (rms (out.l) > 1e-3, "%s: silent", label.c_str());
            CHECK (pk < 4.0f, "%s: peak %.2f", label.c_str(), pk);
        }
        if (modeParam >= 0) fx.set (modeParam, 0.0f);
    }
    std::printf ("  %d pedals, every mode: finite, audible, bounded\n", board.size());
}

void testBypassIsTransparent()
{
    gsp::Pedalboard board;
    board.prepare (fs, block);
    const auto in = guitarLikeSignal ((int) fs, fs);
    for (int i = 0; i < board.size(); ++i)
    {
        auto& fx = board.pedal (i);
        fx.enabled = false;
        fx.prepare (fs, block);
        const auto out = run (fx, in);
        CHECK (maxAbsDiff (out.l, in) < 1e-7f && maxAbsDiff (out.r, in) < 1e-7f,
               "%s bypassed changes the signal", fx.getName().c_str());
    }
}

void testSwitchingIsClickFree()
{
    // Toggle a heavy pedal on and off repeatedly on a quiet sine; the 20 ms crossfade must keep
    // the sample-to-sample step close to what the signal itself has.
    auto fuzz = make<gsp::fx::Fuzz>();
    fuzz->enabled = false;
    auto in = sine ((int) fs, 220.0, 0.1f);
    Stereo out { in, in };
    for (size_t s = 0; s < in.size(); s += block)
    {
        if ((s / block) % 40 == 20) fuzz->enabled = ! fuzz->enabled.load();
        fuzz->process (out.l.data() + s, out.r.data() + s, block);
    }
    auto maxStepOf = [] (const std::vector<float>& v)
    {
        float m = 0;
        for (size_t i = 1; i < v.size(); ++i) m = std::max (m, std::abs (v[i] - v[i - 1]));
        return m;
    };
    // reference: the fuzz's own square-ish edges with the pedal simply left on
    auto steady = make<gsp::fx::Fuzz>();
    steady->enabled = true;
    const auto on = run (*steady, in);
    const float toggling = maxStepOf (out.l), reference = maxStepOf (on.l);
    std::printf ("  largest step: %.3f while toggling vs %.3f with the pedal left on\n", toggling, reference);
    CHECK (toggling <= reference * 1.1f + 0.01f, "click when toggling (%.3f vs %.3f)", toggling, reference);
}

void testDelayTiming()
{
    auto d = make<gsp::fx::Delay>();
    d->enabled = true;
    d->set ("mix", 1.0f);
    d->set ("feedback", 0.0f);
    d->set ("width", 0.0f);
    d->set ("tone", 1.0f);
    d->set ("mod", 0.0f);
    d->set ("time", 250.0f);
    d->reset();
    auto out = run (*d, impulse ((int) fs));
    const size_t hit = argmaxAbs (out.l, 100, out.l.size());
    std::printf ("  free 250 ms -> echo at %zu samples (expected %d)\n", hit, (int) (0.25 * fs));
    CHECK (std::abs ((long) hit - (long) (0.25 * fs)) <= 3, "echo at %zu", hit);

    // Tap tempo: 120 BPM, dotted eighth = 0.375 s
    gsp::fx::TempoClock clock;
    clock.bpm = 120.0;
    d->setTempoClock (&clock);
    d->set ("sync", 1.0f);
    d->set ("division", (float) gsp::fx::Subdivision::DottedEighth);
    d->reset();
    out = run (*d, impulse ((int) fs));
    const size_t hit2 = argmaxAbs (out.l, 100, out.l.size());
    std::printf ("  TAP 120 BPM, 1/8 pontuada -> echo at %zu samples (expected %d)\n", hit2, (int) (0.375 * fs));
    CHECK (std::abs ((long) hit2 - (long) (0.375 * fs)) <= 3, "synced echo at %zu", hit2);
    CHECK (std::abs (d->targetSeconds() - 0.375) < 1e-9, "targetSeconds %f", d->targetSeconds());
}

void testPingPong()
{
    auto d = make<gsp::fx::Delay>();
    d->enabled = true;
    d->set ("mode", (float) gsp::fx::Delay::PingPong);
    d->set ("mix", 1.0f);
    d->set ("feedback", 0.7f);
    d->set ("tone", 1.0f);
    d->set ("mod", 0.0f);
    d->set ("time", 200.0f);
    d->reset();
    const auto out = run (*d, impulse ((int) fs));
    const size_t t = (size_t) (0.2 * fs);
    const float l1 = peak (out.l, t - 50, t + 50), r1 = peak (out.r, t - 50, t + 50);
    const float l2 = peak (out.l, 2 * t - 50, 2 * t + 50), r2 = peak (out.r, 2 * t - 50, 2 * t + 50);
    std::printf ("  echo 1: L=%.3f R=%.3f   echo 2: L=%.3f R=%.3f\n", l1, r1, l2, r2);
    CHECK (l1 > 0.1f && r1 < 0.01f, "first repeat should be left only");
    CHECK (r2 > 0.05f && l2 < 0.01f, "second repeat should be right only");
}

void testReverseDelay()
{
    // A plucked note (sharp attack, slow decay) played backwards swells up and stops abruptly.
    auto d = make<gsp::fx::Delay>();
    d->enabled = true;
    d->set ("mode", (float) gsp::fx::Delay::Reverse);
    d->set ("mix", 1.0f);
    d->set ("feedback", 0.0f);
    d->set ("time", 500.0f);
    d->reset();
    std::vector<float> in ((size_t) (fs * 3), 0.0f);
    for (int i = 0; i < (int) (0.4 * fs); ++i)
        in[(size_t) i] = 0.5f * (float) std::exp (-i / (0.08 * fs)) * (float) std::sin (2 * juce::MathConstants<double>::pi * 330 * i / fs);
    // at mix = 1 the dry gain is 0.5: remove it to keep only the reversed repeats
    auto wet = run (*d, in);
    for (size_t i = 0; i < in.size(); ++i) wet.l[i] -= 0.5f * in[i];
    // energy in the first vs second half of each 250 ms window after the note
    double rising = 0, falling = 0;
    const size_t a = (size_t) (0.5 * fs), b = (size_t) (1.5 * fs);
    std::vector<double> env;
    for (size_t s = a; s < b; s += 480) env.push_back (rms (wet.l, s, s + 480));
    const size_t maxAt = (size_t) std::distance (env.begin(), std::max_element (env.begin(), env.end()));
    for (size_t k = 1; k <= maxAt && k < env.size(); ++k) rising += env[k] > env[k - 1] ? 1 : 0;
    falling = (double) maxAt;
    std::printf ("  reversed note: envelope rises over %zu of %zu frames before its peak\n", (size_t) rising, maxAt);
    CHECK (rms (wet.l, a, b) > 1e-3, "reverse produced nothing");
    CHECK (maxAt >= 5 && rising >= 0.6 * falling, "envelope does not swell (peak at frame %zu)", maxAt);
}

void testReverbDecay()
{
    auto rv = make<gsp::fx::Reverb>();
    rv->enabled = true;
    rv->set ("mix", 1.0f);
    rv->set ("predelay", 0.0f);
    double previous = 0;
    for (float decay : { 1.0f, 2.5f, 6.0f })
    {
        rv->set ("decay", decay);
        rv->reset();
        auto in = impulse ((int) (fs * (decay * 1.5 + 1.0)));
        auto out = run (*rv, in);
        out.l[0] -= 1.0f * 0.5f; out.r[0] -= 1.0f * 0.5f; // remove dry part of the impulse
        const double t60 = measureT60 (out.l, out.r, 0);
        std::printf ("  Hall decay %.1f s -> measured T60 %.2f s\n", decay, t60);
        CHECK (t60 > decay * 0.5 && t60 < decay * 1.6, "T60 %.2f for decay %.1f", t60, decay);
        CHECK (t60 > previous, "decay knob is not monotonic");
        previous = t60;
    }

    // stereo: left and right tails should be decorrelated
    rv->set ("decay", 2.5f);
    rv->reset();
    auto out = run (*rv, impulse ((int) fs * 2));
    double lr = 0, ll = 0, rr = 0;
    for (size_t i = 2000; i < out.l.size(); ++i) { lr += out.l[i] * out.r[i]; ll += out.l[i] * out.l[i]; rr += out.r[i] * out.r[i]; }
    const double corr = lr / std::sqrt (ll * rr);
    std::printf ("  L/R correlation of the tail: %.2f\n", corr);
    CHECK (std::abs (corr) < 0.5, "tail is not stereo (corr %.2f)", corr);
}

void testShimmerStable()
{
    auto rv = make<gsp::fx::Reverb>();
    rv->enabled = true;
    rv->set ("mode", (float) gsp::fx::Reverb::Shimmer);
    rv->set ("decay", 20.0f);
    rv->set ("param", 1.0f);
    rv->set ("mix", 1.0f);
    std::vector<float> in ((size_t) (fs * 14), 0.0f);
    std::mt19937 rng (5);
    std::normal_distribution<float> nd (0.0f, 0.2f);
    for (size_t i = 0; i < (size_t) (fs * 2); ++i) in[i] = nd (rng);
    const auto out = run (*rv, in);
    const float early = peak (out.l, (size_t) (fs * 2), (size_t) (fs * 4));
    const float late = peak (out.l, (size_t) (fs * 12), out.l.size());
    std::printf ("  shimmer, decay 20 s, max amount: peak %.2f after input, %.2f ten seconds later\n", early, late);
    CHECK (allFinite (out.l), "NaN");
    CHECK (late < 2.0f && late <= early * 1.5f, "shimmer runs away (%.2f -> %.2f)", early, late);
}

void testReverbTailWhenBypassed()
{
    auto rv = make<gsp::fx::Reverb>();
    rv->enabled = true;
    rv->set ("decay", 3.0f);
    rv->set ("mix", 0.5f);
    std::vector<float> in ((size_t) fs, 0.0f);
    for (size_t i = 0; i < 4800; ++i) in[i] = 0.5f * (float) std::sin (i * 0.05);
    Stereo out { in, in };
    for (size_t s = 0; s < in.size(); s += block)
    {
        if (s == 9600) rv->enabled = false; // switch off right after the note
        rv->process (out.l.data() + s, out.r.data() + s, block);
    }
    const double tail = rms (out.l, 24000, 48000);
    std::printf ("  tail after switching off: rms %.4f\n", tail);
    CHECK (tail > 1e-3, "reverb tail was cut when the pedal was switched off");
}

void testDrives()
{
    for (const char* type : { "overdrive", "distortion", "fuzz" })
    {
        gsp::Pedalboard board;
        auto& fx = board.pedal (board.indexOfType (type));
        fx.enabled = true;
        const int gainParam = 0;
        fx.prepare (fs, block);

        fx.set (gainParam, 0.05f);
        fx.reset();
        const auto lo = run (fx, sine ((int) fs, 440.0, 0.2f));
        fx.set (gainParam, 0.95f);
        fx.reset();
        const auto hi = run (fx, sine ((int) fs, 440.0, 0.2f));
        const double dLo = residualRatioDb (lo.l, 440.0, 4800), dHi = residualRatioDb (hi.l, 440.0, 4800);

        // aliasing: a 3.7 kHz sine through max gain
        fx.reset();
        const auto al = run (fx, sine ((int) fs, 3700.0, 0.3f));
        const double alias = aliasRatioDb (al.l, 3700.0, 4800);
        std::printf ("  %-10s distortion %.1f dB (low gain) -> %.1f dB (high gain); aliasing %.1f dB\n", type, dLo, dHi, alias);
        CHECK (dHi > dLo + 6.0, "%s: gain knob doesn't add distortion", type);
        CHECK (alias < -30.0, "%s: aliasing %.1f dB", type, alias);
    }
}


// energy of x in a band around f (2nd-order band-pass), dB
double bandDb (const std::vector<float>& x, double f)
{
    juce::dsp::IIR::Filter<float> bp;
    bp.coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (fs, f, 1.4);
    double e = 0.0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const float y = bp.processSample (x[i]);
        if (i > x.size() / 4)
            e += (double) y * y;
    }
    return 10.0 * std::log10 (e / (double) x.size() + 1e-20);
}

void testCrunchToneStack()
{
    auto d = make<gsp::fx::Distortion>();
    d->enabled = true;
    const auto in = gsptest::guitarLikeSignal ((int) fs, fs);
    auto with = [&] (const char* knob, float v)
    {
        for (const char* k : { "bass", "middle", "treble" })
            d->set (k, 0.5f);
        d->set (knob, v);
        d->reset();
        return run (*d, in).l;
    };
    const double bassLo = bandDb (with ("bass", 0.0f), 100.0), bassHi = bandDb (with ("bass", 1.0f), 100.0);
    const double midLo = bandDb (with ("middle", 0.0f), 600.0), midHi = bandDb (with ("middle", 1.0f), 600.0);
    const double trebLo = bandDb (with ("treble", 0.0f), 3000.0), trebHi = bandDb (with ("treble", 1.0f), 3000.0);
    std::printf ("  CRUNCH tone stack: BASS %+.1f dB @100 Hz, MIDDLE %+.1f dB @600 Hz, TREBLE %+.1f dB @3 kHz\n",
                 bassHi - bassLo, midHi - midLo, trebHi - trebLo);
    // British-style passive stack: a wide treble/bass range and a gentler middle (like the amps' stack)
    CHECK (bassHi - bassLo > 4.0 && midHi - midLo > 2.0 && trebHi - trebLo > 5.0, "tone knobs too weak");

    // default settings: in the same loudness ballpark as the guitar (no big jump when switching on)
    for (int i = 0; i < (int) d->getParams().size(); ++i)
        d->set (i, d->getParams()[(size_t) i].def);
    d->reset();
    const auto out = run (*d, in).l;
    const double change = 20.0 * std::log10 (rms (out) / rms (in));
    std::printf ("  CRUNCH default level vs. dry guitar: %+.1f dB\n", change);
    CHECK (std::abs (change) < 6.0 && allFinite (out), "level jump %.1f dB", change);
}

void testCompressor()
{
    auto c = make<gsp::fx::Compressor>();
    c->enabled = true;
    c->set ("sustain", 0.8f);
    std::vector<float> in;
    auto quiet = sine ((int) fs / 2, 330.0, 0.02f), loud = sine ((int) fs / 2, 330.0, 0.5f);
    in.insert (in.end(), loud.begin(), loud.end());
    in.insert (in.end(), quiet.begin(), quiet.end());
    const auto out = run (*c, in);
    const double inRange = 20 * std::log10 (rms (in, 12000, 24000) / rms (in, 36000, 48000));
    const double outRange = 20 * std::log10 (rms (out.l, 12000, 24000) / rms (out.l, 36000, 48000));
    std::printf ("  loud/quiet difference %.1f dB in -> %.1f dB out\n", inRange, outRange);
    CHECK (outRange < inRange - 8.0, "compressor does not reduce dynamics");
}

void testTremoloSync()
{
    auto t = make<gsp::fx::Tremolo>();
    t->enabled = true;
    t->set ("depth", 1.0f);
    t->set ("shape", 0.0f);
    gsp::fx::TempoClock clock;
    clock.bpm = 120.0;
    t->setTempoClock (&clock);
    t->set ("sync", 1.0f);
    t->set ("division", (float) gsp::fx::Subdivision::Quarter);
    std::vector<float> dc ((size_t) fs * 2, 0.5f);
    const auto out = run (*t, dc);
    // find two consecutive minima -> period
    std::vector<size_t> minima;
    for (size_t i = 1; i + 1 < out.l.size(); ++i)
        if (out.l[i] < 0.01f && out.l[i] <= out.l[i - 1] && out.l[i] < out.l[i + 1])
            if (minima.empty() || i - minima.back() > 1000) minima.push_back (i);
    const double period = minima.size() >= 3 ? (double) (minima[2] - minima[1]) / fs : -1;
    std::printf ("  TAP 120 BPM 1/4 -> tremolo period %.3f s (expected 0.500)\n", period);
    CHECK (std::abs (period - 0.5) < 0.005, "period %.3f", period);
    CHECK (peak (out.l) > 0.45f, "depth range wrong");
}

void testTapTempo()
{
    gsp::TapTempo tap;
    double bpm = 0;
    for (double t : { 10.0, 10.5, 11.0, 11.5 }) bpm = tap.tap (t);
    CHECK (std::abs (bpm - 120.0) < 0.01, "4 taps 0.5 s apart -> %.2f BPM", bpm);
    // a long pause starts over
    CHECK (tap.tap (20.0) < 1.0, "a tap after a long pause should start over");
    bpm = tap.tap (20.75);
    CHECK (std::abs (bpm - 80.0) < 0.01, "after a pause: %.2f BPM", bpm);
    std::printf ("  taps every 0.5 s -> 120 BPM; new taps every 0.75 s -> %.1f BPM\n", bpm);
}

void testOrderAndPresets()
{
    gsp::Pedalboard a;
    const int reverb = a.indexOfType ("reverb"), od = a.indexOfType ("overdrive");
    a.movePedal (reverb, 0);  // reverb first, before the amp
    a.pedal (od).enabled = true;
    a.pedal (od).set ("drive", 0.77f);
    a.tempo().bpm = 97.0;

    const auto json = juce::JSON::toString (a.toVar());
    gsp::Pedalboard b;
    b.fromVar (juce::JSON::parse (json));
    CHECK (b.getOrder() == a.getOrder(), "order not restored");
    CHECK (b.pedal (od).enabled.load(), "enabled not restored");
    CHECK (std::abs (b.pedal (od).get ("drive") - 0.77f) < 1e-6f, "param not restored");
    CHECK (std::abs (b.tempo().bpm.load() - 97.0) < 1e-9, "bpm not restored");

    // invalid order is ignored
    auto before = b.getOrder();
    b.setOrder ({ 0, 1, 2 });
    CHECK (b.getOrder() == before, "invalid order accepted");

    // the audio thread runs pedals in the new order: reverb now comes before the AMP block
    gsp::Pedalboard c;
    c.prepare (fs, block);
    c.fromVar (juce::JSON::parse (json));
    c.pedal (reverb).enabled = true;
    std::vector<float> l ((size_t) block, 0.0f), r ((size_t) block, 0.0f);
    l[0] = r[0] = 1.0f;
    float energyAtAmp = 0.0f;
    c.process (l.data(), r.data(), block, [&] (float* al, float*, int n)
    {
        for (int i = 1; i < n; ++i) energyAtAmp += al[i] * al[i]; // reverb tail already present
    });
    CHECK (c.getOrder().front() == reverb, "reverb not first");
    CHECK (energyAtAmp > 0.0f, "AMP block did not receive the reverb output");
    std::printf ("  order, enabled state, parameters and BPM survive save/load\n");

    // a board saved before the dual delay existed keeps its order; the new pedal goes after ECHOES
    {
        gsp::Pedalboard old;
        auto v = juce::JSON::parse (json);
        juce::Array<juce::var> kept;
        for (const auto& item : *v["order"].getArray())
            if (item.toString() != "dualdelay")
                kept.add (item);
        v.getDynamicObject()->setProperty ("order", kept);
        old.fromVar (v);
        const auto order = old.getOrder();
        const auto delayPos = std::find (order.begin(), order.end(), old.indexOfType ("delay"));
        CHECK (order.front() == reverb, "old saved order was discarded");
        CHECK (delayPos + 1 != order.end() && *(delayPos + 1) == old.indexOfType ("dualdelay"), "new pedal not placed after ECHOES");
        std::printf ("  order saved by an older version is kept; the dual delay is added after ECHOES\n");
    }
}

void testDualDelay()
{
    auto d = make<gsp::fx::DualDelay>();
    d->enabled = true;
    for (const char* id : { "feedbackL", "feedbackR", "mod" }) d->set (id, 0.0f);
    for (const char* id : { "levelL", "levelR", "toneL", "toneR" }) d->set (id, 1.0f);
    d->set ("voiceL", (float) gsp::fx::DualDelay::Digital);
    d->set ("voiceR", (float) gsp::fx::DualDelay::Digital);
    d->set ("timeL", 300.0f);
    d->set ("timeR", 450.0f);
    d->reset();

    auto out = run (*d, impulse ((int) fs));
    const size_t hitL = argmaxAbs (out.l, 100, out.l.size()), hitR = argmaxAbs (out.r, 100, out.r.size());
    std::printf ("  free: L echo at %zu (expected %d), R echo at %zu (expected %d)\n", hitL, (int) (0.3 * fs), hitR, (int) (0.45 * fs));
    CHECK (std::abs ((long) hitL - (long) (0.3 * fs)) <= 3, "L echo at %zu", hitL);
    CHECK (std::abs ((long) hitR - (long) (0.45 * fs)) <= 3, "R echo at %zu", hitR);

    // TAP sync: 120 BPM, L = 1/4 (0.5 s), R = 1/8 dotted (0.375 s)
    gsp::fx::TempoClock clock;
    clock.bpm = 120.0;
    d->setTempoClock (&clock);
    d->set ("sync", 1.0f);
    d->set ("divisionL", (float) gsp::fx::Subdivision::Quarter);
    d->set ("divisionR", (float) gsp::fx::Subdivision::DottedEighth);
    d->reset();
    out = run (*d, impulse ((int) fs));
    const size_t sL = argmaxAbs (out.l, 100, out.l.size()), sR = argmaxAbs (out.r, 100, out.r.size());
    std::printf ("  TAP 120 BPM: L 1/4 at %zu (expected %d), R 1/8 pontuada at %zu (expected %d)\n", sL, (int) (0.5 * fs), sR, (int) (0.375 * fs));
    CHECK (std::abs ((long) sL - (long) (0.5 * fs)) <= 3, "synced L at %zu", sL);
    CHECK (std::abs ((long) sR - (long) (0.375 * fs)) <= 3, "synced R at %zu", sR);
    d->set ("sync", 0.0f);

    // sides are independent: an impulse only on L never reaches R with CROSS at 0
    auto onlyLeft = [&] (float cross)
    {
        d->set ("feedbackL", 0.6f); d->set ("feedbackR", 0.6f); d->set ("cross", cross);
        d->reset();
        Stereo s { impulse ((int) fs * 2), std::vector<float> ((size_t) fs * 2, 0.0f) };
        for (size_t i = 0; i < s.l.size(); i += block)
            d->process (s.l.data() + i, s.r.data() + i, (int) std::min ((size_t) block, s.l.size() - i));
        return peak (s.r);
    };
    const float leak = onlyLeft (0.0f), crossed = onlyLeft (1.0f);
    std::printf ("  impulse on L only: R peak %.4f with CROSS 0, %.3f with CROSS 1\n", leak, crossed);
    CHECK (leak < 1e-6f, "L leaks into R with CROSS 0 (%.6f)", leak);
    CHECK (crossed > 0.1f, "CROSS does not send repeats to R (%.4f)", crossed);

    // each side has its own colour: TONE L bright vs TONE R dark on the same echo
    d->set ("cross", 0.0f); d->set ("feedbackL", 0.0f); d->set ("feedbackR", 0.0f);
    d->set ("timeL", 300.0f); d->set ("timeR", 300.0f);
    d->set ("toneL", 1.0f); d->set ("toneR", 0.0f);
    d->reset();
    out = run (*d, impulse ((int) fs));
    auto hf = [] (const std::vector<float>& v)
    {
        double e = 0;
        for (size_t i = 14000; i < 15500; ++i) { const double dlt = v[i] - v[i - 1]; e += dlt * dlt; }
        return e;
    };
    const double ratio = 10.0 * std::log10 (hf (out.l) / std::max (hf (out.r), 1e-30));
    std::printf ("  TONE L 1 vs TONE R 0: L echo has %.1f dB more high end\n", ratio);
    CHECK (ratio > 6.0, "tone per side has no effect (%.1f dB)", ratio);

    // stability: max repeats, full cross, every voice combination
    for (int vl = 0; vl < 3; ++vl)
        for (int vr = 0; vr < 3; ++vr)
        {
            d->set ("voiceL", (float) vl); d->set ("voiceR", (float) vr);
            d->set ("feedbackL", 1.0f); d->set ("feedbackR", 1.0f); d->set ("cross", 0.5f); d->set ("mod", 1.0f);
            d->set ("timeL", 120.0f); d->set ("timeR", 170.0f);
            d->reset();
            Stereo s { sine ((int) fs * 4, 330.0, 0.5f), sine ((int) fs * 4, 330.0, 0.5f) };
            for (size_t i = 0; i < s.l.size(); i += block)
                d->process (s.l.data() + i, s.r.data() + i, (int) std::min ((size_t) block, s.l.size() - i));
            bool finite = true;
            for (size_t i = 0; i < s.l.size(); ++i) finite = finite && std::isfinite (s.l[i]) && std::isfinite (s.r[i]);
            CHECK (finite, "NaN with voices %d/%d", vl, vr);
            CHECK (peak (s.l) < 8.0f && peak (s.r) < 8.0f, "runaway with voices %d/%d: %.1f", vl, vr, std::max (peak (s.l), peak (s.r)));
        }
    std::printf ("  max repeats + cross, all 9 voice combinations: stable\n");
}

void testCpuBudget()
{
    gsp::Pedalboard board;
    for (int i = 0; i < board.size(); ++i) board.pedal (i).enabled = true;
    board.pedal (board.indexOfType ("reverb")).set ("mode", (float) gsp::fx::Reverb::Shimmer);
    board.pedal (board.indexOfType ("delay")).set ("mode", (float) gsp::fx::Delay::Tape);
    const int small = 64;
    board.prepare (fs, small);
    auto in = guitarLikeSignal ((int) fs * 5, fs);
    std::vector<float> r = in;
    const auto start = std::chrono::steady_clock::now();
    for (size_t s = 0; s + small <= in.size(); s += small)
        board.process (in.data() + s, r.data() + s, small, [] (float*, float*, int) {});
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
    const double load = secs / 5.0 * 100.0;
    std::printf ("  all %d pedals on (shimmer + tape), 64-sample buffer: %.1f%% of one CPU core\n", board.size(), load);
    CHECK (load < 50.0, "too slow: %.1f%%", load);
}
} // namespace

void runEffectTests()
{
    runTest ("Pedals: every pedal and mode is stable", testAllPedalsStable);
    runTest ("Pedals: bypass is transparent", testBypassIsTransparent);
    runTest ("Pedals: on/off switching without clicks", testSwitchingIsClickFree);
    runTest ("Delay: time and TAP sync", testDelayTiming);
    runTest ("Dual delay: independent L/R time, tone, cross, stability", testDualDelay);
    runTest ("Delay: ping pong alternates sides", testPingPong);
    runTest ("Delay: reverse", testReverseDelay);
    runTest ("Reverb: decay time and stereo", testReverbDecay);
    runTest ("Reverb: shimmer cannot run away", testShimmerStable);
    runTest ("Reverb: tail keeps ringing when switched off", testReverbTailWhenBypassed);
    runTest ("Drives: gain adds distortion, low aliasing", testDrives);
    runTest ("Drives: CRUNCH bass/middle/treble and level", testCrunchToneStack);
    runTest ("Compressor reduces dynamics", testCompressor);
    runTest ("Tremolo follows TAP tempo", testTremoloSync);
    runTest ("TAP tempo", testTapTempo);
    runTest ("Pedal order and presets", testOrderAndPresets);
    runTest ("CPU budget", testCpuBudget);
}
