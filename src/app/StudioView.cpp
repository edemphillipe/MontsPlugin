#include "StudioView.h"

namespace gsp::ui
{
using studio::StudioEngine;

namespace
{
const juce::Colour recordRed { 0xffe0433a };
const juce::Colour laneBg { 0xff1b1d22 };
const juce::Colour clipColour { 0xff3c4a5c };
const juce::Colour waveColour { 0xffc9d6e6 };

juce::String formatTime (double seconds)
{
    seconds = juce::jmax (0.0, seconds);
    const int m = (int) (seconds / 60.0);
    const double s = seconds - m * 60.0;
    return juce::String (m) + ":" + juce::String (s, 1).paddedLeft ('0', 4);
}

void setupSmallSlider (juce::Slider& s, double min, double max, double def, const juce::String& suffix)
{
    s.setSliderStyle (juce::Slider::LinearHorizontal);
    s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    s.setRange (min, max, 0.1);
    s.setValue (def, juce::dontSendNotification);
    s.setDoubleClickReturnValue (true, def);
    s.setTextValueSuffix (suffix);
    s.setPopupDisplayEnabled (true, true, nullptr);
    s.setColour (juce::Slider::trackColourId, colours::accent.withAlpha (0.7f));
    s.setColour (juce::Slider::backgroundColourId, colours::ledOff);
    s.setColour (juce::Slider::thumbColourId, colours::text);
}

void setupToggle (juce::TextButton& b, juce::Colour on)
{
    b.setClickingTogglesState (true);
    b.setColour (juce::TextButton::buttonOnColourId, on);
    b.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
}
} // namespace

namespace
{
// type straight into the name field of a dialog (the old name is selected)
void focusText (juce::AlertWindow& w)
{
    if (auto* ed = w.getTextEditor ("name"))
    {
        ed->grabKeyboardFocus();
        ed->selectAll();
    }
}
} // namespace

// ================================================================ TrackHeader
TrackHeader::TrackHeader()
{
    name.setEditable (false, true);
    name.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    name.setColour (juce::Label::textColourId, colours::text);
    name.setTooltip (utf8 ("Clique duas vezes para renomear"));
    addAndMakeVisible (name);

    setupToggle (arm, recordRed);
    arm.setTooltip (utf8 ("Armar: a próxima gravação substitui esta faixa (a gravação antiga continua salva na pasta)"));
    setupToggle (mute, juce::Colour (0xff5aa0e0));
    mute.setTooltip ("Mudo");
    setupToggle (solo, colours::accent);
    solo.setTooltip ("Solo");
    reamp.setTooltip (utf8 ("Reamp: toca a gravação limpa desta faixa pelo amp e pedais atuais e grava o novo som"));
    remove.setTooltip ("Apagar a faixa do projeto (os arquivos ficam na pasta Takes)");
    takes.setTooltip (utf8 ("Takes desta faixa: voltar para uma gravação anterior ou carregar o timbre usado nela"));
    for (auto* b : { &arm, &mute, &solo, &reamp, &remove, &takes })
        addAndMakeVisible (b);

    setupSmallSlider (volume, -60.0, 6.0, 0.0, " dB");
    volume.setTooltip ("Volume");
    setupSmallSlider (pan, -1.0, 1.0, 0.0, "");
    pan.setRange (-1.0, 1.0, 0.01);
    pan.setTooltip ("Pan: esquerda / direita");
    addAndMakeVisible (volume);
    addAndMakeVisible (pan);
}

void TrackHeader::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (colours::panel);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (selected ? recordRed : colours::panelEdge);
    g.drawRoundedRectangle (r, 5.0f, selected ? 1.6f : 1.0f);
    g.setColour (colours::textDim);
    g.setFont (juce::FontOptions (10.0f));
    const auto row2 = getLocalBounds().reduced (6, 3).removeFromBottom (18);
    g.drawText ("VOL", row2.withWidth (24), juce::Justification::centredLeft);
    g.drawText ("PAN", row2.withX (row2.getX() + 156).withWidth (24), juce::Justification::centredLeft);
}

void TrackHeader::resized()
{
    auto r = getLocalBounds().reduced (6, 3);
    auto row1 = r.removeFromTop (22);
    remove.setBounds (row1.removeFromRight (24));
    row1.removeFromRight (4);
    solo.setBounds (row1.removeFromRight (24));
    row1.removeFromRight (2);
    mute.setBounds (row1.removeFromRight (24));
    row1.removeFromRight (2);
    arm.setBounds (row1.removeFromRight (24));
    row1.removeFromRight (4);
    takes.setBounds (row1.removeFromRight (56));
    row1.removeFromRight (4);
    name.setBounds (row1);
    auto row2 = r.removeFromBottom (18);
    row2.removeFromLeft (24);
    volume.setBounds (row2.removeFromLeft (128));
    row2.removeFromLeft (28);
    pan.setBounds (row2.removeFromLeft (52));
    row2.removeFromLeft (4);
    reamp.setBounds (row2);
}

