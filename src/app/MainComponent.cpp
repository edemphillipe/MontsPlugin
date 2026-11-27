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

    // ---------------- built-in amp (same panel, chosen with the source selector)
    ampSourceSelect.addItem ("Captura NAM / AIDA-X", 1);
    ampSourceSelect.addItem ("Amplificador interno", 2);
    ampSourceSelect.setSelectedId (1, juce::dontSendNotification);
    ampSourceSelect.onChange = [this]
    {
        chain.ampSource = ampSourceSelect.getSelectedId() == 2 ? SignalChain::sourceAmpModel : SignalChain::sourceCapture;
        updateAmpControls();
        saveState();
    };
    capturePanel.addAndMakeVisible (ampSourceSelect);
    for (int i = 0; i < amp::numVoicings(); ++i)
        voicingSelect.addItem (utf8 (amp::voicing (i).name), i + 1);
    voicingSelect.setSelectedId (chain.getAmp().voicingIndex.load() + 1, juce::dontSendNotification);
    voicingSelect.onChange = [this]
    {
        chain.getAmp().voicingIndex = voicingSelect.getSelectedId() - 1;
        updateAmpControls();
        saveState();
    };
    capturePanel.addChildComponent (voicingSelect);
    voicingInfo.setColour (juce::Label::textColourId, colours::textDim);
    voicingInfo.setFont (juce::FontOptions (13.0f));
    capturePanel.addChildComponent (voicingInfo);
    auto bindAmpKnob = [this] (Knob& k, std::atomic<float>& target)
    {
        capturePanel.addChildComponent (k);
        k.slider.setNumDecimalPlacesToDisplay (1);
        k.slider.onValueChange = [&k, &target] { target = (float) k.slider.getValue(); };
        k.slider.onDragEnd = [this] { saveState(); };
    };
    auto& a = chain.getAmp();
    bindAmpKnob (ampGain, a.gain);
    bindAmpKnob (ampVolume, a.volume);
    bindAmpKnob (ampBass, a.bass);
    bindAmpKnob (ampMiddle, a.middle);
    bindAmpKnob (ampTreble, a.treble);
    bindAmpKnob (ampPresence, a.presence);
    bindAmpKnob (ampMaster, a.master);

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
        saveState();
    };
    cabPanel.onBypassChanged = [this] (bool on) { chain.irEnabled = on; };

    cabModeSelect.addItem ("Simulado (alto-falante + microfone)", 1);
    cabModeSelect.addItem ("IR (arquivo .wav)", 2);
    cabModeSelect.setSelectedId (1, juce::dontSendNotification);
    cabModeSelect.onChange = [this]
    {
        chain.cabinetMode = cabModeSelect.getSelectedId() == 2 ? SignalChain::cabinetImpulseResponse : SignalChain::cabinetSimulated;
        updateAmpControls();
        saveState();
    };
    cabPanel.addAndMakeVisible (cabModeSelect);
    for (int i = 0; i < amp::numCabinets(); ++i)
        cabTypeSelect.addItem (utf8 (amp::cabinet (i).name), i + 1);
    for (int i = 0; i < amp::numMics(); ++i)
        micSelect.addItem ("Microfone " + utf8 (amp::mic (i).name), i + 1);
    cabTypeSelect.setSelectedId (chain.getCabinet().cabinetIndex.load() + 1, juce::dontSendNotification);
    micSelect.setSelectedId (chain.getCabinet().micIndex.load() + 1, juce::dontSendNotification);
    cabTypeSelect.onChange = [this] { chain.getCabinet().cabinetIndex = cabTypeSelect.getSelectedId() - 1; saveState(); };
    micSelect.onChange = [this] { chain.getCabinet().micIndex = micSelect.getSelectedId() - 1; saveState(); };
    micPosition.slider.onValueChange = [this] { chain.getCabinet().position = (float) micPosition.slider.getValue() / 100.0f; };
    micDistance.slider.onValueChange = [this] { chain.getCabinet().distance = (float) micDistance.slider.getValue() / 100.0f; };
    micPosition.slider.onDragEnd = [this] { saveState(); };
    micDistance.slider.onDragEnd = [this] { saveState(); };
    micPosition.slider.setTooltip (utf8 ("0% = centro do cone (mais brilho), 100% = borda (mais escuro)"));
    micDistance.slider.setTooltip (utf8 ("0% = colado na tela, 100% = afastado (menos graves, reflexão do chão)"));
    for (juce::Component* c : std::initializer_list<juce::Component*> { &cabTypeSelect, &micSelect, &micPosition, &micDistance })
        cabPanel.addChildComponent (c);

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

    setSize (1400, 960);
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

    inputPanel.setBounds (take (110));
    gatePanel.setBounds (take (124));
    outputPanel.setBounds (chainRow.removeFromRight (110));
    chainRow.removeFromRight (gap);
    eqPanel.setBounds (chainRow.removeFromRight (200));
    chainRow.removeFromRight (gap);
    cabPanel.setBounds (chainRow.removeFromRight (250));
    chainRow.removeFromRight (gap);
    capturePanel.setBounds (chainRow);

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
        auto sourceRow = b.removeFromTop (26);
        ampSourceSelect.setBounds (sourceRow.removeFromLeft (210));
        sourceRow.removeFromLeft (8);
        voicingSelect.setBounds (sourceRow.removeFromLeft (220));
        {
            auto knobs = b;
            voicingInfo.setBounds (knobs.removeFromTop (20));
            knobs = knobs.withSizeKeepingCentre (knobs.getWidth(), juce::jmin (knobs.getHeight(), 112));
            const int w = knobs.getWidth() / 7;
            for (auto* k : { &ampGain, &ampVolume, &ampBass, &ampMiddle, &ampTreble, &ampPresence, &ampMaster })
                k->setBounds (knobs.removeFromLeft (w).reduced (1, 0));
        }
        b.removeFromTop (8);
        auto top = b.removeFromTop (28);
        nextCapture.setBounds (top.removeFromRight (30));
        top.removeFromRight (4);
        prevCapture.setBounds (top.removeFromRight (30));
        top.removeFromRight (8);
        loadCaptureButton.setBounds (top);
        b.removeFromTop (8);
        auto left = b.removeFromLeft (juce::jmin (b.getWidth(), 360));
        captureName.setBounds (left.removeFromTop (24));
        captureInfo.setBounds (left.removeFromTop (22));
        normalise.setBounds (left.removeFromTop (24));
        b.removeFromLeft (8);
        auto knobs = b.removeFromTop (100);
        aidaKnob1.setBounds (knobs.removeFromLeft (90));
        aidaKnob2.setBounds (knobs.removeFromLeft (90));
    }
    {
        auto b = cabPanel.getLocalBounds().reduced (12).withTrimmedTop (24);
        cabModeSelect.setBounds (b.removeFromTop (26));
        b.removeFromTop (6);
        auto ir = b;
        loadIrButton.setBounds (ir.removeFromTop (28));
        ir.removeFromTop (6);
        irName.setBounds (ir.removeFromTop (40));
        clearIrButton.setBounds (ir.removeFromTop (26).removeFromLeft (90));
        auto sim = b;
        cabTypeSelect.setBounds (sim.removeFromTop (24));
        sim.removeFromTop (4);
        micSelect.setBounds (sim.removeFromTop (24));
        sim.removeFromTop (2);
        micPosition.setBounds (sim.removeFromLeft (sim.getWidth() / 2));
        micDistance.setBounds (sim);
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
    browser.highlight (file);
    if (! restoring)
        ampSourceSelect.setSelectedId (1, juce::sendNotificationSync); // loading a capture means: play the capture
    updateAmpControls();
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
    if (! restoring)
        cabModeSelect.setSelectedId (2, juce::sendNotificationSync);
    saveState();
}

