#pragma once

// Real-time side of the recording studio (Phase 3).
//
// Every audio block goes through StudioEngine::process():
//
//   chain input = live guitar   (or, while re-amping, the recorded DI of one track)
//   chain output (the guitar sound you hear) -> recorded to disk while recording/re-amping
//   output = chain output + every track (volume, pan, mute, solo) + reference music + click
//
// Threading: the UI thread builds a Graph (one FilePlayer per audio file) and hands it over
// while the transport is stopped. The audio thread only touches the graph while holding a
// SpinLock with try-lock, so it never waits: if the UI thread holds the lock for a moment the
// block simply plays the live guitar alone. Recording goes through JUCE ThreadedWriters (lock
// free FIFO, written to disk on a background thread).

#include <juce_audio_formats/juce_audio_formats.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace gsp::studio
{

constexpr int maxTracks = 8;

// One audio file placed on the timeline, read in device samples (resampled if the file
// has another sample rate). With a TimeSliceThread it reads ahead on that thread
// (for the audio callback); without one it reads directly (offline rendering, tests).
class FilePlayer
{
public:
    FilePlayer (std::unique_ptr<juce::AudioFormatReader> reader, double deviceRate, int maxBlock,
                juce::TimeSliceThread* thread, juce::int64 startSample);
    ~FilePlayer();

    // UI thread, transport stopped
    void seek (juce::int64 timelinePosition);
    void waitUntilReady();

    // audio thread: overwrite l/r with the file's audio for timeline samples [pos, pos+n)
    void read (juce::int64 pos, float* l, float* r, int n) noexcept;

    juce::int64 getStart() const noexcept { return start; }
    juce::int64 getLength() const noexcept { return length; }
    juce::int64 getEnd() const noexcept { return start + length; }

private:
    std::unique_ptr<juce::AudioFormatReader> readerKeepAlive;
    std::unique_ptr<juce::AudioFormatReaderSource> source;
    std::unique_ptr<juce::BufferingAudioSource> buffering;
    std::unique_ptr<juce::ResamplingAudioSource> resampler;
    juce::AudioSource* top = nullptr;
    juce::PositionableAudioSource* positionable = nullptr;
    double ratio = 1.0; // file samples per device sample
    juce::int64 start = 0, length = 0;
    int maxBlock = 512;
};

class StudioEngine
{
public:
    // The guitar processing (the SignalChain in the app, anything in tests).
    struct Chain
    {
        virtual ~Chain() = default;
        virtual void run (const float* in, float* outL, float* outR, int n) noexcept = 0;
    };

    struct Graph
    {
        std::array<std::unique_ptr<FilePlayer>, maxTracks> tracks; // processed sound of each track
        std::unique_ptr<FilePlayer> backing;                       // reference music
        std::unique_ptr<FilePlayer> reampSource;                   // DI being re-amped
    };

    struct TrackParams
    {
        std::atomic<float> volumeDb { 0.0f }, pan { 0.0f };
        std::atomic<bool> mute { false }, solo { false };
    };

    enum class State { stopped, playing, recording, reamping };

    StudioEngine();
    ~StudioEngine();

    // Before audio starts / when the device changes. Stops the transport and drops the graph.
    void prepare (double sampleRate, int maxBlockSize);
    double getSampleRate() const noexcept { return sampleRate; }
    int getMaxBlock() const noexcept { return maxBlock; }

    // audio thread
    void process (const float* liveIn, float* outL, float* outR, int n, Chain& chain) noexcept;

    // UI thread (transport must be stopped for setGraph/seek)
    void setGraph (std::unique_ptr<Graph> g);
    void seek (juce::int64 position);
    void play();
    // writers are written from the audio thread and must be deleted on the UI thread after stop()
    void record (std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> di,
                 std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> wet, int replacedTrack);
    void reamp (std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> wet, int track, juce::int64 endPosition);
    // Stops the transport. Returns the writers so the caller can close the files.
    struct Finished
    {
        State was = State::stopped;
        std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> di, wet;
        juce::int64 startPosition = 0, endPosition = 0;
        int track = -1;
    };
    Finished stop();

    State getState() const noexcept { return state.load(); }
    juce::int64 getPosition() const noexcept { return position.load(); }
    // set by the audio thread when a re-amp reached its end; the UI calls stop() to finish it
    bool reachedEnd() const noexcept { return endReached.load(); }
    juce::int64 droppedSamples() const noexcept { return dropped.load(); }

    std::array<TrackParams, maxTracks> trackParams;
    std::atomic<float> backingVolumeDb { -6.0f };
    std::atomic<bool> backingMute { false };
    std::atomic<bool> clickOn { false };
    std::atomic<float> clickVolumeDb { -12.0f };
    std::atomic<double> bpm { 120.0 };
    std::atomic<int> beatsPerBar { 4 };

private:
    void mixFile (FilePlayer& p, juce::int64 pos, float* outL, float* outR, int n, float gainL, float gainR,
                  float& lastL, float& lastR) noexcept;
    void addClick (juce::int64 pos, float* outL, float* outR, int n) noexcept;

    double sampleRate = 48000.0;
    int maxBlock = 512;
    juce::AudioBuffer<float> scratch; // wetL, wetR, fileL, fileR, di

    juce::SpinLock lock;
    std::unique_ptr<Graph> graph;
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> diWriter, wetWriter;
    int specialTrack = -1;       // armed (recording) or re-amped track, not played back
    juce::int64 startPosition = 0, reampEnd = 0;
    std::atomic<State> state { State::stopped };
    State mode = State::stopped; // what was started (guarded by lock); state may stop by itself
    std::atomic<juce::int64> position { 0 };
    std::atomic<bool> endReached { false };
    std::atomic<juce::int64> dropped { 0 };
    std::array<float, maxTracks> lastGainL {}, lastGainR {};
    float lastBackingL = 0.0f, lastBackingR = 0.0f;
};

} // namespace gsp::studio