// ================================================================ StudioView
StudioView::StudioView (StudioEngine& e, studio::Project& p, juce::PropertiesFile& s)
    : engine (e), project (p), settings (s)
{
    projectSelect.setTextWhenNothingSelected ("Projeto");
    projectSelect.setTooltip (utf8 ("Projetos de gravação (pasta Documentos/Guitar Studio Pro/Gravações)"));
    projectSelect.onChange = [this]
    {
        if (projectSelect.getSelectedId() > 0)
            openProject (projectsRoot().getChildFile (projectSelect.getText()));
    };
    newProjectButton.onClick = [this] { newProject(); };
    projectMenuButton.onClick = [this] { showProjectMenu(); };
    projectMenuButton.setTooltip (utf8 ("Renomear, apagar ou abrir a pasta do projeto"));

    toStartButton.setTooltip (utf8 ("Voltar ao início"));
    toStartButton.onClick = [this] { project.seek (0); repaint(); };
    playButton.setTooltip (utf8 ("Tocar / pausar (barra de espaço)"));
    playButton.onClick = [this] { togglePlay(); };
    stopButton.onClick = [this] { project.stop(); };
    recordButton.setTooltip (utf8 ("Gravar (tecla R). Grava a guitarra limpa (DI) e o som com amp e pedais"));
    recordButton.onClick = [this] { startRecording(); };
    recordButton.setColour (juce::TextButton::buttonColourId, recordRed.darker (0.6f));
    timeLabel.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    timeLabel.setJustificationType (juce::Justification::centred);
    timeLabel.setColour (juce::Label::textColourId, colours::accent);
    timeLabel.setColour (juce::Label::backgroundColourId, juce::Colours::black.withAlpha (0.4f));

    clickToggle.setTooltip (utf8 ("Metrônomo no BPM do TAP (só você ouve; não vai para a gravação nem para o mix)"));
    clickToggle.onClick = [this]
    {
        project.session().clickOn = clickToggle.getToggleState();
        project.pushMixer();
        project.save();
    };
    setupSmallSlider (clickVolume, -40.0, 0.0, -12.0, " dB");
    clickVolume.setTooltip (utf8 ("Volume do metrônomo"));
    clickVolume.onValueChange = [this] { project.session().clickVolumeDb = (float) clickVolume.getValue(); project.pushMixer(); };
    clickVolume.onDragEnd = [this] { project.save(); };
    exportButton.setTooltip (utf8 ("Junta todas as faixas (com volume, pan, mudo e solo) e a música de referência num WAV estéreo"));
    exportButton.onClick = [this] { exportMix(); };

    refButton.onClick = [this] { chooseReference(); };
    refButton.setTooltip (utf8 ("Carregar uma música para tocar junto (WAV, AIFF, FLAC, MP3, OGG...)"));
    refRemove.onClick = [this] { project.clearBacking(); };
    setupSmallSlider (refVolume, -40.0, 6.0, -6.0, " dB");
    refVolume.setTooltip (utf8 ("Volume da música de referência"));
    refVolume.onValueChange = [this] { project.session().backingVolumeDb = (float) refVolume.getValue(); project.pushMixer(); };
    refVolume.onDragEnd = [this] { project.save(); };
    refMute.onClick = [this] { project.session().backingMute = refMute.getToggleState(); project.pushMixer(); project.save(); };
    refName.setColour (juce::Label::textColourId, colours::text);
    refName.setColour (juce::Label::backgroundColourId, juce::Colours::black.withAlpha (0.55f));
    refName.setFont (juce::FontOptions (11.0f));
    refName.setInterceptsMouseClicks (false, false);

    noticeLabel.setColour (juce::Label::textColourId, colours::textDim);
    noticeLabel.setFont (juce::FontOptions (12.0f));

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &projectSelect, &newProjectButton, &projectMenuButton, &toStartButton, &playButton, &stopButton, &recordButton,
             &timeLabel, &clickToggle, &clickVolume, &exportButton, &refButton, &refRemove, &refName, &refVolume, &refMute, &noticeLabel })
        addAndMakeVisible (c);

    project.onChanged = [this] { needsRefresh = true; };
    startTimerHz (30);
}

