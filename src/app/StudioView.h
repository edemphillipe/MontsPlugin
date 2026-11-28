#pragma once

// The "GRAVAÇÃO" tab: transport, project, reference music and up to 8 tracks with
// waveforms and a small mixer per track (record-arm, mute, solo, volume, pan, re-amp).

#include "LookAndFeel.h"

#include "studio/Project.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace gsp::ui
{

class TrackHeader final : public juce::Component
{
public:
    TrackHeader();
    void paint (juce::Graphics&) override;
    void resized() override;

    juce::Label name;
    juce::TextButton arm { juce::String::fromUTF8 ("●") }, mute { "M" }, solo { "S" }, reamp { "Reamp" }, remove { "X" }, takes { "Takes" };
    juce::Slider volume, pan;
    bool selected = false;
};

class StudioView final : public juce::Component,
                         private juce::Timer,
                         private juce::ChangeListener
{
public:
    StudioView (studio::StudioEngine&, studio::Project&, juce::PropertiesFile&);
    ~StudioView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    void openInitialProject();   // last project, or a new "Projeto 1"
    void refresh();              // tracks/takes changed
    bool togglePlay();           // space bar
    void startRecording();

    // returns an error text when there is no audio device / input to record from
    std::function<juce::String()> checkAudio;
    // puts a take's sound (pedalboard, amp, cabinet...) back on the board
    std::function<void (const juce::var&)> onLoadTone;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    juce::File projectsRoot() const;
    void refreshProjectList();
    void openProject (const juce::File&);
    void newProject();
    void showProjectMenu();
    void renameProject();
    void deleteProject();
    void showTakesMenu (int track);
    void exportMix();
    void chooseReference();
    void updateTransportButtons();
    void notify (const juce::String& text, bool error = false);

    juce::Rectangle<int> lanesArea() const;  // waveform area of all lanes (with ruler on top)
    juce::Rectangle<int> laneRect (int i) const;
    double viewSeconds() const;
    float timeToX (double seconds) const;
    double xToTime (float x) const;

    studio::StudioEngine& engine;
    studio::Project& project;
    juce::PropertiesFile& settings;

    // top bar
    juce::ComboBox projectSelect;
    juce::TextButton newProjectButton { "Novo projeto" }, projectMenuButton { juce::String::fromUTF8 ("Projeto ▾") };
    juce::TextButton toStartButton { "|<" }, playButton { "Tocar" }, stopButton { "Parar" }, recordButton { "Gravar" };
    juce::Label timeLabel;
    juce::ToggleButton clickToggle { juce::String::fromUTF8 ("Metrônomo") };
    juce::Slider clickVolume;
    juce::TextButton exportButton { "Exportar mix" };

    // reference music
    juce::TextButton refButton { juce::String::fromUTF8 ("Música de referência...") }, refRemove { "Remover" };
    juce::Label refName;
    juce::Slider refVolume;
    juce::ToggleButton refMute { "Mudo" };

    juce::OwnedArray<TrackHeader> headers;
    juce::AudioThumbnailCache thumbCache { 32 };
    juce::OwnedArray<juce::AudioThumbnail> thumbs;
    std::unique_ptr<juce::AudioThumbnail> refThumb;
    juce::Label noticeLabel;
    std::unique_ptr<juce::FileChooser> chooser;

    int armed = -1;          // track to record over (-1: a new track)
    bool needsRefresh = false;
    int reampingTrack = -1;
    double takeStartSeconds = 0.0;
    juce::Rectangle<int> refArea, tracksArea;
    int laneHeight = 52;
    double lastViewSeconds = 30.0;

    static constexpr int headerWidth = 300, rulerHeight = 18, refHeight = 52;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StudioView)
};

} // namespace gsp::ui