void MainComponent::showError (const juce::String& title, const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, title, message);
}

// ======================================================================= state
void MainComponent::saveState()
{
    if (restoring)
        return; // restoreState() is still applying the saved values
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

    auto& a = chain.getAmp();
    auto* ampState = new juce::DynamicObject();
    ampState->setProperty ("source", ampSourceSelect.getSelectedId() == 2 ? "amp" : "capture");
    ampState->setProperty ("model", amp::voicing (a.voicingIndex.load()).id);
    ampState->setProperty ("gain", ampGain.slider.getValue());
    ampState->setProperty ("volume", ampVolume.slider.getValue());
    ampState->setProperty ("bass", ampBass.slider.getValue());
    ampState->setProperty ("middle", ampMiddle.slider.getValue());
    ampState->setProperty ("treble", ampTreble.slider.getValue());
    ampState->setProperty ("presence", ampPresence.slider.getValue());
    ampState->setProperty ("master", ampMaster.slider.getValue());
    ampState->setProperty ("cabinetMode", cabModeSelect.getSelectedId() == 2 ? "ir" : "simulated");
    ampState->setProperty ("cabinet", amp::cabinet (chain.getCabinet().cabinetIndex.load()).id);
    ampState->setProperty ("mic", amp::mic (chain.getCabinet().micIndex.load()).id);
    ampState->setProperty ("micPosition", micPosition.slider.getValue());
    ampState->setProperty ("micDistance", micDistance.slider.getValue());
    settings.setValue ("amp", juce::JSON::toString (juce::var (ampState), true));
    settings.saveIfNeeded();
}

