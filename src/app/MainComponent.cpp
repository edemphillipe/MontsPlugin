#include "MainComponent.h"

namespace gsp::ui
{
namespace
{
struct PanelLayout final
{
    static void row (juce::Rectangle<int> r, std::initializer_list<juce::Component*> items)
    {
        const int w = r.getWidth() / (int) items.size();
        for (auto* c : items)
            c->setBounds (r.removeFromLeft (w).reduced (2));
    }
};
} // namespace

MainComponent::MainComponent (juce::PropertiesFile& s) : settings (s)
{
    setLookAndFeel (&lookAndFeel);

    // ---------------- header
    appTitle.setText ("GUITAR STUDIO PRO", juce::dontSendNotification);
    appTitle.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    appTitle.setColour (juce::Label::textColourId, colours::accent);
    addAndMakeVisible (appTitle);

    deviceLabel.setColour (juce::Label::textColourId, colours::textDim);
    deviceLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (deviceLabel);

    audioButton.onClick = [this] { showAudioSettings(); };
    addAndMakeVisible (audioButton);

    inputSelect.onChange = [this] { inputChannel.store (juce::jmax (0, inputSelect.getSelectedId() - 1)); };
    addAndMakeVisible (inputSelect);

    tapButton.setTooltip ("Toque no ritmo (ou aperte T) para definir o BPM dos delays e modulações");
    tapButton.onClick = [this] { tap(); };
    tapButton.setColour (juce::TextButton::buttonColourId, colours::accent.darker (0.3f));
    tapButton.setColour (juce::TextButton::textColourOffId, colours::background);
    addAndMakeVisible (tapButton);
    bpmLabel.setEditable (true);
    bpmLabel.setJustificationType (juce::Justification::centred);
    bpmLabel.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    bpmLabel.setColour (juce::Label::textColourId, colours::accent);
    bpmLabel.setTooltip (utf8 ("BPM: clique duas vezes para digitar"));
    bpmLabel.onTextChange = [this] { setBpm (bpmLabel.getText().retainCharacters ("0123456789.").getDoubleValue()); };
    addAndMakeVisible (bpmLabel);
    setWantsKeyboardFocus (true);

    // ---------------- pedalboard
    addAndMakeVisible (pedalboardView);
    pedalboardView.onChanged = [this] { saveState(); };

    // ---------------- input
    addAndMakeVisible (inputPanel);
    inputPanel.addAndMakeVisible (inputGain);
    inputPanel.addAndMakeVisible (inputMeter);
    inputGain.slider.onValueChange = [this] { chain.inputGainDb = (float) inputGain.slider.getValue(); };

    // ---------------- gate
    addAndMakeVisible (gatePanel);
    gatePanel.addAndMakeVisible (gateThreshold);
    gateThreshold.slider.onValueChange = [this] { chain.gateThresholdDb = (float) gateThreshold.slider.getValue(); };
    gatePanel.onBypassChanged = [this] (bool on) { chain.gateEnabled = on; };

    // ---------------- capture
    addAndMakeVisible (capturePanel);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &loadCaptureButton, &prevCapture, &nextCapture,
                                                                        &captureName, &captureInfo, &normalise,
                                                                        &aidaKnob1, &aidaKnob2 })
        capturePanel.addAndMakeVisible (c);
    loadCaptureButton.onClick = [this] { chooseCapture(); };
    prevCapture.onClick = [this] { browser.step (-1); };
    nextCapture.onClick = [this] { browser.step (+1); };
    captureName.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    captureName.setText ("Nenhuma captura carregada", juce::dontSendNotification);
    captureInfo.setColour (juce::Label::textColourId, colours::textDim);
    captureInfo.setFont (juce::FontOptions (12.0f));
    normalise.setToggleState (true, juce::dontSendNotification);
    normalise.onClick = [this] { chain.normaliseLoudness = normalise.getToggleState(); };
    capturePanel.onBypassChanged = [this] (bool on) { chain.captureEnabled = on; };
    aidaKnob1.slider.onValueChange = [this] { chain.conditioning1 = (float) aidaKnob1.slider.getValue(); };
    aidaKnob2.slider.onValueChange = [this] { chain.conditioning2 = (float) aidaKnob2.slider.getValue(); };
    aidaKnob1.setVisible (false);
    aidaKnob2.setVisible (false);

    // ---------------- cabinet
    addAndMakeVisible (cabPanel);
    cabPanel.addAndMakeVisible (loadIrButton);
    cabPanel.addAndMakeVisible (clearIrButton);
    cabPanel.addAndMakeVisible (irName);
    irName.setText ("Sem IR", juce::dontSendNotification);
    irName.setColour (juce::Label::textColourId, colours::textDim);
    loadIrButton.onClick = [this] { chooseImpulseResponse(); };
    clearIrButton.onClick = [this]
    {
        chain.clearImpulseResponse();
        currentIrFile = juce::File();
        irName.setText ("Sem IR", juce::dontSendNotification);
    };
    cabPanel.onBypassChanged = [this] (bool on) { chain.irEnabled = on; };

    // ---------------- eq
    addAndMakeVisible (eqPanel);
    for (auto* k : { &bass, &middle, &treble })
        eqPanel.addAndMakeVisible (*k);
    bass.slider.onValueChange = [this] { chain.bassDb = (float) bass.slider.getValue(); };
    middle.slider.onValueChange = [this] { chain.middleDb = (float) middle.slider.getValue(); };
    treble.slider.onValueChange = [this] { chain.trebleDb = (float) treble.slider.getValue(); };

    // ---------------- output
    addAndMakeVisible (outputPanel);
    outputPanel.addAndMakeVisible (outputGain);
    outputPanel.addAndMakeVisible (outputMeter);
    outputGain.slider.onValueChange = [this] { chain.outputGainDb = (float) outputGain.slider.getValue(); };

    // ---------------- library
    addAndMakeVisible (browser);
    browser.onLoad = [this] (const juce::File& f) { loadCapture (f); };
    browser.onChooseFolder = [this] { chooseLibraryFolder(); };
    browser.onFavouritesChanged = [this] { saveState(); };

    // ---------------- audio device
    std::unique_ptr<juce::XmlElement> savedDevice (settings.getXmlValue ("audioDevice"));
    const auto err = deviceManager.initialise (2, 2, savedDevice.get(), true);
    if (err.isNotEmpty())
        showError ("Audio", err);
    deviceManager.addAudioCallback (this);
    deviceManager.addChangeListener (this);

    restoreState();
    refreshInputChoices();
    updateDeviceLabel();

    setSize (1280, 940);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    saveState();
    deviceManager.removeChangeListener (this);
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
    setLookAndFeel (nullptr);
}