StudioView::~StudioView()
{
    project.onChanged = nullptr;
    for (auto* t : thumbs)
        t->removeChangeListener (this);
    if (refThumb != nullptr)
        refThumb->removeChangeListener (this);
}

juce::File StudioView::projectsRoot() const
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("Guitar Studio Pro")
        .getChildFile (utf8 ("Gravações"));
}

void StudioView::refreshProjectList()
{
    projectSelect.clear (juce::dontSendNotification);
    auto dirs = projectsRoot().findChildFiles (juce::File::findDirectories, false);
    dirs.sort();
    int id = 1;
    for (const auto& d : dirs)
    {
        projectSelect.addItem (d.getFileName(), id);
        if (d == project.getFolder())
            projectSelect.setSelectedId (id, juce::dontSendNotification);
        ++id;
    }
}

void StudioView::openInitialProject()
{
    const juce::File last (settings.getValue ("studioProject"));
    if (last.isDirectory())
        openProject (last);
    else
        openProject (projectsRoot().getChildFile ("Projeto 1"));
}

void StudioView::openProject (const juce::File& folder)
{
    juce::String err;
    if (! project.open (folder, err))
    {
        notify (err, true);
        refreshProjectList();
        return;
    }
    settings.setValue ("studioProject", folder.getFullPathName());
    armed = -1;
    refreshProjectList();
    refresh();
    const auto n = project.getLastNotice();
    notify (n.isNotEmpty() ? n : utf8 ("Projeto \"") + folder.getFileName() + utf8 ("\" aberto."), n.isNotEmpty());
}

void StudioView::newProject()
{
    auto* w = new juce::AlertWindow (utf8 ("Novo projeto de gravação"), utf8 ("Nome do projeto (ex.: nome da música):"),
                                     juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", {});
    w->addButton ("Criar", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int result)
    {
        if (result != 1)
            return;
        const auto name = juce::File::createLegalFileName (w->getTextEditorContents ("name").trim());
        if (name.isEmpty())
            return;
        openProject (projectsRoot().getChildFile (name));
    }), true);
    focusText (*w);
}

void StudioView::showProjectMenu()
{
    if (! project.isOpen())
        return;
    juce::PopupMenu m;
    m.addItem (1, "Renomear projeto...");
    m.addItem (2, "Abrir a pasta do projeto");
    m.addSeparator();
    m.addItem (3, utf8 ("Apagar projeto (vai para a lixeira)..."));
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (projectMenuButton), [this] (int r)
    {
        if (r == 1) renameProject();
        if (r == 2) project.getFolder().revealToUser();
        if (r == 3) deleteProject();
    });
}

void StudioView::renameProject()
{
    auto* w = new juce::AlertWindow ("Renomear projeto", "Novo nome do projeto:", juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", project.getFolder().getFileName());
    w->addButton ("Renomear", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    const auto oldName = project.getFolder().getFileName();
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, oldName] (int result)
    {
        if (result != 1 || w->getTextEditorContents ("name").trim() == oldName)
            return;
        juce::String err;
        reampingTrack = -1;
        const bool ok = project.rename (w->getTextEditorContents ("name"), err);
        if (project.isOpen())
            settings.setValue ("studioProject", project.getFolder().getFullPathName());
        refreshProjectList();
        refresh();
        if (ok)
            notify (utf8 ("Projeto renomeado para \"") + project.getFolder().getFileName() + "\".");
        else
            notify (err, true);
    }), true);
    focusText (*w);
}

void StudioView::deleteProject()
{
    const auto name = project.getFolder().getFileName();
    const auto options = juce::MessageBoxOptions{}.withIconType (juce::MessageBoxIconType::WarningIcon)
        .withTitle ("Apagar projeto")
        .withMessage (utf8 ("Mover o projeto \"") + name + utf8 ("\" para a lixeira? Todas as gravações dele vão junto (dá para recuperar pela lixeira)."))
        .withButton ("Apagar", 1).withButton ("Cancelar", 0).withAssociatedComponent (this);
    juce::AlertWindow::showAsync (options, [this, name] (int ok)
    {
        if (ok != 1)
            return;
        juce::String err;
        reampingTrack = -1;
        armed = -1;
        if (! project.moveToTrash (err))
        {
            notify (err, true);
            return;
        }
        auto dirs = projectsRoot().findChildFiles (juce::File::findDirectories, false);
        dirs.sort();
        openProject (dirs.isEmpty() ? projectsRoot().getChildFile ("Projeto 1") : dirs[0]);
        notify (utf8 ("Projeto \"") + name + utf8 ("\" foi para a lixeira."));
    });
}

