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

    tapButton.setTooltip (juce::String::fromUTF8 ("Toque no ritmo (ou aperte T) para definir o BPM dos delays e modulações"));
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

    // presets (whole board)
    presetSelect.setTextWhenNothingSelected ("Pedaleira: (sem nome)");
    presetSelect.setTooltip (utf8 ("Pedaleiras salvas: amp, gabinete, pedais e ordem"));
    presetSelect.onChange = [this]
    {
        const auto name = presetSelect.getText();
        const auto state = presets.load (name);
        if (state.isObject())
        {
            applyBoardState (state);
            settings.setValue ("preset", name);
            saveState();
        }
    };
    addAndMakeVisible (presetSelect);
    savePresetButton.setTooltip (utf8 ("Salvar a pedaleira atual com um nome"));
    savePresetButton.onClick = [this] { savePresetAs(); };
    addAndMakeVisible (savePresetButton);
    deletePresetButton.setTooltip ("Excluir a pedaleira selecionada");
    deletePresetButton.onClick = [this] { deleteCurrentPreset(); };
    addAndMakeVisible (deletePresetButton);

    // tuner
    tuner.onActiveChanged = [this] (bool on) { chain.muteOutput = on; };
    addAndMakeVisible (tuner);
    tunerBuffer.assign (4096, 0.0f);

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
    capturePanel.addAndMakeVisible (voicingInfo);
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

    // ---------------- realistic looks
    capturePanel.setLook (ModulePanel::Look::AmpHead);
    cabPanel.setLook (ModulePanel::Look::Cabinet);
    const auto ampInk = juce::Colour (0xff2b2010);
    for (auto* k : { &ampGain, &ampVolume, &ampBass, &ampMiddle, &ampTreble, &ampPresence, &ampMaster, &aidaKnob1, &aidaKnob2 })
    {
        k->setPanelStyle (&chickenLook, ampInk);
        k->label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    }
    for (auto* k : { &inputGain, &gateThreshold, &bass, &middle, &treble, &outputGain })
        k->setPanelStyle (&pedalKnobLook, colours::text, "silver");
    for (auto* k : { &micPosition, &micDistance })
        k->setPanelStyle (&pedalKnobLook, juce::Colour (0xffefe3c0), "black");
    for (auto* l : { &captureName, &captureInfo })
        l->setColour (juce::Label::textColourId, ampInk);
    normalise.setColour (juce::ToggleButton::textColourId, ampInk);
    normalise.setColour (juce::ToggleButton::tickDisabledColourId, ampInk);
    voicingInfo.setColour (juce::Label::textColourId, juce::Colour (0xffd8c9a8));
    irName.setColour (juce::Label::textColourId, juce::Colour (0xffefe3c0));
    for (auto* b : { &ampSourceSelect, &voicingSelect, &cabModeSelect, &cabTypeSelect, &micSelect })
    {
        b->setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xcc15110e));
        b->setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff5a4a3a));
        b->setColour (juce::ComboBox::textColourId, juce::Colour (0xffefe3c0));
    }
    appTitle.setFont (juce::FontOptions (21.0f, juce::Font::bold));
    deviceLabel.setFont (juce::FontOptions (10.5f));
    deviceLabel.setJustificationType (juce::Justification::centredLeft);

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

    // ---------------- recording studio
    for (auto* b : { &boardTab, &studioTab })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (4711);
        b->setColour (juce::TextButton::buttonColourId, colours::panel);
        addAndMakeVisible (b);
    }
    boardTab.onClick = [this] { if (boardTab.getToggleState()) showTab (false); };
    studioTab.onClick = [this] { if (studioTab.getToggleState()) showTab (true); };
    studioTab.setTooltip (utf8 ("Gravação multipista, mixer, reamp e música de referência"));
    addChildComponent (studioView);
    studioView.checkAudio = [this] { return audioProblem(); };
    studioProject.toneProvider = [this] { return boardStateVar(); };
    studioView.onLoadTone = [this] (const juce::var& tone)
    {
        applyBoardState (tone);
        presetSelect.setSelectedId (0, juce::dontSendNotification);
        saveState();
    };
    studioView.openInitialProject();
    showTab (settings.getBoolValue ("studioTab", false));

    setSize (1400, 990);
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
    mixL.allocate ((size_t) silenceSize, true);
    mixR.allocate ((size_t) silenceSize, true);
    studioEngine.prepare (device->getCurrentSampleRate(), block);
    deviceLatency = device->getInputLatencyInSamples() + device->getOutputLatencyInSamples();
    studioNeedsRebuild = true; // the takes must be re-read at the new sample rate
}

