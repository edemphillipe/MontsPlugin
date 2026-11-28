// Recording studio (Phase 3): multitrack recording, latency compensation, playback,
// mixer, re-amp, click, reference music and mix export.

#include "TestUtil.h"

#include "engine/SignalChain.h"
#include "studio/Project.h"

using namespace gsptest;
using gsp::studio::Project;
using gsp::studio::StudioEngine;

namespace
{
constexpr double fs = 48000.0;
constexpr int block = 256;

// stands in for the guitar chain: a known gain on each side
struct GainChain final : StudioEngine::Chain
{
    float left = 0.5f, right = 0.5f;
    void run (const float* in, float* l, float* r, int n) noexcept override
    {
        for (int i = 0; i < n; ++i)
        {
            l[i] = in[i] * left;
            r[i] = in[i] * right;
        }
    }
};

juce::File freshFolder (const char* name)
{
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (name);
    f.deleteRecursively();
    return f;
}

// run the engine over `input` (live guitar), return the stereo output
std::pair<std::vector<float>, std::vector<float>> runEngine (StudioEngine& e, StudioEngine::Chain& chain, const std::vector<float>& input)
{
    std::vector<float> l (input.size()), r (input.size());
    for (size_t s = 0; s < input.size(); s += block)
    {
        const int n = (int) std::min ((size_t) block, input.size() - s);
        e.process (input.data() + s, l.data() + s, r.data() + s, n, chain);
    }
    return { l, r };
}

std::vector<float> impulseAt (int n, int at, float amp = 1.0f)
{
    std::vector<float> x ((size_t) n, 0.0f);
    x[(size_t) at] = amp;
    return x;
}

int argmaxAbs (const std::vector<float>& v)
{
    int best = 0;
    for (size_t i = 1; i < v.size(); ++i)
        if (std::abs (v[i]) > std::abs (v[(size_t) best]))
            best = (int) i;
    return best;
}

std::vector<float> readChannel (const juce::File& f, int channel, double* rateOut = nullptr)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr)
        return {};
    juce::AudioBuffer<float> b ((int) r->numChannels, (int) r->lengthInSamples);
    r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
    if (rateOut != nullptr)
        *rateOut = r->sampleRate;
    const int ch = juce::jmin (channel, b.getNumChannels() - 1);
    return std::vector<float> (b.getReadPointer (ch), b.getReadPointer (ch) + b.getNumSamples());
}

void testRecordAndPlayBack()
{
    StudioEngine engine;
    engine.prepare (fs, block);
    Project project (engine, false);
    juce::String err;
    CHECK (project.open (freshFolder ("gsp_rec_test1"), err), "%s", err.toRawUTF8());
    project.setLatencySamples (100); // the interface reports 100 samples round trip

    GainChain chain;
    // record one second: a click at 1000 on the live input
    CHECK (project.record (-1, err), "%s", err.toRawUTF8());
    runEngine (engine, chain, impulseAt ((int) fs, 1000));
    project.stop();
    CHECK (project.session().tracks.size() == 1, "tracks %d", (int) project.session().tracks.size());
    const auto& t = project.session().tracks[0];
    CHECK (std::abs (t.diStartSeconds * fs + 100.0) < 0.01, "start %.3f samples", t.diStartSeconds * fs);
    CHECK (engine.droppedSamples() == 0, "dropped %lld", (long long) engine.droppedSamples());

    // the files: DI = the raw input, processed = the chain output
    const auto di = readChannel (project.fileFor (t.diFile), 0);
    const auto wetL = readChannel (project.fileFor (t.wetFile), 0);
    const auto wetR = readChannel (project.fileFor (t.wetFile), 1);
    CHECK (di.size() == (size_t) fs && wetL.size() == (size_t) fs, "lengths %d %d", (int) di.size(), (int) wetL.size());
    CHECK (argmaxAbs (di) == 1000 && std::abs (di[1000] - 1.0f) < 1.0e-5f, "DI peak at %d = %g", argmaxAbs (di), di.empty() ? 0.0 : (double) di[1000]);
    CHECK (std::abs (wetL[1000] - 0.5f) < 1.0e-5f && std::abs (wetR[1000] - 0.5f) < 1.0e-5f, "wet %g %g", (double) wetL[1000], (double) wetR[1000]);

    // play it back: it must come out 100 samples earlier, where it was heard
    project.seek (0);
    project.play();
    GainChain mutedLive;
    mutedLive.left = mutedLive.right = 0.0f;
    const auto [l, r] = runEngine (engine, mutedLive, std::vector<float> ((size_t) fs, 0.0f));
    project.stop();
    std::printf ("  recorded click plays back at sample %d (expected 900 = 1000 - 100 latency)\n", argmaxAbs (l));
    CHECK (argmaxAbs (l) == 900 && argmaxAbs (r) == 900, "played at %d / %d", argmaxAbs (l), argmaxAbs (r));
    CHECK (std::abs (l[900] - 0.5f) < 1.0e-4f, "level %g", (double) l[900]);
    CHECK (project.lengthSamples() == (juce::int64) fs - 100, "length %lld", (long long) project.lengthSamples());

    // reopen the project from disk: the track is still there
    StudioEngine engine2;
    engine2.prepare (fs, block);
    Project again (engine2, false);
    CHECK (again.open (project.getFolder(), err), "%s", err.toRawUTF8());
    CHECK (again.session().tracks.size() == 1 && again.session().tracks[0].wetFile == t.wetFile, "reopened");
}