void StudioView::showTakesMenu (int track)
{
    if (track < 0 || track >= (int) project.session().tracks.size())
        return;
    const auto& t = project.session().tracks[(size_t) track];
    auto labelOf = [] (const studio::TakeInfo& k)
    {
        if (k.label.isNotEmpty())
            return k.label;
        return juce::File (k.wetFile).getFileNameWithoutExtension().upToFirstOccurrenceOf ("_", false, false);
    };
    const auto cur = t.current();
    juce::PopupMenu m;
    m.addSectionHeader ("Take em uso: " + labelOf (cur));
    m.addItem (1, utf8 ("Carregar o timbre deste take na pedaleira"), cur.tone.isObject());
    if (! cur.tone.isObject())
        m.addItem (-1, utf8 ("(take gravado antes desta versão: sem timbre salvo)"), false);
    if (! t.history.empty())
    {
        m.addSeparator();
        m.addSectionHeader ("Takes anteriores");
        for (int k = (int) t.history.size() - 1; k >= 0; --k) // newest first
        {
            const auto& h = t.history[(size_t) k];
            juce::PopupMenu sub;
            sub.addItem (1000 + k, "Usar este take");
            sub.addItem (2000 + k, "Carregar o timbre dele na pedaleira", h.tone.isObject());
            m.addSubMenu (labelOf (h), sub);
        }
    }
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (headers[track]->takes), [this, track] (int r)
    {
        if (track >= (int) project.session().tracks.size())
            return;
        const auto& tr = project.session().tracks[(size_t) track];
        auto loadTone = [this] (const juce::var& tone, const juce::String& what)
        {
            if (onLoadTone != nullptr && tone.isObject())
            {
                onLoadTone (tone);
                notify (utf8 ("Timbre de \"") + what + utf8 ("\" carregado na pedaleira."));
            }
        };
        if (r == 1)
            loadTone (tr.tone, tr.takeLabel.isNotEmpty() ? tr.takeLabel : tr.name);
        else if (r >= 2000 && r - 2000 < (int) tr.history.size())
            loadTone (tr.history[(size_t) (r - 2000)].tone, tr.history[(size_t) (r - 2000)].label);
        else if (r >= 1000 && r - 1000 < (int) tr.history.size())
        {
            reampingTrack = -1;
            project.useTake (track, r - 1000);
            notify (utf8 ("Take trocado na ") + project.session().tracks[(size_t) track].name + utf8 (". O anterior continua na lista de takes."));
        }
    });
}