void MainComponent::audioDeviceStopped() { chain.releaseResources(); }

void MainComponent::audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                                      float* const* outputs, int numOutputs, int numSamples,
                                                      const juce::AudioIODeviceCallbackContext&)
{
    const int ch = inputChannel.load (std::memory_order_relaxed);
    const float* in = (ch < numInputs && inputs[ch] != nullptr) ? inputs[ch]
                                                                : (numInputs > 0 && inputs[0] != nullptr ? inputs[0] : nullptr);
    if (numSamples > silenceSize)
    {
        for (int c = 0; c < numOutputs; ++c)
            if (outputs[c] != nullptr)
                juce::FloatVectorOperations::clear (outputs[c], numSamples);
        return;
    }
    if (in == nullptr)
        in = silence.get(); // no input: the tracks still play

    // guitar chain + recording + playback of the tracks
    float* l = mixL.get();
    float* r = mixR.get();
    studioEngine.process (in, l, r, numSamples, chainAdapter);
    for (int c = 0; c < numOutputs; ++c)
        if (outputs[c] != nullptr)
            juce::FloatVectorOperations::copy (outputs[c], (c % 2 == 0 || numOutputs == 1) ? l : r, numSamples);
}

// ======================================================================= ui
void MainComponent::timerCallback()
{
    // TAP button blinks on the beat
    const double beat = 60.0 / chain.getPedalboard().tempo().bpm.load();
    const bool lit = std::fmod (juce::Time::getMillisecondCounterHiRes() * 0.001, beat) < 0.08;
    tapButton.setColour (juce::TextButton::buttonColourId, lit ? colours::accent : colours::accent.darker (0.5f));

    if (tuner.isActive() && --tunerCountdown <= 0)
    {
        tunerCountdown = 3; // ~10 analyses per second
        chain.getTunerTap().latest (tunerBuffer.data(), (int) tunerBuffer.size());
        tuner.setResult (pitchDetector.detect (tunerBuffer.data(), (int) tunerBuffer.size(), chain.getSampleRate()));
    }

    studioEngine.bpm = chain.getPedalboard().tempo().bpm.load();
    if (studioNeedsRebuild.exchange (false))
    {
        studioProject.setLatencySamples (deviceLatency.load());
        juce::String ignored;
        studioProject.rebuild (ignored);
        studioView.refresh();
    }

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
    if (key == juce::KeyPress::spaceKey)
        return studioView.togglePlay();
    if (key.getTextCharacter() == 'r' || key.getTextCharacter() == 'R')
    {
        studioView.startRecording();
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

    auto header = r.removeFromTop (44);
    {
        auto titleArea = header.removeFromLeft (330);
        appTitle.setBounds (titleArea.removeFromTop (26));
        deviceLabel.setBounds (titleArea);
    }
    header.removeFromLeft (8);
    presetSelect.setBounds (header.removeFromLeft (230).reduced (0, 7));
    header.removeFromLeft (6);
    savePresetButton.setBounds (header.removeFromLeft (70).reduced (0, 7));
    header.removeFromLeft (4);
    deletePresetButton.setBounds (header.removeFromLeft (70).reduced (0, 7));
    audioButton.setBounds (header.removeFromRight (140).reduced (0, 7));
    header.removeFromRight (8);
    inputSelect.setBounds (header.removeFromRight (150).reduced (0, 7));
    header.removeFromRight (8);
    bpmLabel.setBounds (header.removeFromRight (84).reduced (0, 7));
    tapButton.setBounds (header.removeFromRight (56).reduced (0, 7));
    header.removeFromRight (10);
    tuner.setBounds (header.removeFromRight (juce::jmin (240, header.getWidth())).reduced (0, 4));

    r.removeFromTop (10);
    auto chainRow = r.removeFromTop (240);
    const int gap = 10;
    auto take = [&] (int w) { auto b = chainRow.removeFromLeft (w); chainRow.removeFromLeft (gap); return b; };

    inputPanel.setBounds (take (104));
    gatePanel.setBounds (take (110));
    outputPanel.setBounds (chainRow.removeFromRight (104));
    chainRow.removeFromRight (gap);
    eqPanel.setBounds (chainRow.removeFromRight (190));
    chainRow.removeFromRight (gap);
    cabPanel.setBounds (chainRow.removeFromRight (250));
    chainRow.removeFromRight (gap);
    capturePanel.setBounds (chainRow);

    // rack units
    {
        auto b = inputPanel.getLocalBounds().reduced (10).withTrimmedTop (24);
        inputMeter.setBounds (b.removeFromRight (12).reduced (0, 6));
        inputGain.setBounds (b.withSizeKeepingCentre (b.getWidth(), 112));
    }
    {
        auto b = gatePanel.getLocalBounds().reduced (10).withTrimmedTop (24);
        gateThreshold.setBounds (b.withSizeKeepingCentre (b.getWidth(), 112));
    }
    {
        auto b = eqPanel.getLocalBounds().reduced (10).withTrimmedTop (24);
        PanelLayout::row (b.withSizeKeepingCentre (b.getWidth(), 112), { &bass, &middle, &treble });
    }
    {
        auto b = outputPanel.getLocalBounds().reduced (10).withTrimmedTop (24);
        outputMeter.setBounds (b.removeFromRight (12).reduced (0, 6));
        outputGain.setBounds (b.withSizeKeepingCentre (b.getWidth(), 112));
    }

    // amp head: selectors on the grill, controls on the gold panel
    {
        auto grill = capturePanel.getLocalBounds().reduced (24, 14).withTrimmedTop (18);
        grill = grill.removeFromTop ((int) (capturePanel.getHeight() * 0.40f) - 30);
        auto left = grill.removeFromLeft ((int) (grill.getWidth() * 0.42f));
        ampSourceSelect.setBounds (left.removeFromTop (24));
        left.removeFromTop (4);
        voicingSelect.setBounds (left.removeFromTop (24));
        voicingInfo.setBounds (grill.withTrimmedTop (grill.getHeight() - 18).translated (0, 4));

        auto panel = capturePanel.controlPanelArea().reduced (6, 4);
        panel.removeFromLeft (42); // pilot light
        {
            auto knobs = panel;
            const int w = knobs.getWidth() / 7;
            for (auto* k : { &ampGain, &ampVolume, &ampBass, &ampMiddle, &ampTreble, &ampPresence, &ampMaster })
                k->setBounds (knobs.removeFromLeft (w));
        }
        auto cap = panel;
        auto buttons = cap.removeFromTop (26);
        loadCaptureButton.setBounds (buttons.removeFromLeft (170));
        buttons.removeFromLeft (6);
        prevCapture.setBounds (buttons.removeFromLeft (30));
        buttons.removeFromLeft (4);
        nextCapture.setBounds (buttons.removeFromLeft (30));
        cap.removeFromTop (4);
        auto text = cap.removeFromLeft (juce::jmin (cap.getWidth() - 180, 380));
        captureName.setBounds (text.removeFromTop (24));
        captureInfo.setBounds (text.removeFromTop (18));
        normalise.setBounds (text.removeFromTop (24));
        aidaKnob1.setBounds (cap.removeFromLeft (90));
        aidaKnob2.setBounds (cap.removeFromLeft (90));
    }

    // cabinet: controls over the grill cloth
    {
        auto b = cabPanel.getLocalBounds().reduced (24, 14).withTrimmedTop (18);
        cabModeSelect.setBounds (b.removeFromTop (24));
        b.removeFromTop (6);
        auto ir = b;
        loadIrButton.setBounds (ir.removeFromTop (28));
        ir.removeFromTop (6);
        irName.setBounds (ir.removeFromTop (44));
        clearIrButton.setBounds (ir.removeFromTop (26).removeFromLeft (90));
        auto sim = b;
        cabTypeSelect.setBounds (sim.removeFromTop (24));
        sim.removeFromTop (4);
        micSelect.setBounds (sim.removeFromTop (24));
        sim.removeFromTop (2);
        micPosition.setBounds (sim.removeFromLeft (sim.getWidth() / 2));
        micDistance.setBounds (sim);
    }

    r.removeFromTop (8);
    {
        auto tabs = r.removeFromTop (26);
        boardTab.setBounds (tabs.removeFromLeft (140));
        tabs.removeFromLeft (4);
        studioTab.setBounds (tabs.removeFromLeft (140));
    }
    r.removeFromTop (4);
    studioView.setBounds (r);
    pedalboardView.setBounds (r.removeFromTop (juce::jmax (370, r.getHeight() - 200)));
    r.removeFromTop (10);
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

    settings.setValue ("amp", juce::JSON::toString (ampStateVar(), true));
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
        applyAmpState (ampState);
    }
    else
    {
        // first start, or settings from Phase 1: an IR that was in use stays in use
        cabModeSelect.setSelectedId (currentIrFile.existsAsFile() ? 2 : 1, juce::sendNotificationSync);
        for (auto* k : { &ampGain, &ampVolume, &ampBass, &ampMiddle, &ampTreble, &ampPresence, &ampMaster, &micPosition, &micDistance })
            k->slider.setValue (k->slider.getValue(), juce::sendNotificationSync);
    }
    refreshPresetList (settings.getValue ("preset"));
    updateAmpControls();
}