void testOverdubAndMixer()
{
    StudioEngine engine;
    engine.prepare (fs, block);
    Project project (engine, false);
    juce::String err;
    project.open (freshFolder ("gsp_rec_test2"), err);
    GainChain chain;

    // track 1: click at 1000; track 2 (overdub while track 1 plays): click at 3000
    project.record (-1, err);
    runEngine (engine, chain, impulseAt (8000, 1000));
    project.stop();
    project.seek (0);
    project.record (-1, err);
    const auto [hearL, hearR] = runEngine (engine, chain, impulseAt (8000, 3000));
    project.stop();
    CHECK (project.session().tracks.size() == 2, "tracks %d", (int) project.session().tracks.size());
    // while recording track 2 you hear track 1 plus yourself
    CHECK (std::abs (hearL[1000] - 0.5f) < 1.0e-4f && std::abs (hearL[3000] - 0.5f) < 1.0e-4f, "monitor %g %g", (double) hearL[1000], (double) hearL[3000]);

    GainChain silent;
    silent.left = silent.right = 0.0f;
    const std::vector<float> zeros (8000, 0.0f);
    auto playback = [&]
    {
        project.pushMixer();
        project.seek (0);
        project.play();
        auto out = runEngine (engine, silent, zeros);
        project.stop();
        return out;
    };

    auto& tr = project.session().tracks;
    tr[0].volumeDb = -6.0f;
    tr[1].pan = -1.0f; // hard left
    {
        const auto [l, r] = playback();
        CHECK (std::abs (l[1000] - 0.5f * 0.50119f) < 1.0e-4f && std::abs (r[1000] - 0.5f * 0.50119f) < 1.0e-4f, "-6 dB %g %g", (double) l[1000], (double) r[1000]);
        CHECK (std::abs (l[3000] - 0.5f) < 1.0e-4f && std::abs (r[3000]) < 1.0e-6f, "pan left %g %g", (double) l[3000], (double) r[3000]);
    }
    tr[1].solo = true;
    {
        const auto [l, r] = playback();
        CHECK (std::abs (l[1000]) < 1.0e-6f && std::abs (l[3000] - 0.5f) < 1.0e-4f, "solo %g %g", (double) l[1000], (double) l[3000]);
    }
    tr[1].solo = false;
    tr[1].mute = true;
    {
        const auto [l, r] = playback();
        CHECK (std::abs (l[3000]) < 1.0e-6f && std::abs (l[1000]) > 0.2f, "mute %g %g", (double) l[3000], (double) l[1000]);
    }
    tr[1].mute = false;

    // record over track 1: during the take, the old take is silent; afterwards the new one plays
    const auto oldWet = tr[0].wetFile;
    project.seek (0);
    project.record (0, err);
    const auto [overL, overR] = runEngine (engine, chain, impulseAt (8000, 5000));
    project.stop();
    CHECK (std::abs (overL[1000]) < 1.0e-6f, "old take audible while replacing it: %g", (double) overL[1000]);
    CHECK (project.session().tracks.size() == 2 && project.session().tracks[0].wetFile != oldWet, "replaced");
    CHECK (project.fileFor (oldWet).existsAsFile(), "old take kept on disk");
    {
        tr[0].volumeDb = 0.0f;
        const auto [l, r] = playback();
        CHECK (std::abs (l[1000]) < 1.0e-6f && std::abs (l[5000] - 0.5f) < 1.0e-4f, "new take %g %g", (double) l[1000], (double) l[5000]);
    }

    // export: same as playback, written to a 24-bit WAV
    const auto mixFile = project.getFolder().getChildFile ("mix.wav");
    float peak = 0.0f;
    CHECK (project.exportMix (mixFile, err, &peak), "%s", err.toRawUTF8());
    const auto mixL = readChannel (mixFile, 0), mixR = readChannel (mixFile, 1);
    CHECK (mixL.size() == 8000, "mix length %d", (int) mixL.size());
    CHECK (mixL.size() == 8000 && std::abs (mixL[3000] - 0.5f) < 1.0e-4f && std::abs (mixR[3000]) < 1.0e-5f && std::abs (mixL[5000] - 0.5f) < 1.0e-4f,
           "mix content");
    CHECK (std::abs (peak - 0.5f) < 1.0e-4f, "peak %g", (double) peak);

    // remove a track: the other one stays, the files stay
    const auto removedWet = tr[1].wetFile;
    CHECK (project.removeTrack (1) && project.session().tracks.size() == 1, "remove");
    CHECK (project.fileFor (removedWet).existsAsFile(), "removed track's take kept on disk");
}