void StudioView::refresh()
{
    needsRefresh = false;
    for (auto* t : thumbs)
        t->removeChangeListener (this);
    thumbs.clear();
    headers.clear();

    auto& s = project.session();
    if (armed >= (int) s.tracks.size())
        armed = -1;
    for (int i = 0; i < (int) s.tracks.size(); ++i)
    {
        const auto& t = s.tracks[(size_t) i];
        auto* h = headers.add (new TrackHeader());
        h->name.setText (t.name, juce::dontSendNotification);
        h->mute.setToggleState (t.mute, juce::dontSendNotification);
        h->solo.setToggleState (t.solo, juce::dontSendNotification);
        h->volume.setValue (t.volumeDb, juce::dontSendNotification);
        h->pan.setValue (t.pan, juce::dontSendNotification);
        h->arm.setToggleState (armed == i, juce::dontSendNotification);
        h->selected = armed == i;
        h->reamp.setEnabled (t.diFile.isNotEmpty());
        h->takes.setButtonText (t.history.empty() ? "Takes" : "Takes " + juce::String ((int) t.history.size() + 1));
        h->takes.onClick = [this, i] { showTakesMenu (i); };

        h->name.onTextChange = [this, i, h]
        {
            project.session().tracks[(size_t) i].name = h->name.getText().trim().isEmpty() ? "Faixa " + juce::String (i + 1) : h->name.getText().trim();
            project.save();
        };
        h->arm.onClick = [this, i]
        {
            armed = (armed == i) ? -1 : i;
            for (int k = 0; k < headers.size(); ++k)
            {
                headers[k]->arm.setToggleState (armed == k, juce::dontSendNotification);
                headers[k]->selected = armed == k;
                headers[k]->repaint();
            }
        };
        h->mute.onClick = [this, i, h] { project.session().tracks[(size_t) i].mute = h->mute.getToggleState(); project.pushMixer(); project.save(); repaint(); };
        h->solo.onClick = [this, i, h] { project.session().tracks[(size_t) i].solo = h->solo.getToggleState(); project.pushMixer(); project.save(); repaint(); };
        h->volume.onValueChange = [this, i, h] { project.session().tracks[(size_t) i].volumeDb = (float) h->volume.getValue(); project.pushMixer(); };
        h->pan.onValueChange = [this, i, h] { project.session().tracks[(size_t) i].pan = (float) h->pan.getValue(); project.pushMixer(); };
        h->volume.onDragEnd = h->pan.onDragEnd = [this] { project.save(); };
        h->reamp.onClick = [this, i]
        {
            if (checkAudio != nullptr)
                if (const auto problem = checkAudio(); problem.isNotEmpty())
                {
                    notify (problem, true);
                    return;
                }
            if (reampingTrack == i)
            {
                project.stop(); // cancel: the track keeps its previous sound
                return;
            }
            juce::String err;
            if (project.reamp (i, err))
            {
                reampingTrack = i;
                takeStartSeconds = (double) engine.getPosition() / engine.getSampleRate();
            }
            if (reampingTrack == i)
                notify (utf8 ("Reamp da ") + project.session().tracks[(size_t) i].name
                        + utf8 (": tocando a gravação limpa pelo amp e pedais atuais. Para quando a faixa terminar."));
            else
                notify (err, true);
        };
        h->remove.onClick = [this, i]
        {
            const auto name = project.session().tracks[(size_t) i].name;
            const auto options = juce::MessageBoxOptions{}.withIconType (juce::MessageBoxIconType::QuestionIcon).withTitle ("Apagar faixa").withMessage (utf8 ("Apagar \"") + name + utf8 ("\" do projeto? Os arquivos de áudio continuam na pasta Takes.")).withButton ("Apagar", 1).withButton ("Cancelar", 0).withAssociatedComponent (this);
            juce::AlertWindow::showAsync (options, [this, i] (int ok)
            {
                if (ok == 1)
                {
                    if (armed == i)
                        armed = -1;
                    project.removeTrack (i);
                }
            });
        };
        addAndMakeVisible (h);

        auto* th = thumbs.add (new juce::AudioThumbnail (256, project.getFormats(), thumbCache));
        const auto f = project.fileFor (t.wetFile);
        if (t.wetFile.isNotEmpty() && f.existsAsFile())
            th->setSource (new juce::FileInputSource (f));
        th->addChangeListener (this);
    }

    if (refThumb != nullptr)
        refThumb->removeChangeListener (this);
    refThumb.reset();
    if (s.backingFile.isNotEmpty())
    {
        const juce::File f (s.backingFile);
        refName.setText (f.getFileName(), juce::dontSendNotification);
        refThumb = std::make_unique<juce::AudioThumbnail> (512, project.getFormats(), thumbCache);
        if (f.existsAsFile())
            refThumb->setSource (new juce::FileInputSource (f));
        refThumb->addChangeListener (this);
    }
    else
    {
        refName.setText (utf8 ("Sem música de referência. Carregue uma música para tocar junto."), juce::dontSendNotification);
    }
    refRemove.setEnabled (s.backingFile.isNotEmpty());
    refVolume.setValue (s.backingVolumeDb, juce::dontSendNotification);
    refMute.setToggleState (s.backingMute, juce::dontSendNotification);
    clickToggle.setToggleState (s.clickOn, juce::dontSendNotification);
    clickVolume.setValue (s.clickVolumeDb, juce::dontSendNotification);

    if (project.getLastNotice().isNotEmpty())
        notify (project.getLastNotice(), project.getLastNotice().contains (utf8 ("Atenção")) || project.getLastNotice().contains (utf8 ("não")));

    lastViewSeconds = 0.0;
    resized();
    repaint();
}

void StudioView::changeListenerCallback (juce::ChangeBroadcaster*) { repaint (tracksArea.getUnion (refArea)); }

void StudioView::notify (const juce::String& text, bool error)
{
    noticeLabel.setText (text, juce::dontSendNotification);
    noticeLabel.setColour (juce::Label::textColourId, error ? recordRed.brighter (0.3f) : colours::textDim);
}

bool StudioView::togglePlay()
{
    if (engine.getState() == StudioEngine::State::stopped)
        project.play();
    else
        project.stop();
    updateTransportButtons();
    return true;
}

void StudioView::startRecording()
{
    if (engine.getState() == StudioEngine::State::recording)
    {
        project.stop();
        return;
    }
    if (checkAudio != nullptr)
        if (const auto problem = checkAudio(); problem.isNotEmpty())
        {
            notify (problem, true);
            return;
        }
    juce::String err;
    if (engine.getState() != StudioEngine::State::stopped)
        project.stop();
    takeStartSeconds = (double) engine.getPosition() / engine.getSampleRate();
    if (project.record (armed, err))
        notify (armed >= 0 ? utf8 ("Gravando por cima da ") + project.session().tracks[(size_t) armed].name + utf8 ("... (Parar ou espaço para terminar)")
                           : utf8 ("Gravando numa faixa nova... (Parar ou espaço para terminar)"));
    else
        notify (err, true);
    updateTransportButtons();
}