// ======================================================================= audio
void MainComponent::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const int block = device->getCurrentBufferSizeSamples();
    chain.prepare (device->getCurrentSampleRate(), block);
    silence.allocate ((size_t) juce::jmax (block, 4096), true);
    silenceSize = juce::jmax (block, 4096);
}

void MainComponent::audioDeviceStopped() { chain.releaseResources(); }

void MainComponent::audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                                      float* const* outputs, int numOutputs, int numSamples,
                                                      const juce::AudioIODeviceCallbackContext&)
{
    const int ch = inputChannel.load (std::memory_order_relaxed);
    const float* in = (ch < numInputs && inputs[ch] != nullptr) ? inputs[ch]
                                                                : (numInputs > 0 && inputs[0] != nullptr ? inputs[0] : nullptr);
    if (in == nullptr || numSamples > silenceSize)
    {
        for (int c = 0; c < numOutputs; ++c)
            if (outputs[c] != nullptr)
                juce::FloatVectorOperations::clear (outputs[c], numSamples);
        if (in == nullptr)
            return;
    }

    chain.process (in, outputs, numOutputs, numSamples);
}

// ======================================================================= ui
void MainComponent::timerCallback()
{
    // TAP button blinks on the beat
    const double beat = 60.0 / chain.getPedalboard().tempo().bpm.load();
    const bool lit = std::fmod (juce::Time::getMillisecondCounterHiRes() * 0.001, beat) < 0.08;
    tapButton.setColour (juce::TextButton::buttonColourId, lit ? colours::accent : colours::accent.darker (0.5f));

    const auto levels = chain.readAndResetLevels();
    inputMeter.setPeak (levels.inputPeak);
    outputMeter.setPeak (levels.outputPeak);
    chain.collectGarbage();
}

