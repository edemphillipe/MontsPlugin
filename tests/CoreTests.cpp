// Core tests: run on every build (Linux, macOS, Windows) via ctest.
// They check that captures are really read (bit-for-bit or near against the upstream
// libraries), that sample-rate conversion is clean, and that the chain is glitch-free.

#include "captures/CaptureLoader.h"
#include "dsp/NoiseGate.h"
#include "dsp/Resampler.h"
#include "engine/SignalChain.h"
#include "reference/Reference.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <vector>

namespace
{
int failures = 0, checks = 0, skipped = 0;

#define CHECK(cond, ...)                                                        \
    do {                                                                        \
        ++checks;                                                               \
        if (! (cond)) {                                                         \
            ++failures;                                                         \
            std::printf ("  FAIL %s:%d  %s  ", __FILE__, __LINE__, #cond);      \
            std::printf (__VA_ARGS__);                                          \
            std::printf ("\n");                                                 \
        }                                                                       \
    } while (0)

std::vector<float> guitarLikeSignal (int n, double sr, unsigned seed = 1)
{
    // Plucked-string-ish test signal: decaying harmonics plus a little noise.
    std::mt19937 rng (seed);
    std::normal_distribution<float> noise (0.0f, 0.002f);
    std::vector<float> x ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        const double env = std::exp (-3.0 * std::fmod (t, 0.5));
        double v = 0.0;
        for (int h = 1; h <= 6; ++h)
            v += std::sin (2.0 * juce::MathConstants<double>::pi * 110.0 * h * t) / h;
        x[(size_t) i] = (float) (0.25 * env * v) + noise (rng);
    }
    return x;
}

bool allFinite (const std::vector<float>& v)
{
    for (float s : v)
        if (! std::isfinite (s))
            return false;
    return true;
}

double rms (const std::vector<float>& v, size_t from = 0)
{
    double s = 0;
    for (size_t i = from; i < v.size(); ++i)
        s += (double) v[i] * v[i];
    return std::sqrt (s / (double) juce::jmax ((size_t) 1, v.size() - from));
}

float maxAbsDiff (const std::vector<float>& a, const std::vector<float>& b)
{
    float d = 0;
    for (size_t i = 0; i < std::min (a.size(), b.size()); ++i)
        d = std::max (d, std::abs (a[i] - b[i]));
    return d;
}

void runTest (const char* name, const std::function<void()>& fn)
{
    const int before = failures;
    std::printf ("[ %s ]\n", name);
    fn();
    std::printf ("  %s\n", failures == before ? "ok" : "FAILED");
}

// ---------------------------------------------------------------------------------------------
void testNamModels()
{
    const juce::File dir (GSP_NAM_EXAMPLES);
    const char* names[] = { "wavenet.nam", "lstm.nam", "A2.nam", "wavenet_a1_standard.nam" };
    const double sr = 48000.0;
    const int block = 256;
    const auto in = guitarLikeSignal (48000, sr);

    for (auto* name : names)
    {
        const auto file = dir.getChildFile (name);
        if (! file.existsAsFile())
        {
            std::printf ("  skip %s (not found)\n", name);
            ++skipped;
            continue;
        }

        std::string err;
        auto model = gsp::loadCapture (file.getFullPathName().toStdString(), err);
        CHECK (model != nullptr, "%s: %s", name, err.c_str());
        if (model == nullptr)
            continue;

        model->prepare (sr, block);
        std::vector<float> ours = in;
        for (size_t s = 0; s < ours.size(); s += block)
            model->process (ours.data() + s, (int) std::min ((size_t) block, ours.size() - s));

        std::vector<float> ref;
        CHECK (gsp::reference::runNam (file.getFullPathName().toStdString(), in, ref, sr, block, err), "%s", err.c_str());

        const float diff = maxAbsDiff (ours, ref);
        std::printf ("  %-28s arch=%-10s sr=%.0f  rms_out=%.4f  max|ours-ref|=%.2e\n", name,
                     model->info().architecture.c_str(), model->info().expectedSampleRate, rms (ours), diff);
        CHECK (allFinite (ours), "%s produced NaN/Inf", name);
        CHECK (rms (ours) > 1e-5, "%s output is silent", name);
        // Identical library, identical input -> identical output (only float->double rounding differs).
        CHECK (diff < 1e-5f, "%s differs from NeuralAmpModelerCore by %g", name, diff);
    }
}

void testAidaModel()
{
    const juce::File file (GSP_AIDA_MODEL);
    if (! file.existsAsFile())
    {
        std::printf ("  skip (AIDA-X test model not downloaded)\n");
        ++skipped;
        return;
    }

    std::string err;
    auto model = gsp::loadCapture (file.getFullPathName().toStdString(), err);
    CHECK (model != nullptr, "%s", err.c_str());
    if (model == nullptr)
        return;

    const auto& info = model->info();
    std::printf ("  name=%s arch=%s sr=%.0f based=%s\n", info.name.c_str(), info.architecture.c_str(),
                 info.expectedSampleRate, info.metadata.count ("based") ? info.metadata.at ("based").c_str() : "-");
    CHECK (info.format == gsp::CaptureFormat::AidaX, "format");
    CHECK (info.expectedSampleRate == 48000.0, "metadata samplerate %f", info.expectedSampleRate);

    const auto in = guitarLikeSignal (48000, 48000.0);
    model->prepare (48000.0, 512);
    std::vector<float> ours = in;
    for (size_t s = 0; s < ours.size(); s += 512)
        model->process (ours.data() + s, (int) std::min ((size_t) 512, ours.size() - s));

    std::vector<float> ref;
    CHECK (gsp::reference::runAidaDynamic (file.getFullPathName().toStdString(), in, ref, err), "%s", err.c_str());
    const float diff = maxAbsDiff (ours, ref);
    std::printf ("  rms_in=%.4f rms_out=%.4f  max|static-dynamic|=%.2e\n", rms (in), rms (ours), diff);
    CHECK (allFinite (ours), "NaN/Inf");
    CHECK (rms (ours) > 1e-4, "silent");
    CHECK (diff < 1e-4f, "static model differs from RTNeural dynamic model by %g", diff);

    // Write input/output for the independent numpy check in tools/verify_aidax.py
    juce::File outDir = file.getParentDirectory();
    juce::FileOutputStream fin (outDir.getChildFile ("aida_in.f32")), fout (outDir.getChildFile ("aida_out.f32"));
    fin.setPosition (0); fin.truncate(); fout.setPosition (0); fout.truncate();
    fin.write (in.data(), in.size() * sizeof (float));
    fout.write (ours.data(), ours.size() * sizeof (float));
}

void testRejectsGarbage()
{
    auto tmp = juce::File::createTempFile (".nam");
    tmp.replaceWithText ("{ not a model");
    std::string err;
    CHECK (gsp::loadCapture (tmp.getFullPathName().toStdString(), err) == nullptr, "garbage .nam accepted");
    CHECK (! err.empty(), "no error message");
    auto tmpJson = tmp.withFileExtension ("json");
    tmpJson.replaceWithText ("{\"hello\": 1}");
    err.clear();
    CHECK (gsp::loadCapture (tmpJson.getFullPathName().toStdString(), err) == nullptr, "garbage .json accepted");
    tmp.deleteFile();
    tmpJson.deleteFile();
}

void testResampler()
{
    // 44.1 kHz device, 48 kHz model: up then down must give back the same sine, delayed.
    const double fsDev = 44100.0, fsModel = 48000.0, f = 1000.0;
    const int block = 128, blocks = 400;
    gsp::StreamResampler up, down;
    up.prepare (fsDev, fsModel, block);
    down.prepare (fsModel, fsDev, up.maxOutputFor (block));

    std::vector<float> mid ((size_t) up.maxOutputFor (block)), back ((size_t) down.maxOutputFor ((int) mid.size()));
    std::vector<float> input, output;
    long totalMid = 0;

    for (int b = 0; b < blocks; ++b)
    {
        std::vector<float> x ((size_t) block);
        for (int i = 0; i < block; ++i)
            x[(size_t) i] = (float) (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * f * (b * block + i) / fsDev));
        input.insert (input.end(), x.begin(), x.end());
        const int n = up.process (x.data(), block, mid.data(), (int) mid.size());
        totalMid += n;
        const int m = down.process (mid.data(), n, back.data(), (int) back.size());
        output.insert (output.end(), back.begin(), back.begin() + m);
    }