juce::var MainComponent::ampStateVar() const
{
    auto& a = const_cast<SignalChain&> (chain).getAmp();
    auto& cab = const_cast<SignalChain&> (chain).getCabinet();
    auto* o = new juce::DynamicObject();
    o->setProperty ("source", ampSourceSelect.getSelectedId() == 2 ? "amp" : "capture");
    o->setProperty ("model", amp::voicing (a.voicingIndex.load()).id);
    o->setProperty ("gain", ampGain.slider.getValue());
    o->setProperty ("volume", ampVolume.slider.getValue());
    o->setProperty ("bass", ampBass.slider.getValue());
    o->setProperty ("middle", ampMiddle.slider.getValue());
    o->setProperty ("treble", ampTreble.slider.getValue());
    o->setProperty ("presence", ampPresence.slider.getValue());
    o->setProperty ("master", ampMaster.slider.getValue());
    o->setProperty ("cabinetMode", cabModeSelect.getSelectedId() == 2 ? "ir" : "simulated");
    o->setProperty ("cabinet", amp::cabinet (cab.cabinetIndex.load()).id);
    o->setProperty ("mic", amp::mic (cab.micIndex.load()).id);
    o->setProperty ("micPosition", micPosition.slider.getValue());
    o->setProperty ("micDistance", micDistance.slider.getValue());
    return juce::var (o);
}