void MainComponent::tap()
{
    const double bpm = tapTempo.tap (juce::Time::getMillisecondCounterHiRes() * 0.001);
    if (bpm > 0.0)
        setBpm (bpm);
}

void MainComponent::setBpm (double bpm)
{
    if (bpm < 30.0 || bpm > 300.0)
    {
        bpmLabel.setText (juce::String (chain.getPedalboard().tempo().bpm.load(), 1) + " BPM", juce::dontSendNotification);
        return;
    }
    chain.getPedalboard().tempo().bpm = bpm;
    bpmLabel.setText (juce::String (bpm, 1) + " BPM", juce::dontSendNotification);
    saveState();
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (key.getTextCharacter() == 't' || key.getTextCharacter() == 'T')
    {
        tap();
        return true;
    }
    return false;
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // audio device, sample rate, buffer or channels changed
    refreshInputChoices();
    updateDeviceLabel();
    saveState();
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    // signal-flow arrows between modules
    g.setColour (colours::panelEdge);
    const juce::Component* modules[] = { &inputPanel, &gatePanel, &capturePanel, &cabPanel, &eqPanel, &outputPanel };
    for (int i = 0; i + 1 < 6; ++i)
    {
        const auto a = modules[i]->getBounds(), b = modules[i + 1]->getBounds();
        if (a.getY() != b.getY())
            continue;
        const float y = (float) a.getCentreY();
        juce::Path arrow;
        arrow.addArrow ({ (float) a.getRight() + 1.0f, y, (float) b.getX() - 1.0f, y }, 2.0f, 8.0f, 6.0f);
        g.fillPath (arrow);
    }
}

void MainComponent::resized()
{
    auto r = getLocalBounds().reduced (14);

    auto header = r.removeFromTop (36);
    appTitle.setBounds (header.removeFromLeft (280));
    audioButton.setBounds (header.removeFromRight (150).reduced (0, 4));
    header.removeFromRight (8);
    bpmLabel.setBounds (header.removeFromRight (90).reduced (0, 4));
    tapButton.setBounds (header.removeFromRight (60).reduced (0, 4));
    header.removeFromRight (8);
    inputSelect.setBounds (header.removeFromRight (150).reduced (0, 4));
    header.removeFromRight (8);
    deviceLabel.setBounds (header);

    r.removeFromTop (12);
    auto chainRow = r.removeFromTop (220);
    const int gap = 14;
    auto take = [&] (int w) { auto b = chainRow.removeFromLeft (w); chainRow.removeFromLeft (gap); return b; };

    inputPanel.setBounds (take (120));
    gatePanel.setBounds (take (130));
    capturePanel.setBounds (take (350));
    cabPanel.setBounds (take (190));
    outputPanel.setBounds (chainRow.removeFromRight (120));
    chainRow.removeFromRight (gap);
    eqPanel.setBounds (chainRow);

    // panel contents
    {
        auto b = inputPanel.getLocalBounds().reduced (10).withTrimmedTop (24);
        inputMeter.setBounds (b.removeFromRight (14));
        inputGain.setBounds (b.withSizeKeepingCentre (b.getWidth(), 110));
    }
    {
        auto b = gatePanel.getLocalBounds().reduced (10).withTrimmedTop (24);
        gateThreshold.setBounds (b.withSizeKeepingCentre (b.getWidth(), 110));
    }
    {
        auto b = capturePanel.getLocalBounds().reduced (12).withTrimmedTop (24);
        auto top = b.removeFromTop (28);
        nextCapture.setBounds (top.removeFromRight (30));
        top.removeFromRight (4);
        prevCapture.setBounds (top.removeFromRight (30));
        top.removeFromRight (8);
        loadCaptureButton.setBounds (top);
        b.removeFromTop (8);
        captureName.setBounds (b.removeFromTop (24));
        captureInfo.setBounds (b.removeFromTop (34));
        normalise.setBounds (b.removeFromTop (24));
        auto knobs = b.removeFromTop (100);
        aidaKnob1.setBounds (knobs.removeFromLeft (90));
        aidaKnob2.setBounds (knobs.removeFromLeft (90));
    }
    {
        auto b = cabPanel.getLocalBounds().reduced (12).withTrimmedTop (24);
        loadIrButton.setBounds (b.removeFromTop (28));
        b.removeFromTop (8);
        irName.setBounds (b.removeFromTop (40));
        clearIrButton.setBounds (b.removeFromTop (26).removeFromLeft (90));
    }
    {
        auto b = eqPanel.getLocalBounds().reduced (10).withTrimmedTop (24);
        PanelLayout::row (b.withSizeKeepingCentre (b.getWidth(), 110), { &bass, &middle, &treble });
    }
    {
        auto b = outputPanel.getLocalBounds().reduced (10).withTrimmedTop (24);
        outputMeter.setBounds (b.removeFromRight (14));
        outputGain.setBounds (b.withSizeKeepingCentre (b.getWidth(), 110));
    }

    r.removeFromTop (12);
    pedalboardView.setBounds (r.removeFromTop (juce::jmax (330, r.getHeight() - 220)));
    r.removeFromTop (12);
    browser.setBounds (r);
}