void testReamp()
{
    StudioEngine engine;
    engine.prepare (fs, block);
    Project project (engine, false);
    juce::String err;
    project.open (freshFolder ("gsp_rec_test3"), err);
    project.setLatencySamples (64);

    GainChain chain; // "amp A"
    project.record (-1, err);
    runEngine (engine, chain, impulseAt (12000, 2000));
    project.stop();

    // change the sound ("amp B") and re-amp the clean recording through it,
    // while somebody keeps playing the live guitar: that must not leak in
    chain.left = 0.25f;
    chain.right = -0.125f;
    CHECK (project.reamp (0, err), "%s", err.toRawUTF8());
    std::vector<float> noise (block);
    std::mt19937 rng (3);
    std::uniform_real_distribution<float> u (-0.5f, 0.5f);
    std::vector<float> l (block), r (block);
    int blocks = 0;
    while (! engine.reachedEnd() && blocks < 5000)
    {
        for (auto& s : noise)
            s = u (rng);
        engine.process (noise.data(), l.data(), r.data(), block, chain);
        ++blocks;
    }
    project.poll();
    CHECK (engine.getState() == StudioEngine::State::stopped && blocks < 5000, "re-amp stops by itself after the DI + 3 s (%d blocks)", blocks);
    const auto& t = project.session().tracks[0];
    CHECK (t.wetFile.contains ("reamp"), "track now uses the re-amped take: %s", t.wetFile.toRawUTF8());

    // a re-amp stopped half way is thrown away: the track keeps the finished re-amp
    const auto keptWet = t.wetFile;
    CHECK (project.reamp (0, err), "%s", err.toRawUTF8());
    runEngine (engine, chain, std::vector<float> (4096, 0.0f));
    project.stop();
    CHECK (project.session().tracks[0].wetFile == keptWet, "cancelled re-amp replaced the track");

    GainChain silent;
    silent.left = silent.right = 0.0f;
    project.seek (0);
    project.play();
    const auto [pl, pr] = runEngine (engine, silent, std::vector<float> (16000, 0.0f));
    project.stop();
    // same place as before (2000 - 64), new sound, nothing from the live noise
    std::printf ("  re-amped click at %d, L %.4f R %.4f\n", argmaxAbs (pl), (double) pl[1936], (double) pr[1936]);
    CHECK (argmaxAbs (pl) == 1936, "position %d", argmaxAbs (pl));
    CHECK (std::abs (pl[1936] - 0.25f) < 1.0e-4f && std::abs (pr[1936] + 0.125f) < 1.0e-4f, "new sound %g %g", (double) pl[1936], (double) pr[1936]);
    double leak = 0.0;
    for (size_t i = 0; i < pl.size(); ++i)
        if (i != 1936)
            leak = std::max (leak, (double) std::abs (pl[i]));
    CHECK (leak < 1.0e-5, "live guitar leaked into the re-amp: %g", leak);
}

