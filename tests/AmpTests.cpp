// Phase 2 tests: built-in amplifiers (tone stack against the circuit equations, gain/tone
// knobs, level matching, stability), cabinet + mic simulation, CPU budget.

#include "TestUtil.h"

#include "amp/AmpSim.h"
#include "amp/CabinetSim.h"
#include "engine/SignalChain.h"

#include <juce_dsp/juce_dsp.h>

#include <chrono>
#include <complex>
#include <random>

using namespace gsptest;

namespace
{
constexpr double fs = 48000.0;
constexpr int block = 128;
constexpr double pi = 3.14159265358979323846;

std::vector<float> sineWave (int length, double hz, float amp)
{
    std::vector<float> x ((size_t) length);
    for (int i = 0; i < length; ++i)
        x[(size_t) i] = amp * (float) std::sin (2.0 * pi * hz * i / fs);
    return x;
}

// plucked-string-like test signal: decaying harmonic notes, peaks around -8 dBFS
std::vector<float> guitarNotes (int length)
{
    std::vector<float> x ((size_t) length, 0.0f);
    const double notes[] = { 82.4, 110.0, 146.8, 196.0, 246.9, 329.6 };
    const int noteLen = (int) (fs * 0.5);
    for (int start = 0, k = 0; start < length; start += noteLen, ++k)
    {
        const double f = notes[k % 6];
        for (int i = 0; i < noteLen && start + i < length; ++i)
        {
            const double t = i / fs, env = std::exp (-t * 4.0);
            double s = 0;
            for (int h = 1; h <= 8; ++h)
                s += std::sin (2.0 * pi * f * h * t) / (h * h * 0.6 + 0.4);
            x[(size_t) (start + i)] += (float) (0.3 * env * s);
        }
    }
    return x;
}

void runAmp (gsp::amp::AmpSim& a, std::vector<float>& x, int blockSize = block)
{
    for (size_t s = 0; s < x.size(); s += (size_t) blockSize)
        a.process (x.data() + s, (int) std::min ((size_t) blockSize, x.size() - s));
}

double rmsDb (const std::vector<float>& v, size_t from = 0)
{
    double e = 0;
    for (size_t i = from; i < v.size(); ++i) e += (double) v[i] * v[i];
    return 10.0 * std::log10 (std::max (e / (double) (v.size() - from), 1e-30));
}

bool allFinite (const std::vector<float>& v)
{
    for (float f : v) if (! std::isfinite (f)) return false;
    return true;
}

double residualDb (const std::vector<float>& y, double hz, size_t from)
{
    double ss = 0, cc = 0, sc = 0, ys = 0, yc = 0, mean = 0;
    for (size_t i = from; i < y.size(); ++i) mean += y[i];
    mean /= (double) (y.size() - from);
    for (size_t i = from; i < y.size(); ++i)
    {
        const double w = 2.0 * pi * hz * (double) i / fs, s = std::sin (w), c = std::cos (w), v = y[i] - mean;
        ss += s * s; cc += c * c; sc += s * c; ys += v * s; yc += v * c;
    }
    const double det = ss * cc - sc * sc, A = (ys * cc - yc * sc) / det, B = (yc * ss - ys * sc) / det;
    double e = 0, f = 0;
    for (size_t i = from; i < y.size(); ++i)
    {
        const double w = 2.0 * pi * hz * (double) i / fs, fit = A * std::sin (w) + B * std::cos (w);
        e += (y[i] - mean - fit) * (y[i] - mean - fit);
        f += fit * fit;
    }
    return 10.0 * std::log10 (std::max (e, 1e-30) / std::max (f, 1e-30));
}

// magnitude response (dB) of an impulse response, averaged over 1/6 octave around f
struct Response
{
    std::vector<float> mag;
    int size;
    double at (double f) const
    {
        const double lo = f / std::pow (2.0, 1.0 / 12.0), hi = f * std::pow (2.0, 1.0 / 12.0);
        double e = 0; int n = 0;
        for (int k = std::max (1, (int) (lo * size / fs)); k <= (int) (hi * size / fs); ++k) { e += (double) mag[(size_t) k] * mag[(size_t) k]; ++n; }
        return 10.0 * std::log10 (std::max (e / std::max (n, 1), 1e-30));
    }
};

Response responseOf (const std::vector<float>& ir)
{
    const int order = 14, size = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> buf ((size_t) size * 2, 0.0f);
    std::copy (ir.begin(), ir.begin() + (std::ptrdiff_t) std::min ((size_t) size, ir.size()), buf.begin());
    fft.performFrequencyOnlyForwardTransform (buf.data());
    return { buf, size };
}

Response cabResponse (int cab, int micIndex, float position, float distance)
{
    gsp::amp::CabinetSim c;
    c.cabinetIndex = cab; c.micIndex = micIndex; c.position = position; c.distance = distance;
    c.prepare (fs);
    std::vector<float> x (16384, 0.0f);
    x[0] = 1.0f;
    c.process (x.data(), (int) x.size());
    return responseOf (x);
}

void setKnobs (gsp::amp::AmpSim& a, float g, float b = 5, float m = 5, float t = 5)
{
    a.gain = g; a.bass = b; a.middle = m; a.treble = t;
    a.volume = 5; a.presence = 5; a.master = 5;
}

// Independent check: solve the tone stack circuit's nodal equations numerically (complex
// 5x5 system, Gaussian elimination) and return |Vout/Vin|.
double nodalMagnitude (const gsp::amp::ToneStackParts& p, double t, double m, double b, double f)
{
    using C = std::complex<double>;
    const C s (0.0, 2.0 * pi * f);
    const double l = std::exp ((b - 1.0) * 3.4), tiny = 1e-6;
    // nodes: 0 = input (1 V), 1 = A, 2 = treble top, 3 = output (wiper), 4 = treble bottom, 5 = mid top
    C Y[6][6] = {};
    auto add = [&] (int a, int c, C y)
    {
        Y[a][a] += y; if (c >= 0) { Y[c][c] += y; Y[a][c] -= y; Y[c][a] -= y; }
    };
    add (0, 2, s * p.C1);
    add (2, 3, 1.0 / std::max ((1.0 - t) * p.R1, tiny));
    add (3, 4, 1.0 / std::max (t * p.R1, tiny));
    add (0, 1, 1.0 / p.R4);
    add (1, 4, s * p.C2);
    add (1, 5, s * p.C3);
    add (4, 5, 1.0 / std::max (l * p.R2, tiny));
    add (5, -1, 1.0 / std::max (m * p.R3, tiny));
    C A[5][6];
    for (int r = 0; r < 5; ++r)
    {
        for (int c = 0; c < 5; ++c) A[r][c] = Y[r + 1][c + 1];
        A[r][5] = -Y[r + 1][0];
    }
    for (int c = 0; c < 5; ++c)
    {
        int piv = c;
        for (int r = c + 1; r < 5; ++r) if (std::abs (A[r][c]) > std::abs (A[piv][c])) piv = r;
        for (int k = 0; k < 6; ++k) std::swap (A[c][k], A[piv][k]);
        for (int r = 0; r < 5; ++r)
            if (r != c)
            {
                const C q = A[r][c] / A[c][c];
                for (int k = c; k < 6; ++k) A[r][k] -= q * A[c][k];
            }
    }
    return std::abs (A[2][5] / A[2][2]); // node 3 (output) is row 2
}

// ------------------------------------------------------------------ tests
void testToneStackMatchesCircuit()
{
    double worstCircuit = 0;
    for (int v = 0; v < gsp::amp::numVoicings(); ++v)
        for (double t : { 0.0, 0.3, 0.7, 1.0 })
            for (double m : { 0.0, 0.4, 1.0 })
                for (double b : { 0.0, 0.5, 1.0 })
                    for (double f : { 40.0, 100.0, 300.0, 1000.0, 3000.0, 8000.0 })
                    {
                        const auto& parts = gsp::amp::voicing (v).toneStack;
                        const double d = 20.0 * std::log10 (gsp::amp::ToneStack::analogMagnitude (gsp::amp::ToneStack::analog (parts, t, m, b), f)
                                                            / nodalMagnitude (parts, t, m, b, f));
                        worstCircuit = std::max (worstCircuit, std::abs (d));
                    }
    std::printf ("  tone stack: transfer function vs numerical nodal solve of the circuit, worst %.4f dB\n", worstCircuit);
    CHECK (worstCircuit < 0.01, "tone stack equations differ from the circuit by %.3f dB", worstCircuit);

    double worst = 0;
    for (int v = 0; v < gsp::amp::numVoicings(); ++v)
    {
        const auto& parts = gsp::amp::voicing (v).toneStack;
        for (double t : { 0.0, 0.5, 1.0 })
            for (double m : { 0.0, 0.5, 1.0 })
                for (double b : { 0.0, 0.5, 1.0 })
                {
                    gsp::amp::ToneStack ts;
                    ts.setup (parts, 192000.0, t, m, b);
                    const auto an = gsp::amp::ToneStack::analog (parts, t, m, b);
                    for (double f : { 40.0, 100.0, 300.0, 1000.0, 3000.0, 8000.0 })
                    {
                        const double d = 20.0 * std::log10 (ts.digitalMagnitude (f, 192000.0) / gsp::amp::ToneStack::analogMagnitude (an, f));
                        worst = std::max (worst, std::abs (d));
                    }
                }
    }
    std::printf ("  tone stack: digital filter vs analog transfer function, worst difference %.3f dB (6 amps x 27 knob settings x 6 freqs)\n", worst);
    CHECK (worst < 0.25, "tone stack differs from the circuit by %.3f dB", worst);

    // passive stack sanity: magnitude never above 0 dB (it can only cut)
    double maxGain = -100;
    for (int v = 0; v < gsp::amp::numVoicings(); ++v)
        for (double f = 20; f < 20000; f *= 1.1)
            maxGain = std::max (maxGain, 20.0 * std::log10 (gsp::amp::ToneStack::analogMagnitude (
                                             gsp::amp::ToneStack::analog (gsp::amp::voicing (v).toneStack, 1, 1, 1), f)));
    std::printf ("  tone stack: highest gain with all knobs at 10 = %.2f dB (passive circuit must be <= 0)\n", maxGain);
    CHECK (maxGain <= 0.01, "passive tone stack has gain %.2f dB", maxGain);
}

void testToneStackKnobs()
{
    const auto& parts = gsp::amp::voicing (0).toneStack;
    auto db = [&] (double t, double m, double b, double f)
    { return 20.0 * std::log10 (gsp::amp::ToneStack::analogMagnitude (gsp::amp::ToneStack::analog (parts, t, m, b), f)); };
    const double bassRange = db (0.5, 0.5, 1.0, 80) - db (0.5, 0.5, 0.0, 80);
    const double midRange = db (0.5, 1.0, 0.5, 500) - db (0.5, 0.0, 0.5, 500);
    const double trebleRange = db (1.0, 0.5, 0.5, 5000) - db (0.0, 0.5, 0.5, 5000);
    std::printf ("  tone stack ranges: BASS %.1f dB @80 Hz, MIDDLE %.1f dB @500 Hz, TREBLE %.1f dB @5 kHz\n", bassRange, midRange, trebleRange);
    CHECK (bassRange > 6.0, "bass range %.1f", bassRange);
    CHECK (midRange > 4.0, "mid range %.1f", midRange);
    CHECK (trebleRange > 8.0, "treble range %.1f", trebleRange);
}

void testVoicings()
{
    const auto notes = guitarNotes ((int) fs * 3);
    double minLevel = 100, maxLevel = -100;
    for (int v = 0; v < gsp::amp::numVoicings(); ++v)
    {
        gsp::amp::AmpSim a;
        a.voicingIndex = v;
        a.prepare (fs, block);

        auto distortionAt = [&] (float g)
        {
            setKnobs (a, g);
            a.reset();
            auto x = sineWave ((int) fs, 220.0, 0.25f);
            runAmp (a, x);
            CHECK (allFinite (x), "%s: NaN/inf", gsp::amp::voicing (v).name);
            return residualDb (x, 220.0, 9600);
        };
        const double d2 = distortionAt (2.0f), d5 = distortionAt (5.0f), d9 = distortionAt (9.0f);

        setKnobs (a, 5.0f);
        a.reset();
        auto x = notes;
        runAmp (a, x);
        const double level = rmsDb (x, 4800);
        float pk = 0; for (float f : x) pk = std::max (pk, std::abs (f));
        minLevel = std::min (minLevel, level); maxLevel = std::max (maxLevel, level);

        std::printf ("  %-20s harmonics GAIN 2/5/9: %6.1f %6.1f %6.1f dB | level %.1f dBFS RMS, peak %.2f\n",
                     gsp::amp::voicing (v).name, d2, d5, d9, level, pk);
        CHECK (d9 > d2 + 3.0, "%s: GAIN doesn't add distortion", gsp::amp::voicing (v).name);
        CHECK (pk < 2.0f, "%s: output peak %.2f", gsp::amp::voicing (v).name, pk);
        CHECK (allFinite (x), "%s: NaN", gsp::amp::voicing (v).name);
    }
    std::printf ("  level spread between amps (all knobs at 5): %.1f dB\n", maxLevel - minLevel);
    CHECK (maxLevel - minLevel < 6.0, "amps not level-matched: spread %.1f dB", maxLevel - minLevel);

    // clean amp stays clean at low gain; high-gain amp is saturated at high gain
    {
        gsp::amp::AmpSim a; a.voicingIndex = 0; a.prepare (fs, block); setKnobs (a, 2.0f);
        auto x = sineWave ((int) fs, 220.0, 0.25f); runAmp (a, x);
        const double d = residualDb (x, 220.0, 9600);
        CHECK (d < -25.0, "clean amp not clean: %.1f dB", d);
    }
    {
        gsp::amp::AmpSim a; a.voicingIndex = gsp::amp::numVoicings() - 1; a.prepare (fs, block); setKnobs (a, 8.0f);
        auto x = sineWave ((int) fs, 220.0, 0.25f); runAmp (a, x);
        const double d = residualDb (x, 220.0, 9600);
        CHECK (d > -10.0, "high-gain amp not saturated: %.1f dB", d);
    }
}

void testAmpToneKnobs()
{
    // on the clean amp at low gain, BASS / TREBLE must move the matching band
    auto bandLevel = [] (float b, float t, double hz)
    {
        gsp::amp::AmpSim a; a.voicingIndex = 0; a.prepare (fs, block);
        setKnobs (a, 1.5f, b, 5.0f, t);
        a.volume = 2.0f;
        auto x = sineWave ((int) fs, hz, 0.05f);
        runAmp (a, x);
        return rmsDb (x, 9600);
    };
    const double bass = bandLevel (10, 5, 90) - bandLevel (0, 5, 90);
    const double treble = bandLevel (5, 10, 3000) - bandLevel (5, 0, 3000);
    std::printf ("  amp knobs: BASS 0->10 = %+.1f dB @90 Hz, TREBLE 0->10 = %+.1f dB @3 kHz\n", bass, treble);
    CHECK (bass > 6.0, "bass knob %.1f", bass);
    CHECK (treble > 6.0, "treble knob %.1f", treble);

    // MASTER: 5 -> 10 = +12 dB (clean amp, below clipping)
    auto masterLevel = [] (float m)
    {
        gsp::amp::AmpSim a; a.voicingIndex = 0; a.prepare (fs, block);
        setKnobs (a, 1.5f); a.volume = 2.0f; a.master = m;
        auto x = sineWave ((int) fs, 440.0, 0.05f); runAmp (a, x);
        return rmsDb (x, 9600);
    };
    const double master = masterLevel (10) - masterLevel (5);
    std::printf ("  MASTER 5 -> 10: %+.2f dB\n", master);
    CHECK (std::abs (master - 12.04) < 0.2, "master %.2f", master);
}

void testCabinet()
{
    for (int c = 0; c < gsp::amp::numCabinets(); ++c)
    {
        const auto r = cabResponse (c, 0, 0.3f, 0.1f);
        const double mid = r.at (1500), low = r.at (100), top = r.at (10000), dc = r.at (30);
        std::printf ("  %-16s 30 Hz %+.1f | 100 Hz %+.1f | 1.5 kHz %+.1f | 10 kHz %+.1f dB\n",
                     gsp::amp::cabinet (c).name, dc - mid, low - mid, 0.0, top - mid);
        CHECK (top < mid - 20.0, "%s: no speaker roll-off", gsp::amp::cabinet (c).name);
        CHECK (dc < mid - 6.0, "%s: no low cut", gsp::amp::cabinet (c).name);
        CHECK (std::abs (mid) < 12.0, "%s: midband level %.1f dB", gsp::amp::cabinet (c).name, mid);
    }
    const auto centre = cabResponse (2, 0, 0.0f, 0.1f), edge = cabResponse (2, 0, 1.0f, 0.1f);
    const double darker = (centre.at (4000) - centre.at (1000)) - (edge.at (4000) - edge.at (1000));
    std::printf ("  mic POSITION centre -> edge: 4 kHz drops %.1f dB (relative to 1 kHz)\n", darker);
    CHECK (darker > 4.0, "position does nothing (%.1f dB)", darker);

    const auto ribbon = cabResponse (2, 1, 0.3f, 0.1f), condenser = cabResponse (2, 2, 0.3f, 0.1f);
    const double micDiff = (condenser.at (5000) - condenser.at (1000)) - (ribbon.at (5000) - ribbon.at (1000));
    std::printf ("  condenser vs ribbon: %.1f dB brighter at 5 kHz\n", micDiff);
    CHECK (micDiff > 4.0, "mics sound the same (%.1f dB)", micDiff);

    const auto close = cabResponse (2, 0, 0.3f, 0.0f), far = cabResponse (2, 0, 0.3f, 1.0f);
    const double proximity = (close.at (120) - close.at (1000)) - (far.at (120) - far.at (1000));
    // comb filter from the floor reflection: ripple between neighbouring bands in the far response
    double ripple = 0;
    for (double f = 300; f < 3000; f *= 1.02)
        ripple = std::max (ripple, std::abs (far.at (f) - far.at (f * 1.02)));
    std::printf ("  DISTANCE close -> far: %.1f dB less bass at 120 Hz, floor-reflection ripple %.1f dB\n", proximity, ripple);
    CHECK (proximity > 3.0, "distance doesn't change proximity bass (%.1f dB)", proximity);
}

void testCabinetLevel()
{
    // the cabinet should not change the overall loudness much (amp output in, guitar band)
    gsp::amp::AmpSim a; a.voicingIndex = 1; a.prepare (fs, block); setKnobs (a, 5.0f);
    auto x = guitarNotes ((int) fs * 3);
    runAmp (a, x);
    const double before = rmsDb (x, 4800);
    for (int c = 0; c < gsp::amp::numCabinets(); ++c)
    {
        gsp::amp::CabinetSim cab; cab.cabinetIndex = c; cab.prepare (fs);
        auto y = x;
        cab.process (y.data(), (int) y.size());
        const double change = rmsDb (y, 4800) - before;
        std::printf ("  %-16s level change %+.1f dB\n", gsp::amp::cabinet (c).name, change);
        CHECK (std::abs (change) < 4.0, "%s changes the level by %.1f dB", gsp::amp::cabinet (c).name, change);
    }
}

void testCpu()
{
    gsp::amp::AmpSim a; a.voicingIndex = gsp::amp::numVoicings() - 1; a.prepare (fs, 64); setKnobs (a, 8.0f);
    gsp::amp::CabinetSim c; c.prepare (fs);
    auto x = guitarNotes ((int) fs * 5);
    const auto start = std::chrono::steady_clock::now();
    for (size_t s = 0; s + 64 <= x.size(); s += 64)
    {
        a.process (x.data() + s, 64);
        c.process (x.data() + s, 64);
    }
    const double load = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count() / 5.0 * 100.0;
    std::printf ("  amp (5 stages, 4x oversampling) + cabinet, 64-sample buffer: %.1f%% of one CPU core\n", load);
    CHECK (load < 25.0, "too slow: %.1f%%", load);
}

void testChainIntegration()
{
    gsp::SignalChain chain;
    chain.prepare (fs, block);
    chain.ampSource = 1; // built-in amp
    chain.cabinetMode = 0; // simulated cabinet
    auto in = guitarNotes ((int) fs * 2);
    std::vector<float> l (in.size()), r (in.size());
    for (size_t s = 0; s < in.size(); s += block)
    {
        float* outs[] = { l.data() + s, r.data() + s };
        if (s == (size_t) fs) chain.getAmp().voicingIndex = 4; // switch amp while playing
        chain.process (in.data() + s, outs, 2, block);
    }
    float maxStep = 0;
    for (size_t i = 1; i < l.size(); ++i) maxStep = std::max (maxStep, std::abs (l[i] - l[i - 1]));
    std::printf ("  chain with built-in amp + cabinet: level %.1f dBFS RMS, largest sample step %.3f\n", rmsDb (l, 4800), maxStep);
    CHECK (allFinite (l) && allFinite (r), "NaN in chain");
    CHECK (rmsDb (l, 4800) > -40.0, "chain output silent");
    CHECK (maxStep < 0.5f, "click when switching amps (step %.3f)", maxStep);
}
} // namespace

void runAmpTests()
{
    runTest ("Amps: tone stack vs circuit", testToneStackMatchesCircuit);
    runTest ("Amps: tone stack knob ranges", testToneStackKnobs);
    runTest ("Amps: voicings: distortion and level", testVoicings);
    runTest ("Amps: tone and master knobs", testAmpToneKnobs);
    runTest ("Amps: cabinet + microphone", testCabinet);
    runTest ("Amps: cabinet keeps the level", testCabinetLevel);
    runTest ("Amps: CPU", testCpu);
    runTest ("Amps: signal chain", testChainIntegration);
}