    const double expectedMid = (double) blocks * block * fsModel / fsDev;
    CHECK (std::abs (totalMid - expectedMid) < 40, "upsampled count %ld vs %f", totalMid, expectedMid);
    CHECK (std::abs ((long) output.size() - (long) input.size()) < 80, "round-trip count %zu vs %zu", output.size(), input.size());

    // Fit a sine of frequency f (least squares on sin/cos) to the steady-state output;
    // what is left over is the converter's error. Independent of the (fractional) delay.
    double ss = 0, cc = 0, sc = 0, ys = 0, yc = 0;
    const size_t from = 4000, to = output.size() - 200;
    for (size_t i = from; i < to; ++i)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * f * (double) i / fsDev;
        const double s = std::sin (w), c = std::cos (w);
        ss += s * s; cc += c * c; sc += s * c; ys += output[i] * s; yc += output[i] * c;
    }
    const double det = ss * cc - sc * sc;
    const double A = (ys * cc - yc * sc) / det, B = (yc * ss - ys * sc) / det;
    double err = 0, sig = 0;
    for (size_t i = from; i < to; ++i)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * f * (double) i / fsDev;
        const double fit = A * std::sin (w) + B * std::cos (w);
        err += (output[i] - fit) * (output[i] - fit);
        sig += fit * fit;
    }
    const double gainDb = 20.0 * std::log10 (std::sqrt (A * A + B * B) / 0.5);
    const double snr = 10.0 * std::log10 (sig / juce::jmax (err, 1e-20));
    std::printf ("  round-trip gain=%.4f dB, SNR=%.1f dB\n", gainDb, snr);
    CHECK (snr > 80.0, "round trip SNR %.1f dB", snr);
    CHECK (std::abs (gainDb) < 0.1, "round trip gain %.3f dB", gainDb);
}

