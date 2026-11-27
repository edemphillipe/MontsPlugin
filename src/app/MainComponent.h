#pragma once

#include "CaptureBrowser.h"
#include "PedalboardView.h"
#include "LookAndFeel.h"
#include "Widgets.h"

#include "engine/SignalChain.h"

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

    juce::PropertiesFile& settings;
    StudioLookAndFeel lookAndFeel;

    juce::AudioDeviceManager deviceManager;
    SignalChain chain;
    std::atomic<int> inputChannel { 0 };
    juce::HeapBlock<float> silence;
    int silenceSize = 0;

    // header
    juce::Label appTitle, deviceLabel;
    juce::TextButton audioButton { gsp::ui::utf8 ("Configurar áudio") };
    juce::ComboBox inputSelect;
    juce::TextButton tapButton { "TAP" };
    juce::Label bpmLabel;
    TapTempo tapTempo;

    // modules
    ModulePanel inputPanel { "ENTRADA", false }, gatePanel { "NOISE GATE" }, capturePanel { "CAPTURA  NAM / AIDA-X" },
                cabPanel { "GABINETE  IR" }, eqPanel { "EQUALIZADOR", false }, outputPanel { gsp::ui::utf8 ("SAÍDA"), false };

    Knob inputGain { "GANHO", -24.0, 24.0, 0.0, " dB" };
    Meter inputMeter;
    Knob gateThreshold { "LIMIAR", -90.0, -20.0, -60.0, " dB" };

    juce::TextButton loadCaptureButton { "Carregar captura..." }, prevCapture { "<" }, nextCapture { ">" };
    juce::Label captureName, captureInfo;
    juce::ToggleButton normalise { "Normalizar volume" };
    Knob aidaKnob1 { "PARAM 1", 0.0, 1.0, 0.5, "" }, aidaKnob2 { "PARAM 2", 0.0, 1.0, 0.5, "" };

    juce::TextButton loadIrButton { "Carregar IR..." }, clearIrButton { "Remover" };
    juce::Label irName;

    Knob bass { "GRAVES", -12.0, 12.0, 0.0, " dB" }, middle { gsp::ui::utf8 ("MÉDIOS"), -12.0, 12.0, 0.0, " dB" },
         treble { "AGUDOS", -12.0, 12.0, 0.0, " dB" };

    Knob outputGain { "VOLUME", -40.0, 12.0, 0.0, " dB" };
    Meter outputMeter;

    PedalboardView pedalboardView { chain.getPedalboard() };
    CaptureBrowser browser;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::File currentCaptureFile, currentIrFile;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace gsp::ui