void MainComponent::applyAmpState (const juce::var& ampState)
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

juce::var MainComponent::boardStateVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("pedalboard", const_cast<SignalChain&> (chain).getPedalboard().toVar());
    o->setProperty ("amp", ampStateVar());
    o->setProperty ("capture", currentCaptureFile.getFullPathName());
    o->setProperty ("ir", currentIrFile.getFullPathName());
    o->setProperty ("captureOn", capturePanel.bypass.getToggleState());
    o->setProperty ("irOn", cabPanel.bypass.getToggleState());
    o->setProperty ("normalise", normalise.getToggleState());
    o->setProperty ("gateOn", gatePanel.bypass.getToggleState());
    o->setProperty ("gateThreshold", gateThreshold.slider.getValue());
    o->setProperty ("bass", bass.slider.getValue());
    o->setProperty ("middle", middle.slider.getValue());
    o->setProperty ("treble", treble.slider.getValue());
    o->setProperty ("outputGain", outputGain.slider.getValue());
    return juce::var (o);
}

void MainComponent::applyBoardState (const juce::var& st)
{
    const juce::ScopedValueSetter<bool> scope (restoring, true); // one save at the end, not one per control
    const juce::File capture (st["capture"].toString()), ir (st["ir"].toString());
    if (capture.existsAsFile() && capture != currentCaptureFile)
        loadCapture (capture);
    if (ir.existsAsFile() && ir != currentIrFile)
        loadImpulseResponse (ir);

    auto toggle = [&] (ModulePanel& p, const char* key)
    {
        if (! st.hasProperty (key)) return;
        p.bypass.setToggleState ((bool) st[key], juce::dontSendNotification);
        if (p.onBypassChanged) p.onBypassChanged (p.bypass.getToggleState());
        p.repaint();
    };
    toggle (capturePanel, "captureOn");
    toggle (cabPanel, "irOn");
    toggle (gatePanel, "gateOn");
    if (st.hasProperty ("normalise"))
    {
        normalise.setToggleState ((bool) st["normalise"], juce::dontSendNotification);
        chain.normaliseLoudness = normalise.getToggleState();
    }
    auto num = [&] (const char* key, Knob& k)
    {
        if (st.hasProperty (key))
            k.slider.setValue ((double) st[key], juce::sendNotificationSync);
    };
    num ("gateThreshold", gateThreshold); num ("bass", bass); num ("middle", middle); num ("treble", treble); num ("outputGain", outputGain);
    if (st["amp"].isObject())
        applyAmpState (st["amp"]);
    if (st["pedalboard"].isObject())
        chain.getPedalboard().fromVar (st["pedalboard"]);
    pedalboardView.rebuild();
    bpmLabel.setText (juce::String (chain.getPedalboard().tempo().bpm.load(), 1) + " BPM", juce::dontSendNotification);
    updateAmpControls();
}