void StudioView::exportMix()
{
    if (! project.isOpen())
        return;
    project.stop();
    chooser = std::make_unique<juce::FileChooser> ("Exportar mix", project.getFolder().getChildFile ("Mix.wav"), "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f == juce::File())
            return;
        if (! f.hasFileExtension ("wav"))
            f = f.withFileExtension ("wav");
        juce::String err;
        float peak = 0.0f;
        if (! project.exportMix (f, err, &peak))
        {
            notify (err, true);
            return;
        }
        const auto peakDb = juce::Decibels::gainToDecibels (peak, -100.0f);
        notify ("Mix exportado: " + f.getFileName() + " (pico " + juce::String (peakDb, 1) + " dBFS)"
                    + (peak >= 1.0f ? utf8 (". Passou de 0 dB e vai distorcer: abaixe os volumes das faixas.") : juce::String()),
                peak >= 1.0f);
    });
}

void StudioView::chooseReference()
{
    if (! project.isOpen())
        return;
    chooser = std::make_unique<juce::FileChooser> (utf8 ("Música de referência"),
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   project.getFormats().getWildcardForAllFormats());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f == juce::File())
            return;
        juce::String err;
        if (project.setBacking (f, err))
            notify (utf8 ("Música de referência: ") + f.getFileName());
        else
            notify (err, true);
    });
}

void StudioView::updateTransportButtons()
{
    const auto st = engine.getState();
    const bool rec = st == StudioEngine::State::recording, reamping = st == StudioEngine::State::reamping;
    playButton.setButtonText (st == StudioEngine::State::playing ? "Pausar" : "Tocar");
    recordButton.setButtonText (rec ? "Parar REC" : "Gravar");
    const bool blink = std::fmod (juce::Time::getMillisecondCounterHiRes() * 0.001, 1.0) < 0.5;
    recordButton.setColour (juce::TextButton::buttonColourId, rec ? (blink ? recordRed : recordRed.darker (0.3f)) : recordRed.darker (0.6f));
    if (! reamping)
        reampingTrack = -1;
    for (int i = 0; i < headers.size(); ++i)
        headers[i]->reamp.setButtonText (i == reampingTrack ? "Parar" : "Reamp");
}

// ---------------------------------------------------------------- geometry
double StudioView::viewSeconds() const
{
    const double rate = engine.getSampleRate();
    const double len = (double) project.lengthSamples() / rate;
    const double pos = (double) engine.getPosition() / rate;
    const double ref = refThumb != nullptr ? refThumb->getTotalLength() : 0.0;
    return juce::jmax (juce::jmax (30.0, len * 1.1, ref * 1.02), pos + 5.0, lastViewSeconds);
}

float StudioView::timeToX (double seconds) const
{
    const auto a = lanesArea();
    return (float) a.getX() + (float) (seconds / viewSeconds()) * (float) a.getWidth();
}

double StudioView::xToTime (float x) const
{
    const auto a = lanesArea();
    return juce::jmax (0.0, (double) (x - (float) a.getX()) / (double) juce::jmax (1, a.getWidth()) * viewSeconds());
}

juce::Rectangle<int> StudioView::lanesArea() const
{
    return tracksArea.withTrimmedLeft (headerWidth);
}

juce::Rectangle<int> StudioView::laneRect (int i) const
{
    const auto a = lanesArea();
    return { a.getX(), a.getY() + rulerHeight + i * laneHeight, a.getWidth(), laneHeight - 4 };
}