void MainComponent::showAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (deviceManager, 1, 2, 2, 2, false, false, false, false);
    selector->setSize (520, 420);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector);
    options.dialogTitle = utf8 ("Configurar áudio");
    options.dialogBackgroundColour = colours::panel;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

void MainComponent::refreshInputChoices()
{
    inputSelect.clear (juce::dontSendNotification);
    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        const auto names = device->getInputChannelNames();
        const auto active = device->getActiveInputChannels();
        int index = 0;
        for (int i = 0; i < names.size(); ++i)
            if (active[i])
                inputSelect.addItem ("Entrada: " + names[i], ++index);
    }
    if (inputSelect.getNumItems() == 0)
        inputSelect.addItem ("Sem entrada", 1);

    const int saved = settings.getIntValue ("inputChannel", 0);
    inputSelect.setSelectedId (juce::jlimit (1, inputSelect.getNumItems(), saved + 1), juce::dontSendNotification);
    inputChannel.store (inputSelect.getSelectedId() - 1);
}

void MainComponent::updateDeviceLabel()
{
    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        const double sr = device->getCurrentSampleRate();
        const int buffer = device->getCurrentBufferSizeSamples();
        const int latency = buffer * 2 + device->getInputLatencyInSamples() + device->getOutputLatencyInSamples();
        deviceLabel.setText (device->getTypeName() + "  |  " + device->getName() + "  |  "
                                 + juce::String (sr / 1000.0, 1) + " kHz  |  buffer " + juce::String (buffer)
                                 + "  |  ~" + juce::String (1000.0 * latency / sr, 1) + " ms",
                             juce::dontSendNotification);
    }
    else
    {
        deviceLabel.setText (utf8 ("Nenhum dispositivo de áudio aberto — clique em Configurar áudio"), juce::dontSendNotification);
    }
}

// ======================================================================= files
void MainComponent::chooseCapture()
{
    const auto start = currentCaptureFile.existsAsFile() ? currentCaptureFile.getParentDirectory() : browser.getLibraryFolder();
    chooser = std::make_unique<juce::FileChooser> ("Escolha uma captura NAM ou AIDA-X", start, "*.nam;*.json;*.aidax");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (fc.getResult().existsAsFile())
                                  loadCapture (fc.getResult());
                          });
}

void MainComponent::chooseImpulseResponse()
{
    const auto start = currentIrFile.existsAsFile() ? currentIrFile.getParentDirectory() : juce::File();
    chooser = std::make_unique<juce::FileChooser> ("Escolha um IR de gabinete", start, "*.wav;*.aif;*.aiff");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (fc.getResult().existsAsFile())
                                  loadImpulseResponse (fc.getResult());
                          });
}

void MainComponent::chooseLibraryFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Pasta da biblioteca de capturas", browser.getLibraryFolder());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (fc.getResult().isDirectory())
                              {
                                  browser.setLibraryFolder (fc.getResult());
                                  saveState();
                              }
                          });
}