void MainComponent::refreshPresetList (const juce::String& select)
{
    presetSelect.clear (juce::dontSendNotification);
    const auto names = presets.names();
    for (int i = 0; i < names.size(); ++i)
        presetSelect.addItem (names[i], i + 1);
    const int index = names.indexOf (select);
    if (index >= 0)
        presetSelect.setSelectedId (index + 1, juce::dontSendNotification);
    deletePresetButton.setEnabled (index >= 0);
}

void MainComponent::savePresetAs()
{
    auto* w = new juce::AlertWindow (utf8 ("Salvar pedaleira"), utf8 ("Nome da pedaleira (amp, gabinete, pedais, ordem e BPM):"),
                                     juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", presetSelect.getSelectedId() > 0 ? presetSelect.getText() : juce::String());
    w->addButton ("Salvar", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int result)
    {
        if (result != 1)
            return;
        const auto name = PresetStore::sanitise (w->getTextEditorContents ("name"));
        juce::String error;
        if (! presets.save (name, boardStateVar(), error))
        {
            showError ("Pedaleira", error);
            return;
        }
        settings.setValue ("preset", name);
        refreshPresetList (name);
    }), true);
}

void MainComponent::deleteCurrentPreset()
{
    const auto name = presetSelect.getText();
    if (presetSelect.getSelectedId() <= 0)
        return;
    const auto options = juce::MessageBoxOptions{}.withIconType (juce::MessageBoxIconType::QuestionIcon).withTitle (utf8 ("Excluir pedaleira")).withMessage (utf8 ("Excluir \"") + name + "\"?").withButton ("Excluir", 1).withButton ("Cancelar", 0).withAssociatedComponent (this);
    juce::AlertWindow::showAsync (options, [this, name] (int ok)
    {
        if (ok == 1 && presets.remove (name))
            refreshPresetList();
    });
}

void MainComponent::updateAmpControls()
{
    const bool model = ampSourceSelect.getSelectedId() == 2;
    for (juce::Component* c : std::initializer_list<juce::Component*> { &ampGain, &ampVolume, &ampBass,
                                                                        &ampMiddle, &ampTreble, &ampPresence, &ampMaster })
        c->setVisible (model);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &loadCaptureButton, &prevCapture, &nextCapture, &captureName,
                                                                        &captureInfo, &normalise })
        c->setVisible (! model);
    aidaKnob1.setVisible (! model && chain.getCaptureConditioningInputs() >= 1);
    aidaKnob2.setVisible (! model && chain.getCaptureConditioningInputs() >= 2);
    voicingInfo.setText (model ? utf8 (amp::voicing (voicingSelect.getSelectedId() - 1).description) : utf8 ("Captura NAM / AIDA-X"), juce::dontSendNotification);
    capturePanel.setHeadline (model ? utf8 (amp::voicing (voicingSelect.getSelectedId() - 1).name)
                                    : (chain.getCaptureName().isEmpty() ? juce::String ("CAPTURA") : chain.getCaptureName()));
    voicingSelect.setVisible (model);

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

namespace gsp::ui
{
void MainComponent::showTab (bool studio)
{
    (studio ? studioTab : boardTab).setToggleState (true, juce::dontSendNotification);
    pedalboardView.setVisible (! studio);
    browser.setVisible (! studio);
    studioView.setVisible (studio);
    settings.setValue ("studioTab", studio);
}

juce::String MainComponent::audioProblem() const
{
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr || ! device->isPlaying())
        return utf8 ("Nenhuma interface de áudio aberta. Clique em Configurar áudio antes de gravar.");
    if (device->getActiveInputChannels().isZero())
        return utf8 ("A interface não tem nenhuma entrada ativa. Ative a entrada da guitarra em Configurar áudio.");
    return {};
}
} // namespace gsp::ui
