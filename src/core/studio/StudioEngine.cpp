#include "studio/StudioEngine.h"

#include <cmath>

namespace gsp::studio
{

// ================================================================ FilePlayer
FilePlayer::FilePlayer (std::unique_ptr<juce::AudioFormatReader> reader, double deviceRate, int maxBlockIn,
                        juce::TimeSliceThread* thread, juce::int64 startSample)
    : start (startSample), maxBlock (juce::jmax (1, maxBlockIn))
{
    jassert (reader != nullptr);
    ratio = reader->sampleRate > 0.0 ? reader->sampleRate / deviceRate : 1.0;
    length = (juce::int64) std::floor ((double) reader->lengthInSamples / ratio);

    source = std::make_unique<juce::AudioFormatReaderSource> (reader.release(), true);
    positionable = source.get();
    top = source.get();
    if (thread != nullptr)
    {
        buffering = std::make_unique<juce::BufferingAudioSource> (source.get(), *thread, false,
                                                                  (int) juce::jmax (32768.0, deviceRate * 2.0), 2, true);
        positionable = buffering.get();
        top = buffering.get();
    }
    if (std::abs (ratio - 1.0) > 1.0e-9)
    {
        resampler = std::make_unique<juce::ResamplingAudioSource> (top, false, 2);
        resampler->setResamplingRatio (ratio);
        top = resampler.get();
    }
    top->prepareToPlay (maxBlock, deviceRate);
}

FilePlayer::~FilePlayer()
{
    if (top != nullptr)
        top->releaseResources();
}

void FilePlayer::seek (juce::int64 timelinePosition)
{
    const auto local = juce::jmax ((juce::int64) 0, timelinePosition - start);
    positionable->setNextReadPosition ((juce::int64) std::llround ((double) local * ratio));
    if (resampler != nullptr)
        resampler->flushBuffers();
}

void FilePlayer::waitUntilReady()
{
    if (buffering == nullptr)
        return;
    // wait for a comfortable head start (about 0.25 s), not just the first block
    const int ahead = juce::jmax (maxBlock, 12000);
    juce::AudioBuffer<float> probe (2, ahead);
    juce::AudioSourceChannelInfo info (&probe, 0, ahead);
    buffering->waitForNextAudioBlockReady (info, 2000);
}

void FilePlayer::read (juce::int64 pos, float* l, float* r, int n) noexcept
{
    juce::FloatVectorOperations::clear (l, n);
    juce::FloatVectorOperations::clear (r, n);
    const auto local = pos - start;
    if (local + n <= 0 || local >= length)
        return;
    const int skip = local < 0 ? (int) -local : 0;
    const int count = (int) juce::jmin ((juce::int64) (n - skip), length - juce::jmax ((juce::int64) 0, local));
    if (count <= 0)
        return;
    float* channels[] = { l, r };
    juce::AudioBuffer<float> view (channels, 2, n);
    top->getNextAudioBlock (juce::AudioSourceChannelInfo (&view, skip, count));
}

// ================================================================ StudioEngine
StudioEngine::StudioEngine() { prepare (48000.0, 512); }

StudioEngine::~StudioEngine() = default;

void StudioEngine::prepare (double rate, int block)
{
    std::unique_ptr<Graph> oldGraph;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        state = State::stopped;
        sampleRate = rate > 0.0 ? rate : 48000.0;
        maxBlock = juce::jmax (16, block);
        scratch.setSize (5, maxBlock, false, true, false);
        oldGraph = std::move (graph); // the player positions/ratios are no longer valid
    }
}

void StudioEngine::setGraph (std::unique_ptr<Graph> g)
{
    jassert (state.load() == State::stopped);
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        std::swap (graph, g);
    }
    // the previous graph is released here, outside the lock, on the UI thread
}