void MainComponent::loadCapture (const juce::File& file)
{
    juce::String error;
    if (! chain.loadCapture (file, error))
    {
        showError ("Captura", error);
        return;
    }

    currentCaptureFile = file;
    captureName.setText (chain.getCaptureName(), juce::dontSendNotification);
    captureInfo.setText (chain.getCaptureDescription(), juce::dontSendNotification);
    pedalboardView.setCaptureName (chain.getCaptureName());
    aidaKnob1.setVisible (chain.getCaptureConditioningInputs() >= 1);
    aidaKnob2.setVisible (chain.getCaptureConditioningInputs() >= 2);
    browser.highlight (file);
    saveState();
}

void MainComponent::loadImpulseResponse (const juce::File& file)
{
    juce::String error;
    if (! chain.loadImpulseResponse (file, error))
    {
        showError ("IR", error);
        return;
    }
    currentIrFile = file;
    irName.setText (chain.getImpulseResponseName(), juce::dontSendNotification);
    saveState();
}

void MainComponent::showError (const juce::String& title, const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, title, message);
}

// ======================================================================= state
void MainComponent::saveState()
{
    if (auto xml = deviceManager.createStateXml())
        settings.setValue ("audioDevice", xml.get());
    settings.setValue ("inputChannel", inputChannel.load());
    settings.setValue ("capture", currentCaptureFile.getFullPathName());
    settings.setValue ("ir", currentIrFile.getFullPathName());
    settings.setValue ("library", browser.getLibraryFolder().getFullPathName());
    settings.setValue ("favourites", browser.getFavourites().joinIntoString ("\n"));
    settings.setValue ("inputGain", inputGain.slider.getValue());
    settings.setValue ("gateOn", gatePanel.bypass.getToggleState());
    settings.setValue ("gateThreshold", gateThreshold.slider.getValue());
    settings.setValue ("captureOn", capturePanel.bypass.getToggleState());
    settings.setValue ("normalise", normalise.getToggleState());
    settings.setValue ("irOn", cabPanel.bypass.getToggleState());
    settings.setValue ("bass", bass.slider.getValue());
    settings.setValue ("middle", middle.slider.getValue());
    settings.setValue ("treble", treble.slider.getValue());
    settings.setValue ("outputGain", outputGain.slider.getValue());
    settings.setValue ("pedalboard", juce::JSON::toString (chain.getPedalboard().toVar(), true));
    settings.saveIfNeeded();
}

void MainComponent::restoreState()
{
    auto restoreKnob = [this] (Knob& k, const char* key)
    {
        if (settings.containsKey (key))
            k.slider.setValue (settings.getDoubleValue (key), juce::sendNotificationSync);
        else
            k.slider.setValue (k.slider.getValue(), juce::sendNotificationSync);
    };
    auto restoreToggle = [this] (ModulePanel& p, const char* key)
    {
        p.bypass.setToggleState (settings.getBoolValue (key, true), juce::dontSendNotification);
        if (p.onBypassChanged)
            p.onBypassChanged (p.bypass.getToggleState());
    };

    restoreKnob (inputGain, "inputGain");
    restoreKnob (gateThreshold, "gateThreshold");
    restoreKnob (bass, "bass");
    restoreKnob (middle, "middle");
    restoreKnob (treble, "treble");
    restoreKnob (outputGain, "outputGain");
    restoreToggle (gatePanel, "gateOn");
    restoreToggle (capturePanel, "captureOn");
    restoreToggle (cabPanel, "irOn");
    normalise.setToggleState (settings.getBoolValue ("normalise", true), juce::dontSendNotification);
    chain.normaliseLoudness = normalise.getToggleState();

    if (settings.containsKey ("pedalboard"))
        chain.getPedalboard().fromVar (juce::JSON::parse (settings.getValue ("pedalboard")));
    pedalboardView.rebuild();
    bpmLabel.setText (juce::String (chain.getPedalboard().tempo().bpm.load(), 1) + " BPM", juce::dontSendNotification);

    browser.setFavourites (juce::StringArray::fromLines (settings.getValue ("favourites")));
    const juce::File library (settings.getValue ("library"));
    if (library.isDirectory())
        browser.setLibraryFolder (library);

    const juce::File capture (settings.getValue ("capture"));
    if (capture.existsAsFile())
        loadCapture (capture);
    const juce::File ir (settings.getValue ("ir"));
    if (ir.existsAsFile())
        loadImpulseResponse (ir);
}

} // namespace gsp::ui