void testClickAndReference()
{
    StudioEngine engine;
    engine.prepare (fs, block);
    Project project (engine, false);
    juce::String err;
    project.open (freshFolder ("gsp_rec_test4"), err);

    // reference music at 44.1 kHz with a click at exactly 1.0 s
    const auto refFile = project.getFolder().getChildFile ("ref44.wav");
    {
        std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (refFile);
        juce::WavAudioFormat wav;
        auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (44100.0).withNumChannels (2).withBitsPerSample (24));
        juce::AudioBuffer<float> b (2, 88200);
        b.clear();
        b.setSample (0, 44100, 0.8f);
        b.setSample (1, 44100, 0.8f);
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }
    CHECK (project.setBacking (refFile, err), "%s", err.toRawUTF8());
    project.session().backingVolumeDb = 0.0f;
    project.pushMixer();

    GainChain silent;
    silent.left = silent.right = 0.0f;
    project.seek (0);
    project.play();
    const auto [l, r] = runEngine (engine, silent, std::vector<float> (96000, 0.0f));
    project.stop();
    std::printf ("  44.1 kHz reference: click at 1 s lands on sample %d at 48 kHz\n", argmaxAbs (l));
    CHECK (std::abs (argmaxAbs (l) - 48000) <= 2, "reference timing %d", argmaxAbs (l));

    project.session().backingMute = true;
    project.session().clickOn = true;
    project.pushMixer();
    engine.bpm = 120.0; // one beat = 24000 samples
    project.seek (0);
    project.play();
    const auto [cl, cr] = runEngine (engine, silent, std::vector<float> (96000, 0.0f));
    project.stop();
    auto energy = [&] (int from, int to) { return rms (cl, (size_t) from, (size_t) to); };
    CHECK (energy (24000, 24000 + 1400) > 0.01 && energy (48000, 48000 + 1400) > 0.01, "click on the beats");
    CHECK (energy (2000, 22000) < 1.0e-6 && energy (26000, 46000) < 1.0e-6, "silence between clicks (%g)", energy (2000, 22000));
    CHECK (energy (0, 1400) > energy (24000, 25400), "bar downbeat is louder");
    // the click is for you only: it is not in the exported mix
    project.session().backingMute = false;
    project.pushMixer();
    const auto mix = project.getFolder().getChildFile ("mix.wav");
    CHECK (project.exportMix (mix, err), "%s", err.toRawUTF8());
    const auto m = readChannel (mix, 0);
    CHECK (m.size() == 96000 && rms (m, 0, 40000) < 1.0e-6, "no click in export");
}

void testBufferedPlayback()
{
    // the app reads ahead on a background thread: same result as direct reading
    StudioEngine engine;
    engine.prepare (fs, block);
    juce::String err;
    const auto folder = freshFolder ("gsp_rec_test5");
    {
        Project direct (engine, false);
        direct.open (folder, err);
        GainChain chain;
        direct.record (-1, err);
        runEngine (engine, chain, impulseAt ((int) fs, 30000));
        direct.stop();
    }
    Project project (engine, true);
    CHECK (project.open (folder, err), "%s", err.toRawUTF8());
    GainChain silent;
    silent.left = silent.right = 0.0f;
    project.seek (20000); // start in the middle
    project.play();
    // pace the blocks like a real audio callback (256 samples = 5.3 ms)
    std::vector<float> l (24000), r (24000), zeros (block, 0.0f);
    for (int s = 0; s < 24000; s += block)
    {
        const int n = std::min (block, 24000 - s);
        engine.process (zeros.data(), l.data() + s, r.data() + s, n, silent);
        juce::Thread::sleep (5);
    }
    project.stop();
    CHECK (argmaxAbs (l) == 10000 && std::abs (l[10000] - 0.5f) < 1.0e-4f, "buffered playback from 20000: peak at %d", argmaxAbs (l));
}

