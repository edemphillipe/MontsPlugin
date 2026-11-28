#pragma once

// A recording project: a folder with session.json and the recorded takes.
//
//   <folder>/session.json         tracks, mixer, reference music, click
//   <folder>/Takes/*.wav          every take: the clean guitar (DI, mono) and the
//                                 processed sound (stereo), 24-bit WAV
//
// Takes are never overwritten or deleted by recording again or re-amping: a new take gets
// new files and the track points to them.
//
// All methods run on the UI thread.

#include "studio/StudioEngine.h"

#include <functional>

namespace gsp::studio
{

// One take: the files plus the sound (whole pedalboard, amp, cabinet...) it was made with.
struct TakeInfo
{
    juce::String diFile, wetFile;
    double diStartSeconds = 0.0, wetStartSeconds = 0.0;
    juce::var tone;      // board state when it was recorded / re-amped (void if unknown)
    juce::String label;  // e.g. "Gravação 27/11 23:10"
};

struct TrackInfo
{
    juce::String name;
    juce::String diFile, wetFile; // relative to the project folder
    double diStartSeconds = 0.0, wetStartSeconds = 0.0; // where each file starts on the timeline
    juce::var tone;
    juce::String takeLabel;
    std::vector<TakeInfo> history; // earlier takes of this track, newest last

    TakeInfo current() const { return { diFile, wetFile, diStartSeconds, wetStartSeconds, tone, takeLabel }; }
    void setCurrent (const TakeInfo& t)
    {
        diFile = t.diFile; wetFile = t.wetFile; diStartSeconds = t.diStartSeconds; wetStartSeconds = t.wetStartSeconds;
        tone = t.tone; takeLabel = t.label;
    }
    float volumeDb = 0.0f, pan = 0.0f;
    bool mute = false, solo = false;
};

struct SessionInfo
{
    std::vector<TrackInfo> tracks;
    juce::String backingFile; // reference music (absolute path)
    float backingVolumeDb = -6.0f;
    bool backingMute = false;
    bool clickOn = false;
    float clickVolumeDb = -12.0f;
    int nextTake = 1;

    juce::var toVar() const;
    static SessionInfo fromVar (const juce::var&);
};

class Project
{
public:
    // realtimePlayback: read ahead on a background thread (the app). Tests and the
    // mix export read the files directly.
    Project (StudioEngine& engine, bool realtimePlayback = true);
    ~Project();

    bool open (const juce::File& folder, juce::String& error); // creates it if needed
    void close();                                                     // stop and release every file
    bool rename (const juce::String& newName, juce::String& error);   // renames the folder, stays open
    bool moveToTrash (juce::String& error);                           // the whole folder goes to the trash
    bool isOpen() const { return folder != juce::File(); }
    const juce::File& getFolder() const { return folder; }
    SessionInfo& session() { return info; }
    void save();

    void pushMixer();                    // session mixer -> engine
    bool rebuild (juce::String& error);  // (re)load the audio files; transport must be stopped
    void setLatencySamples (int samples) { latency = juce::jmax (0, samples); }

    // transport
    void play();
    void stop();                         // also finishes a recording / re-amp
    void seek (juce::int64 position);
    bool record (int track, juce::String& error); // track -1 = new track
    bool reamp (int track, juce::String& error);
    void poll();                         // UI timer: finishes a re-amp that reached its end

    bool removeTrack (int track);
    bool useTake (int track, int historyIndex); // make an earlier take the current one (the current goes to the history)
    // the sound in use right now, stored with every new take (set by the app)
    std::function<juce::var()> toneProvider;
    bool setBacking (const juce::File& file, juce::String& error);
    void clearBacking();

    juce::int64 lengthSamples() const;   // end of the last track (or of the reference music)
    bool exportMix (const juce::File& out, juce::String& error, float* peakOut = nullptr);

    juce::File fileFor (const juce::String& relative) const { return folder.getChildFile (relative); }
    juce::AudioFormatManager& getFormats() { return formats; }
    juce::String getLastNotice() const { return notice; }

    std::function<void()> onChanged; // takes or tracks changed (after stop/finish)

private:
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> makeWriter (const juce::File& f, int channels, juce::String& error);
    std::unique_ptr<FilePlayer> makePlayer (const juce::File& f, juce::int64 start, bool realtime, double rate, int block);
    std::unique_ptr<StudioEngine::Graph> buildGraph (bool realtime, double rate, int block, int reampTrack, juce::String& error);
    void finish (StudioEngine::Finished f);
    void pushMixerTo (StudioEngine& e) const;
    juce::int64 graphLength = 0;
    juce::String newTakeName (const juce::String& suffix);

    StudioEngine& engine;
    bool realtime;
    juce::TimeSliceThread io { "Guitar Studio Pro disk" };
    juce::AudioFormatManager formats;
    juce::File folder;
    SessionInfo info;
    int latency = 0;
    juce::String pendingDi, pendingWet; // files of the take being recorded
    juce::var pendingTone;
    static juce::String takeLabelNow (const juce::String& kind);
    juce::String notice;
};

} // namespace gsp::studio
