#pragma once

#include "CaptureBrowser.h"
#include "PedalboardView.h"
#include "StudioView.h"
#include "TunerDisplay.h"
#include "LookAndFeel.h"
#include "Widgets.h"

#include "engine/SignalChain.h"
#include "presets/PresetStore.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>

namespace gsp::ui
{

class MainComponent final : public juce::Component,
                            private juce::AudioIODeviceCallback,
                            private juce::ChangeListener,
                            private juce::Timer
{
public:
    explicit MainComponent (juce::PropertiesFile& settings);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void tap();
    void setBpm (double bpm);
    // audio
    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                           float* const* outputs, int numOutputs, int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void showAudioSettings();
    void chooseCapture();
    void chooseImpulseResponse();
    void chooseLibraryFolder();
    void loadCapture (const juce::File&);
    void loadImpulseResponse (const juce::File&);
    void refreshInputChoices();
    void updateDeviceLabel();
    void showError (const juce::String& title, const juce::String& message);
    void saveState();
    void restoreState();
    void updateAmpControls();
    void updateAmpTile();

    // whole-board presets
    juce::var ampStateVar() const;
    void applyAmpState (const juce::var&);
    juce::var boardStateVar() const;        // everything a preset stores
    void applyBoardState (const juce::var&);
    void refreshPresetList (const juce::String& select = {});
    void savePresetAs();
    void deleteCurrentPreset();

    juce::PropertiesFile& settings;
    StudioLookAndFeel lookAndFeel;

    juce::AudioDeviceManager deviceManager;
    SignalChain chain;
    std::atomic<int> inputChannel { 0 };
    juce::HeapBlock<float> silence;
    int silenceSize = 0;

    // recording studio: the guitar chain runs inside the studio engine
    struct ChainAdapter final : studio::StudioEngine::Chain
    {
        explicit ChainAdapter (SignalChain& c) : signalChain (c) {}
        void run (const float* in, float* l, float* r, int n) noexcept override
        {
            float* outs[] = { l, r };
            signalChain.process (in, outs, 2, n);
        }
        SignalChain& signalChain;
    } chainAdapter { chain };
    studio::StudioEngine studioEngine;
    studio::Project studioProject { studioEngine };
    juce::HeapBlock<float> mixL, mixR;
    std::atomic<bool> studioNeedsRebuild { false };
    std::atomic<int> deviceLatency { 0 };

    // header
    juce::Label appTitle, deviceLabel;
    juce::TextButton audioButton { gsp::ui::utf8 ("Configurar áudio") };
    juce::ComboBox inputSelect;
    juce::TextButton tapButton { "TAP" };
    juce::ComboBox presetSelect;
    juce::TextButton savePresetButton { "Salvar" }, deletePresetButton { "Excluir" };
    TunerDisplay tuner;
    PitchDetector pitchDetector;
    std::vector<float> tunerBuffer;
    int tunerCountdown = 0;
    PresetStore presets { juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                              .getChildFile ("Guitar Studio Pro").getChildFile ("Pedaleiras") };
    real::ChickenHeadLook chickenLook;
    real::PedalKnobLook pedalKnobLook;
    juce::Label bpmLabel;
    TapTempo tapTempo;

    // modules
    ModulePanel inputPanel { "ENTRADA", false }, gatePanel { "NOISE GATE" }, capturePanel { "AMP" },
                cabPanel { "GABINETE" }, eqPanel { "EQUALIZADOR", false }, outputPanel { gsp::ui::utf8 ("SAÍDA"), false };

    Knob inputGain { "GANHO", -24.0, 24.0, 0.0, " dB" };
    Meter inputMeter;
    Knob gateThreshold { "LIMIAR", -90.0, -20.0, -60.0, " dB" };

    juce::TextButton loadCaptureButton { "Carregar captura..." }, prevCapture { "<" }, nextCapture { ">" };
    juce::Label captureName, captureInfo;
    juce::ToggleButton normalise { "Normalizar volume" };
    Knob aidaKnob1 { "PARAM 1", 0.0, 1.0, 0.5, "" }, aidaKnob2 { "PARAM 2", 0.0, 1.0, 0.5, "" };

    // built-in amp
    juce::ComboBox ampSourceSelect, voicingSelect;
    juce::Label voicingInfo;
    Knob ampGain { "GAIN", 0.0, 10.0, 5.0, "" }, ampVolume { "VOLUME", 0.0, 10.0, 5.0, "" },
         ampBass { "BASS", 0.0, 10.0, 5.0, "" }, ampMiddle { "MIDDLE", 0.0, 10.0, 5.0, "" },
         ampTreble { "TREBLE", 0.0, 10.0, 5.0, "" }, ampPresence { "PRESENCE", 0.0, 10.0, 5.0, "" },
         ampMaster { "MASTER", 0.0, 10.0, 5.0, "" };

    juce::TextButton loadIrButton { "Carregar IR..." }, clearIrButton { "Remover" };
    juce::Label irName;
    juce::ComboBox cabModeSelect, cabTypeSelect, micSelect;
    Knob micPosition { gsp::ui::utf8 ("POSIÇÃO"), 0.0, 100.0, 30.0, " %" },
         micDistance { gsp::ui::utf8 ("DISTÂNCIA"), 0.0, 100.0, 10.0, " %" };

    Knob bass { "GRAVES", -12.0, 12.0, 0.0, " dB" }, middle { gsp::ui::utf8 ("MÉDIOS"), -12.0, 12.0, 0.0, " dB" },
         treble { "AGUDOS", -12.0, 12.0, 0.0, " dB" };

    Knob outputGain { "VOLUME", -40.0, 12.0, 0.0, " dB" };
    Meter outputMeter;

    PedalboardView pedalboardView { chain.getPedalboard() };
    CaptureBrowser browser;
    juce::TextButton boardTab { "PEDALEIRA" }, studioTab { gsp::ui::utf8 ("GRAVAÇÃO") };
    StudioView studioView { studioEngine, studioProject, settings };
    void showTab (bool studio);
    juce::String audioProblem() const;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::File currentCaptureFile, currentIrFile;
    bool restoring = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace gsp::ui