void testSessionJson()
{
    gsp::studio::SessionInfo s;
    gsp::studio::TrackInfo t;
    t.name = "Solo";
    t.diFile = "Takes/Take001_DI.wav";
    t.wetFile = "Takes/Take002_reamp.wav";
    t.diStartSeconds = -0.002;
    t.wetStartSeconds = 0.0;
    t.volumeDb = -3.5f;
    t.pan = 0.25f;
    t.solo = true;
    s.tracks.push_back (t);
    s.backingFile = "/musica.mp3";
    s.clickOn = true;
    s.nextTake = 7;
    const auto back = gsp::studio::SessionInfo::fromVar (juce::JSON::parse (juce::JSON::toString (s.toVar())));
    CHECK (back.tracks.size() == 1 && back.tracks[0].name == "Solo" && back.tracks[0].wetFile == t.wetFile
               && std::abs (back.tracks[0].diStartSeconds + 0.002) < 1e-9 && std::abs (back.tracks[0].volumeDb + 3.5f) < 1e-6f
               && back.tracks[0].solo && ! back.tracks[0].mute && back.backingFile == "/musica.mp3" && back.clickOn && back.nextTake == 7,
           "round trip");
}


void testLiveSoundUnchanged()
{
    // the live guitar through the studio engine must sound exactly like the chain alone
    gsp::SignalChain direct, viaStudio;
    direct.prepare (fs, block);
    viaStudio.prepare (fs, block);
    direct.ampSource = viaStudio.ampSource = (int) gsp::SignalChain::sourceAmpModel;
    struct Adapter final : StudioEngine::Chain
    {
        gsp::SignalChain& c;
        explicit Adapter (gsp::SignalChain& x) : c (x) {}
        void run (const float* in, float* l, float* r, int n) noexcept override { float* o[] = { l, r }; c.process (in, o, 2, n); }
    } adapter (viaStudio);
    StudioEngine engine;
    engine.prepare (fs, block);
    const auto in = guitarLikeSignal ((int) fs, fs);
    std::vector<float> dl (in.size()), dr (in.size());
    for (size_t s = 0; s < in.size(); s += block)
    {
        float* o[] = { dl.data() + s, dr.data() + s };
        direct.process (in.data() + s, o, 2, (int) std::min ((size_t) block, in.size() - s));
    }
    const auto [sl, sr] = runEngine (engine, adapter, in);
    double maxDiff = 0.0;
    for (size_t i = 0; i < in.size(); ++i)
        maxDiff = std::max (maxDiff, (double) std::abs (dl[i] - sl[i]) + std::abs (dr[i] - sr[i]));
    std::printf ("  live sound: chain alone rms %.4f, through the studio rms %.4f, largest difference %g\n", rms (dl), rms (sl), maxDiff);
    CHECK (maxDiff < 1.0e-6 && rms (dl) > 0.01, "studio changes the live sound (%g)", maxDiff);
}

} // namespace

void runRecordingTests()
{
    runTest ("Recording: record, latency compensation, play back, reopen", testRecordAndPlayBack);
    runTest ("Recording: overdub, volume, pan, solo, mute, record over, export", testOverdubAndMixer);
    runTest ("Recording: re-amp through a new sound", testReamp);
    runTest ("Recording: reference music (44.1 kHz) and click", testClickAndReference);
    runTest ("Recording: read-ahead playback from the middle", testBufferedPlayback);
    runTest ("Recording: session file", testSessionJson);
    runTest ("Recording: live sound unchanged through the studio", testLiveSoundUnchanged);
}