void StudioView::resized()
{
    auto r = getLocalBounds().reduced (8, 6);
    auto top = r.removeFromTop (30);
    projectSelect.setBounds (top.removeFromLeft (200));
    top.removeFromLeft (4);
    newProjectButton.setBounds (top.removeFromLeft (100));
    top.removeFromLeft (4);
    projectMenuButton.setBounds (top.removeFromLeft (86));
    top.removeFromLeft (20);
    toStartButton.setBounds (top.removeFromLeft (36));
    top.removeFromLeft (4);
    playButton.setBounds (top.removeFromLeft (70));
    top.removeFromLeft (4);
    stopButton.setBounds (top.removeFromLeft (62));
    top.removeFromLeft (4);
    recordButton.setBounds (top.removeFromLeft (90));
    top.removeFromLeft (8);
    timeLabel.setBounds (top.removeFromLeft (100));
    exportButton.setBounds (top.removeFromRight (110));
    top.removeFromRight (16);
    clickVolume.setBounds (top.removeFromRight (90).reduced (0, 5));
    clickToggle.setBounds (top.removeFromRight (104));

    r.removeFromTop (8);
    noticeLabel.setBounds (r.removeFromBottom (20));
    r.removeFromBottom (2);

    refArea = r.removeFromTop (refHeight);
    {
        auto h = refArea.withWidth (headerWidth - 6).reduced (6, 4);
        auto row1 = h.removeFromTop (22);
        refButton.setBounds (row1.removeFromLeft (150));
        row1.removeFromLeft (4);
        refRemove.setBounds (row1.removeFromLeft (64));
        row1.removeFromLeft (4);
        refMute.setBounds (row1);
        auto row2 = h.removeFromBottom (18);
        row2.removeFromLeft (24);
        refVolume.setBounds (row2.removeFromLeft (180));
        refName.setBounds (refArea.withTrimmedLeft (headerWidth).reduced (6, 3).removeFromTop (16)
                               .withWidth (juce::jmin (refArea.getWidth() - headerWidth - 12,
                                                       juce::GlyphArrangement::getStringWidthInt (refName.getFont(), refName.getText()) + 16)));
    }
    r.removeFromTop (6);
    tracksArea = r;

    const int lanes = juce::jmax (4, headers.size() + 1);
    laneHeight = juce::jlimit (40, 62, (tracksArea.getHeight() - rulerHeight) / juce::jmin (lanes, studio::maxTracks));
    for (int i = 0; i < headers.size(); ++i)
    {
        const auto lane = laneRect (i);
        headers[i]->setBounds (tracksArea.getX(), lane.getY(), headerWidth - 6, lane.getHeight());
    }
}