void StudioEngine::seek (juce::int64 pos)
{
    jassert (state.load() == State::stopped);
    pos = juce::jmax ((juce::int64) 0, pos);
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        position = pos;
        if (graph != nullptr)
        {
            for (auto& t : graph->tracks)
                if (t != nullptr)
                    t->seek (pos);
            if (graph->backing != nullptr)
                graph->backing->seek (pos);
            if (graph->reampSource != nullptr)
                graph->reampSource->seek (pos);
        }
    }
    // the audio thread does not read the players while stopped, so waiting here is safe
    if (graph != nullptr)
    {
        for (auto& t : graph->tracks)
            if (t != nullptr)
                t->waitUntilReady();
        if (graph->backing != nullptr)
            graph->backing->waitUntilReady();
        if (graph->reampSource != nullptr)
            graph->reampSource->waitUntilReady();
    }
}

void StudioEngine::play()
{
    const juce::SpinLock::ScopedLockType sl (lock);
    specialTrack = -1;
    startPosition = position.load();
    endReached = false;
    lastGainL.fill (-1.0f);
    lastBackingL = -1.0f;
    state = State::playing;
    mode = state.load();
}

void StudioEngine::record (std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> di,
                           std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> wet, int replacedTrack)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    diWriter = std::move (di);
    wetWriter = std::move (wet);
    specialTrack = replacedTrack;
    startPosition = position.load();
    endReached = false;
    dropped = 0;
    lastGainL.fill (-1.0f);
    lastBackingL = -1.0f;
    state = State::recording;
    mode = state.load();
}

void StudioEngine::reamp (std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> wet, int track, juce::int64 endPosition)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    wetWriter = std::move (wet);
    specialTrack = track;
    startPosition = position.load();
    reampEnd = endPosition;
    endReached = false;
    dropped = 0;
    lastGainL.fill (-1.0f);
    lastBackingL = -1.0f;
    state = State::reamping;
    mode = state.load();
}

StudioEngine::Finished StudioEngine::stop()
{
    Finished f;
    const juce::SpinLock::ScopedLockType sl (lock);
    f.was = mode;
    mode = State::stopped;
    f.di = std::move (diWriter);
    f.wet = std::move (wetWriter);
    f.startPosition = startPosition;
    f.endPosition = position.load();
    f.track = specialTrack;
    specialTrack = -1;
    state = State::stopped;
    return f;
}

void StudioEngine::mixFile (FilePlayer& p, juce::int64 pos, float* outL, float* outR, int n, float gainL, float gainR,
                            float& lastL, float& lastR) noexcept
{
    float* fl = scratch.getWritePointer (2);
    float* fr = scratch.getWritePointer (3);
    p.read (pos, fl, fr, n);
    if (lastL < 0.0f)
    {
        lastL = gainL;
        lastR = gainR;
    }
    // short linear ramp from the previous block's gain: no zipper noise when moving a fader
    const float stepL = (gainL - lastL) / (float) n, stepR = (gainR - lastR) / (float) n;
    float gl = lastL, gr = lastR;
    for (int i = 0; i < n; ++i)
    {
        gl += stepL;
        gr += stepR;
        outL[i] += fl[i] * gl;
        outR[i] += fr[i] * gr;
    }
    lastL = gainL;
    lastR = gainR;
}

void StudioEngine::addClick (juce::int64 pos, float* outL, float* outR, int n) noexcept
{
    const double beat = 60.0 / juce::jlimit (20.0, 400.0, bpm.load (std::memory_order_relaxed)) * sampleRate;
    const int perBar = juce::jmax (1, beatsPerBar.load (std::memory_order_relaxed));
    const double clickLen = 0.03 * sampleRate;
    const float vol = juce::Decibels::decibelsToGain (clickVolumeDb.load (std::memory_order_relaxed));
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) (pos + i);
        const double beatIndex = std::floor (t / beat);
        const double inBeat = t - beatIndex * beat;
        if (inBeat >= clickLen)
            continue;
        const bool downbeat = ((juce::int64) beatIndex % perBar) == 0;
        const double f = downbeat ? 2000.0 : 1400.0;
        const float s = (float) (std::sin (juce::MathConstants<double>::twoPi * f * inBeat / sampleRate)
                                 * std::exp (-inBeat / (0.006 * sampleRate))) * vol * (downbeat ? 1.0f : 0.7f);
        outL[i] += s;
        outR[i] += s;
    }
}