void MainComponent::restoreState()
{
    const juce::ScopedValueSetter<bool> restoringScope (restoring, true);
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

    const auto ampState = juce::JSON::parse (settings.getValue ("amp"));
    if (ampState.isObject())
    {
        auto num = [&] (const char* key, Knob& k)
        {
            if (ampState.hasProperty (key))
                k.slider.setValue ((double) ampState[key], juce::sendNotificationSync);
        };
        num ("gain", ampGain); num ("volume", ampVolume); num ("bass", ampBass); num ("middle", ampMiddle);
        num ("treble", ampTreble); num ("presence", ampPresence); num ("master", ampMaster);
        num ("micPosition", micPosition); num ("micDistance", micDistance);
        const int v = amp::voicingIndexForId (ampState["model"].toString());
        if (v >= 0) voicingSelect.setSelectedId (v + 1, juce::sendNotificationSync);
        const int c = amp::cabinetIndexForId (ampState["cabinet"].toString().toRawUTF8());
        if (c >= 0) cabTypeSelect.setSelectedId (c + 1, juce::sendNotificationSync);
        const int m = amp::micIndexForId (ampState["mic"].toString().toRawUTF8());
        if (m >= 0) micSelect.setSelectedId (m + 1, juce::sendNotificationSync);
        ampSourceSelect.setSelectedId (ampState["source"].toString() == "amp" ? 2 : 1, juce::sendNotificationSync);
        cabModeSelect.setSelectedId (ampState["cabinetMode"].toString() == "ir" ? 2 : 1, juce::sendNotificationSync);
    }
    else
    {
        // first start, or settings from Phase 1: an IR that was in use stays in use
        cabModeSelect.setSelectedId (currentIrFile.existsAsFile() ? 2 : 1, juce::sendNotificationSync);
        for (auto* k : { &ampGain, &ampVolume, &ampBass, &ampMiddle, &ampTreble, &ampPresence, &ampMaster, &micPosition, &micDistance })
            k->slider.setValue (k->slider.getValue(), juce::sendNotificationSync);
    }
    updateAmpControls();
}

void MainComponent::updateAmpControls()
{
    const bool model = ampSourceSelect.getSelectedId() == 2;
    for (juce::Component* c : std::initializer_list<juce::Component*> { &voicingSelect, &voicingInfo, &ampGain, &ampVolume, &ampBass,
                                                                        &ampMiddle, &ampTreble, &ampPresence, &ampMaster })
        c->setVisible (model);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &loadCaptureButton, &prevCapture, &nextCapture, &captureName,
                                                                        &captureInfo, &normalise })
        c->setVisible (! model);
    aidaKnob1.setVisible (! model && chain.getCaptureConditioningInputs() >= 1);
    aidaKnob2.setVisible (! model && chain.getCaptureConditioningInputs() >= 2);
    voicingInfo.setText (utf8 (amp::voicing (voicingSelect.getSelectedId() - 1).description), juce::dontSendNotification);

    const bool irMode = cabModeSelect.getSelectedId() == 2;
    for (juce::Component* c : std::initializer_list<juce::Component*> { &loadIrButton, &irName, &clearIrButton })
        c->setVisible (irMode);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &cabTypeSelect, &micSelect, &micPosition, &micDistance })
        c->setVisible (! irMode);
    if (irMode && ! chain.hasImpulseResponse())
        irName.setText (utf8 ("Sem IR: carregue um arquivo (até lá o gabinete simulado é usado)"), juce::dontSendNotification);
    updateAmpTile();
}

void MainComponent::updateAmpTile()
{
    const bool model = ampSourceSelect.getSelectedId() == 2;
    juce::String text = model ? utf8 (amp::voicing (voicingSelect.getSelectedId() - 1).name) : chain.getCaptureName();
    if (cabModeSelect.getSelectedId() == 2 && chain.hasImpulseResponse())
        text << "\n" << chain.getImpulseResponseName();
    else
        text << "\n" << utf8 (amp::cabinet (cabTypeSelect.getSelectedId() - 1).name);
    pedalboardView.setCaptureName (text);
}

} // namespace gsp::ui