// ---------------------------------------------------------------- drawing
void StudioView::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
    const double rate = engine.getSampleRate();
    const auto st = engine.getState();
    const auto lanes = lanesArea();

    // reference music lane
    {
        auto head = refArea.withWidth (headerWidth - 6).toFloat();
        g.setColour (colours::panel);
        g.fillRoundedRectangle (head, 5.0f);
        g.setColour (colours::textDim);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText ("VOL", refArea.withWidth (headerWidth - 6).reduced (6, 4).removeFromBottom (18).withWidth (24), juce::Justification::centredLeft);
        const auto wave = refArea.withTrimmedLeft (headerWidth);
        g.setColour (laneBg);
        g.fillRoundedRectangle (wave.toFloat(), 4.0f);
        if (refThumb != nullptr && refThumb->getTotalLength() > 0.0)
        {
            const float x1 = timeToX (refThumb->getTotalLength());
            const juce::Rectangle<float> clip ((float) wave.getX(), (float) wave.getY() + 2.0f, juce::jmin (x1, (float) wave.getRight()) - (float) wave.getX(), (float) wave.getHeight() - 4.0f);
            g.setColour (juce::Colour (0xff2d4636));
            g.fillRoundedRectangle (clip, 3.0f);
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (wave);
            g.setColour (juce::Colour (0xff8fd0a0).withAlpha (project.session().backingMute ? 0.3f : 0.85f));
            refThumb->drawChannels (g, juce::Rectangle<int> (wave.getX(), wave.getY() + 4, (int) (x1 - (float) wave.getX()), wave.getHeight() - 8),
                                    0.0, refThumb->getTotalLength(), 1.0f);
        }
    }

    // ruler
    {
        const auto ruler = lanes.withHeight (rulerHeight);
        g.setColour (colours::panel);
        g.fillRect (ruler);
        const double secs = viewSeconds();
        const double pxPerSec = lanes.getWidth() / secs;
        double step = 1.0;
        for (double s : { 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0 })
            if (s * pxPerSec >= 60.0) { step = s; break; }
        g.setFont (juce::FontOptions (10.0f));
        for (double t = 0.0; t <= secs; t += step)
        {
            const float x = timeToX (t);
            g.setColour (colours::panelEdge);
            g.drawVerticalLine ((int) x, (float) ruler.getY(),
                                (float) laneRect (juce::jmin (studio::maxTracks, (int) project.session().tracks.size() + 1) - 1).getBottom());
            g.setColour (colours::textDim);
            if (x < (float) ruler.getRight() - 30.0f)
                g.drawText (formatTime (t).upToLastOccurrenceOf (".", false, false), (int) x + 3, ruler.getY(), 50, rulerHeight, juce::Justification::centredLeft);
        }
    }

    // track lanes
    const int nTracks = (int) project.session().tracks.size();
    const int recLane = st == StudioEngine::State::recording ? (armed >= 0 ? armed : nTracks) : -1;
    for (int i = 0; i < juce::jmin (studio::maxTracks, nTracks + 1); ++i)
    {
        const auto lane = laneRect (i);
        g.setColour (laneBg.withAlpha (0.85f));
        g.fillRoundedRectangle (lane.toFloat(), 4.0f);
        if (i < nTracks && i < thumbs.size())
        {
            const auto& t = project.session().tracks[(size_t) i];
            auto* th = thumbs[i];
            const double len = th->getTotalLength();
            if (len > 0.0 && i != recLane)
            {
                const float x0 = timeToX (t.wetStartSeconds), x1 = timeToX (t.wetStartSeconds + len);
                juce::Graphics::ScopedSaveState ss (g);
                g.reduceClipRegion (lane);
                bool anySolo = false;
                for (const auto& other : project.session().tracks)
                    anySolo = anySolo || other.solo;
                const bool audible = ! t.mute && (! anySolo || t.solo);
                g.setColour (clipColour.withAlpha (audible ? 1.0f : 0.4f));
                g.fillRoundedRectangle (x0, (float) lane.getY() + 2.0f, x1 - x0, (float) lane.getHeight() - 4.0f, 3.0f);
                g.setColour (waveColour.withAlpha (audible ? 0.9f : 0.3f));
                th->drawChannels (g, juce::Rectangle<int> ((int) x0, lane.getY() + 3, (int) (x1 - x0), lane.getHeight() - 6), 0.0, len, 1.0f);
                g.setColour (colours::text.withAlpha (0.6f));
                g.setFont (juce::FontOptions (10.0f));
                g.drawText (t.wetFile.contains ("reamp") ? "reamp" : "", (int) x0 + 4, lane.getY() + 2, 60, 12, juce::Justification::left);
            }
        }
        else if (i == nTracks && recLane != i)
        {
            g.setColour (colours::textDim);
            g.setFont (juce::FontOptions (12.0f));
            g.drawText (utf8 ("Aperte Gravar (ou R) para gravar uma faixa nova. Para regravar uma faixa, clique no ● dela antes."),
                        lane.reduced (12, 0), juce::Justification::centredLeft);
        }
    }

    // take in progress
    const int takeLane = recLane >= 0 ? recLane : (st == StudioEngine::State::reamping ? reampingTrack : -1);
    if (takeLane >= 0)
    {
        const auto r = laneRect (takeLane);
        const float x0 = timeToX (takeStartSeconds), x1 = timeToX ((double) engine.getPosition() / rate);
        const auto c = recLane >= 0 ? recordRed : colours::accent;
        g.setColour (c.withAlpha (0.45f));
        g.fillRoundedRectangle (x0, (float) r.getY() + 2.0f, juce::jmax (2.0f, x1 - x0), (float) r.getHeight() - 4.0f, 3.0f);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (recLane >= 0 ? "REC" : "REAMP", (int) x0 + 6, r.getY() + 3, 60, 14, juce::Justification::left);
    }

    // playhead
    {
        const float x = timeToX ((double) engine.getPosition() / rate);
        g.setColour (st == StudioEngine::State::stopped ? colours::accent.withAlpha (0.7f) : colours::accent);
        g.drawLine (x, (float) refArea.getY(), x, (float) laneRect (juce::jmin (studio::maxTracks, nTracks + 1) - 1).getBottom(), 1.6f);
        juce::Path tri;
        tri.addTriangle (x - 5.0f, (float) lanes.getY(), x + 5.0f, (float) lanes.getY(), x, (float) lanes.getY() + 7.0f);
        g.fillPath (tri);
    }
}

void StudioView::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    const bool inWaves = p.x >= lanesArea().getX() && (tracksArea.contains (p) || refArea.contains (p));
    if (! inWaves)
        return;
    project.seek ((juce::int64) (xToTime ((float) p.x) * engine.getSampleRate()));
    updateTransportButtons();
    repaint();
}

void StudioView::timerCallback()
{
    project.poll();
    if (needsRefresh)
        refresh();
    const auto st = engine.getState();
    const double rate = engine.getSampleRate();
    timeLabel.setText (formatTime ((double) engine.getPosition() / rate), juce::dontSendNotification);
    updateTransportButtons();
    if (st != StudioEngine::State::stopped)
    {
        lastViewSeconds = viewSeconds(); // the view only grows while running
        repaint (tracksArea.getUnion (refArea).expanded (2));
    }
}

} // namespace gsp::ui