namespace
{
// balance pan: the centre keeps unity gain on both sides
inline void panGains (float pan, float gain, float& l, float& r) noexcept
{
    pan = juce::jlimit (-1.0f, 1.0f, pan);
    l = gain * juce::jmin (1.0f, 1.0f - pan);
    r = gain * juce::jmin (1.0f, 1.0f + pan);
}
} // namespace

void StudioEngine::process (const float* liveIn, float* outL, float* outR, int n, Chain& chain) noexcept
{
    if (n > maxBlock)
    {
        for (int s = 0; s < n; s += maxBlock)
        {
            const int m = juce::jmin (maxBlock, n - s);
            process (liveIn + s, outL + s, outR + s, m, chain);
        }
        return;
    }

    const juce::SpinLock::ScopedTryLockType tl (lock);
    const State st = tl.isLocked() ? state.load (std::memory_order_relaxed) : State::stopped;
    const bool running = st != State::stopped;
    const auto pos = position.load (std::memory_order_relaxed);

    // ---- what goes into the guitar chain
    const float* in = liveIn;
    if (st == State::reamping)
    {
        float* di = scratch.getWritePointer (4);
        if (graph != nullptr && graph->reampSource != nullptr)
            graph->reampSource->read (pos, di, scratch.getWritePointer (2), n);
        else
            juce::FloatVectorOperations::clear (di, n);
        in = di;
    }

    chain.run (in, outL, outR, n);

    // ---- record
    if (st == State::recording || st == State::reamping)
    {
        const float* wet[] = { outL, outR };
        if (wetWriter != nullptr && ! wetWriter->write (wet, n))
            dropped.fetch_add (n, std::memory_order_relaxed);
        if (st == State::recording && diWriter != nullptr)
        {
            const float* di[] = { liveIn };
            if (! diWriter->write (di, n))
                dropped.fetch_add (n, std::memory_order_relaxed);
        }
    }

    if (! running)
        return;

    // ---- playback
    if (graph != nullptr)
    {
        bool anySolo = false;
        for (int t = 0; t < maxTracks; ++t)
            anySolo = anySolo || (graph->tracks[(size_t) t] != nullptr && trackParams[(size_t) t].solo.load (std::memory_order_relaxed));

        for (int t = 0; t < maxTracks; ++t)
        {
            auto& player = graph->tracks[(size_t) t];
            if (player == nullptr || t == specialTrack)
                continue;
            const auto& p = trackParams[(size_t) t];
            const bool audible = ! p.mute.load (std::memory_order_relaxed) && (! anySolo || p.solo.load (std::memory_order_relaxed));
            float gl, gr;
            panGains (p.pan.load (std::memory_order_relaxed),
                      audible ? juce::Decibels::decibelsToGain (p.volumeDb.load (std::memory_order_relaxed), -80.0f) : 0.0f, gl, gr);
            mixFile (*player, pos, outL, outR, n, gl, gr, lastGainL[(size_t) t], lastGainR[(size_t) t]);
        }

        if (graph->backing != nullptr)
        {
            const float g = backingMute.load (std::memory_order_relaxed) ? 0.0f
                                                                         : juce::Decibels::decibelsToGain (backingVolumeDb.load (std::memory_order_relaxed), -80.0f);
            mixFile (*graph->backing, pos, outL, outR, n, g, g, lastBackingL, lastBackingR);
        }
    }

    if (clickOn.load (std::memory_order_relaxed))
        addClick (pos, outL, outR, n);

    position.store (pos + n, std::memory_order_relaxed);
    if (st == State::reamping && pos + n >= reampEnd)
    {
        state = State::stopped; // stop reading; the UI finishes the take
        endReached = true;
    }
}

} // namespace gsp::studio