void testGate()
{
    gsp::NoiseGate gate;
    gate.prepare (48000.0);
    gate.setThresholdDb (-50.0f);
    std::vector<float> hiss (48000);
    std::mt19937 rng (3);
    std::normal_distribution<float> nd (0.0f, 0.0005f); // ~ -66 dBFS
    for (auto& s : hiss) s = nd (rng);
    gate.process (hiss.data(), (int) hiss.size());
    CHECK (rms (hiss, 24000) < 1e-6, "gate did not close on hiss (rms %g)", rms (hiss, 24000));

    auto note = guitarLikeSignal (24000, 48000.0);
    const auto original = note;
    gate.process (note.data(), (int) note.size());
    CHECK (rms (note, 2000) > 0.9 * rms (original, 2000), "gate attenuated a played note");
}

void testSignalChainSwap()
{
    // Device at 44.1 kHz with a 48 kHz NAM model -> resampling path; swap captures mid-stream.
    const juce::File dir (GSP_NAM_EXAMPLES);
    const auto a = dir.getChildFile ("wavenet.nam"), b = dir.getChildFile ("lstm.nam");
    if (! a.existsAsFile() || ! b.existsAsFile())
    {
        ++skipped;
        return;
    }

    gsp::SignalChain chain;
    chain.gateEnabled = false;
    const int block = 64;
    chain.prepare (44100.0, block);

    juce::String error;
    CHECK (chain.loadCapture (a, error), "%s", error.toRawUTF8());
    std::printf ("  %s | %s\n", chain.getCaptureName().toRawUTF8(), chain.getCaptureDescription().toRawUTF8());

    const auto in = guitarLikeSignal (44100 * 2, 44100.0);
    std::vector<float> outL (in.size()), outR (in.size());
    float maxJump = 0.0f;

    for (size_t s = 0; s + block <= in.size(); s += block)
    {
        if (s == 44100)
            CHECK (chain.loadCapture (b, error), "%s", error.toRawUTF8());
        if (s == 66150)
            chain.clearCapture();
        float* outs[] = { outL.data() + s, outR.data() + s };
        chain.process (in.data() + s, outs, 2, block);
        chain.collectGarbage();
    }
    for (size_t i = 1; i < outL.size(); ++i)
        maxJump = std::max (maxJump, std::abs (outL[i] - outL[i - 1]));

    CHECK (allFinite (outL), "NaN in chain output");
    CHECK (rms (outL) > 1e-4, "chain output silent");
    CHECK (maxAbsDiff (outL, outR) == 0.0f, "L/R differ in mono chain");
    std::printf ("  rms=%.4f  largest sample-to-sample step=%.3f\n", rms (outL), maxJump);
    CHECK (maxJump < 0.5f, "click/discontinuity of %g", maxJump);
}

} // namespace

int main()
{
    runTest ("NAM captures match NeuralAmpModelerCore", testNamModels);
    runTest ("AIDA-X capture matches RTNeural", testAidaModel);
    runTest ("Invalid files are rejected with a message", testRejectsGarbage);
    runTest ("Sample-rate conversion 44.1 <-> 48 kHz", testResampler);
    runTest ("Noise gate", testGate);
    runTest ("Signal chain: resampling, capture swap, clear", testSignalChainSwap);

    std::printf ("\n%d checks, %d failures, %d skipped\n", checks, failures, skipped);
    return failures == 0 ? 0 : 1;
}
