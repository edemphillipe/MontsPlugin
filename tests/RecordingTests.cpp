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

// play the project with a silent live input and return where the loudest sample of the
// left channel is and how loud it is
std::pair<int, float> playPeak (StudioEngine& engine, Project& project, int length)
{
    GainChain silent;
    silent.left = silent.right = 0.0f;
    project.seek (0);
    project.play();
    const auto [l, r] = runEngine (engine, silent, std::vector<float> ((size_t) length, 0.0f));
    project.stop();
    const int at = argmaxAbs (l);
    return { at, l[(size_t) at] };
}

void testTakesAndTone()
{
    StudioEngine engine;
    engine.prepare (fs, block);
    Project project (engine, false);
    juce::String err;
    const auto dir = freshFolder ("gsp_rec_takes");
    project.open (dir, err);

    // the app hands over its whole board; here a stand-in "tone" that names the sound
    juce::String toneName = "Clean";
    project.toneProvider = [&]
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", toneName);
        return juce::var (o);
    };

    GainChain chain;
    chain.left = chain.right = 0.5f;
    CHECK (project.record (-1, err), "%s", err.toRawUTF8());
    runEngine (engine, chain, impulseAt (8000, 1000));
    project.stop();
    {
        const auto& t = project.session().tracks[0];
        CHECK (t.tone["name"].toString() == "Clean", "take 1 keeps its tone (%s)", t.tone["name"].toString().toRawUTF8());
        CHECK (t.takeLabel.startsWith (juce::String::fromUTF8 ("Gravação")) && t.history.empty(), "label '%s'", t.takeLabel.toRawUTF8());
    }

    // record over track 1 with another sound: the first take goes to the history
    toneName = "Lead";
    chain.left = chain.right = 0.25f;
    project.seek (0);
    CHECK (project.record (0, err), "%s", err.toRawUTF8());
    runEngine (engine, chain, impulseAt (8000, 3000));
    project.stop();
    {
        const auto& t = project.session().tracks[0];
        CHECK (t.history.size() == 1 && t.history[0].tone["name"].toString() == "Clean", "first take in history");
        CHECK (t.tone["name"].toString() == "Lead", "current take has the new tone");
        if (t.history.size() == 1)
            CHECK (project.fileFor (t.history[0].wetFile).existsAsFile() && project.fileFor (t.history[0].diFile).existsAsFile(), "old files kept");
    }
    auto [at2, v2] = playPeak (engine, project, 10000);
    CHECK (at2 == 3000 && std::abs (v2 - 0.25f) < 1.0e-4f, "plays take 2: %d %g", at2, (double) v2);

    // re-amp: the second take goes to the history too, the re-amp gets the sound in use
    toneName = "Crunch";
    chain.left = chain.right = 0.125f;
    CHECK (project.reamp (0, err), "%s", err.toRawUTF8());
    std::vector<float> l (block), r (block), z (block, 0.0f);
    for (int b = 0; b < 5000 && ! engine.reachedEnd(); ++b)
        engine.process (z.data(), l.data(), r.data(), block, chain);
    project.poll();
    {
        const auto& t = project.session().tracks[0];
        CHECK (t.history.size() == 2 && t.tone["name"].toString() == "Crunch" && t.takeLabel.startsWith ("Reamp"),
               "reamp is a new take (%d in history, %s)", (int) t.history.size(), t.takeLabel.toRawUTF8());
    }

    // go back to the very first take: it plays again, the re-amp stays in the list
    CHECK (project.useTake (0, 0), "useTake");
    {
        const auto& t = project.session().tracks[0];
        CHECK (t.tone["name"].toString() == "Clean" && t.history.size() == 2, "first take back");
        CHECK (! t.history.empty() && t.history.back().tone["name"].toString() == "Crunch", "re-amp went to the history");
    }
    auto [at1, v1] = playPeak (engine, project, 10000);
    CHECK (at1 == 1000 && std::abs (v1 - 0.5f) < 1.0e-4f, "plays take 1 again: %d %g", at1, (double) v1);
    CHECK (! project.useTake (0, 5) && ! project.useTake (3, 0), "bad indices refused");

    // all of it survives closing and reopening
    project.close();
    CHECK (! project.isOpen(), "closed");
    CHECK (project.open (dir, err), "%s", err.toRawUTF8());
    {
        const auto& t = project.session().tracks[0];
        CHECK (t.tone["name"].toString() == "Clean" && t.history.size() == 2, "reopened: %d takes", (int) t.history.size());
        if (t.history.size() == 2)
            CHECK (t.history[0].tone["name"].toString() == "Lead" && t.history[1].wetFile.contains ("reamp"), "history order");
    }
    auto [at1b, v1b] = playPeak (engine, project, 10000);
    CHECK (at1b == 1000, "reopened plays take 1 (%d)", at1b);
}

void testRenameAndTrash()
{
    StudioEngine engine;
    engine.prepare (fs, block);
    Project project (engine, false);
    juce::String err;
    const auto a = freshFolder ("gsp_rec_name_a");
    const auto b = freshFolder ("gsp_rec_name_b");
    const auto c = freshFolder (juce::String::fromUTF8 ("gsp_rec_name_ç").toRawUTF8());
    project.open (a, err);
    GainChain chain;
    project.record (-1, err);
    runEngine (engine, chain, impulseAt (6000, 500));
    project.stop();
    // reference music inside the project folder
    const auto ref = a.getChildFile ("ref.wav");
    {
        std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (ref);
        juce::WavAudioFormat wav;
        auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (fs).withNumChannels (2).withBitsPerSample (24));
        juce::AudioBuffer<float> buf (2, 4800);
        buf.clear();
        w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
    }
    CHECK (project.setBacking (ref, err), "%s", err.toRawUTF8());
    project.play(); // renaming while playing must stop and close the files first

    CHECK (project.rename ("gsp_rec_name_b", err), "%s", err.toRawUTF8());
    CHECK (project.getFolder() == b && ! a.exists() && b.getChildFile ("session.json").existsAsFile(), "folder renamed");
    CHECK (project.session().tracks.size() == 1, "tracks kept");
    CHECK (juce::File (project.session().backingFile) == b.getChildFile ("ref.wav"), "reference music moved with the project: %s",
           project.session().backingFile.toRawUTF8());
    CHECK (project.getLastNotice().isEmpty() || ! project.getLastNotice().contains ("encontrado"), "no missing-file notice");
    auto [at, v] = playPeak (engine, project, 8000);
    CHECK (at == 500, "plays after rename (%d)", at);

    // a name that is taken is refused and nothing changes
    c.createDirectory();
    CHECK (! project.rename (juce::String::fromUTF8 ("gsp_rec_name_ç"), err) && err.isNotEmpty(), "existing name refused");
    CHECK (project.getFolder() == b && project.isOpen(), "still open after refusal");
    c.deleteRecursively();

    const bool trashed = project.moveToTrash (err);
    std::printf ("  move to trash: %s %s\n", trashed ? "ok" : "failed", err.toRawUTF8());
    CHECK (trashed && ! b.exists() && ! project.isOpen(), "project in the trash");
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
    runTest ("Recording: take history and the tone of each take", testTakesAndTone);
    runTest ("Recording: rename and delete a project", testRenameAndTrash);
}
