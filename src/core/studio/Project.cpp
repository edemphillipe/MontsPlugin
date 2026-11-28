#include "studio/Project.h"

#include <cmath>

namespace gsp::studio
{

// ================================================================ SessionInfo
juce::var SessionInfo::toVar() const
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("format", "guitar-studio-pro-session");
    root->setProperty ("version", 1);
    root->setProperty ("nextTake", nextTake);
    juce::Array<juce::var> list;
    for (const auto& t : tracks)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", t.name);
        o->setProperty ("di", t.diFile);
        o->setProperty ("wet", t.wetFile);
        o->setProperty ("diStart", t.diStartSeconds);
        o->setProperty ("wetStart", t.wetStartSeconds);
        o->setProperty ("volume", t.volumeDb);
        o->setProperty ("pan", t.pan);
        o->setProperty ("mute", t.mute);
        o->setProperty ("solo", t.solo);
        o->setProperty ("tone", t.tone);
        o->setProperty ("label", t.takeLabel);
        juce::Array<juce::var> hist;
        for (const auto& h : t.history)
        {
            auto* ho = new juce::DynamicObject();
            ho->setProperty ("di", h.diFile);
            ho->setProperty ("wet", h.wetFile);
            ho->setProperty ("diStart", h.diStartSeconds);
            ho->setProperty ("wetStart", h.wetStartSeconds);
            ho->setProperty ("tone", h.tone);
            ho->setProperty ("label", h.label);
            hist.add (juce::var (ho));
        }
        o->setProperty ("history", hist);
        list.add (juce::var (o));
    }
    root->setProperty ("tracks", list);
    auto* backing = new juce::DynamicObject();
    backing->setProperty ("file", backingFile);
    backing->setProperty ("volume", backingVolumeDb);
    backing->setProperty ("mute", backingMute);
    root->setProperty ("reference", juce::var (backing));
    auto* click = new juce::DynamicObject();
    click->setProperty ("on", clickOn);
    click->setProperty ("volume", clickVolumeDb);
    root->setProperty ("click", juce::var (click));
    return juce::var (root);
}

SessionInfo SessionInfo::fromVar (const juce::var& v)
{
    SessionInfo s;
    s.nextTake = juce::jmax (1, (int) v.getProperty ("nextTake", 1));
    if (auto* list = v.getProperty ("tracks", {}).getArray())
        for (const auto& o : *list)
        {
            if (s.tracks.size() >= (size_t) maxTracks)
                break;
            TrackInfo t;
            t.name = o.getProperty ("name", "Faixa").toString();
            t.diFile = o.getProperty ("di", {}).toString();
            t.wetFile = o.getProperty ("wet", {}).toString();
            t.diStartSeconds = (double) o.getProperty ("diStart", 0.0);
            t.wetStartSeconds = (double) o.getProperty ("wetStart", t.diStartSeconds);
            t.volumeDb = juce::jlimit (-80.0f, 12.0f, (float) (double) o.getProperty ("volume", 0.0));
            t.pan = juce::jlimit (-1.0f, 1.0f, (float) (double) o.getProperty ("pan", 0.0));
            t.mute = (bool) o.getProperty ("mute", false);
            t.solo = (bool) o.getProperty ("solo", false);
            t.tone = o.getProperty ("tone", {});
            t.takeLabel = o.getProperty ("label", {}).toString();
            if (auto* hist = o.getProperty ("history", {}).getArray())
                for (const auto& h : *hist)
                {
                    TakeInfo k;
                    k.diFile = h.getProperty ("di", {}).toString();
                    k.wetFile = h.getProperty ("wet", {}).toString();
                    k.diStartSeconds = (double) h.getProperty ("diStart", 0.0);
                    k.wetStartSeconds = (double) h.getProperty ("wetStart", k.diStartSeconds);
                    k.tone = h.getProperty ("tone", {});
                    k.label = h.getProperty ("label", {}).toString();
                    t.history.push_back (k);
                }
            s.tracks.push_back (t);
        }
    const auto backing = v.getProperty ("reference", {});
    s.backingFile = backing.getProperty ("file", {}).toString();
    s.backingVolumeDb = juce::jlimit (-80.0f, 12.0f, (float) (double) backing.getProperty ("volume", -6.0));
    s.backingMute = (bool) backing.getProperty ("mute", false);
    const auto click = v.getProperty ("click", {});
    s.clickOn = (bool) click.getProperty ("on", false);
    s.clickVolumeDb = juce::jlimit (-60.0f, 6.0f, (float) (double) click.getProperty ("volume", -12.0));
    return s;
}

// ================================================================ Project
Project::Project (StudioEngine& e, bool realtimePlayback) : engine (e), realtime (realtimePlayback)
{
    formats.registerBasicFormats();
    io.startThread();
}

Project::~Project()
{
    stop();
    engine.setGraph (nullptr);
    io.stopThread (2000);
}

void Project::close()
{
    stop();
    engine.setGraph (nullptr);
    engine.seek (0);
    graphLength = 0;
    folder = juce::File();
    info = SessionInfo();
}

bool Project::rename (const juce::String& newName, juce::String& error)
{
    if (! isOpen())
        return false;
    const auto legal = juce::File::createLegalFileName (newName.trim());
    if (legal.isEmpty())
    {
        error = juce::String::fromUTF8 ("Nome inválido.");
        return false;
    }
    const auto old = folder;
    const auto target = old.getSiblingFile (legal);
    if (target == old)
        return true;
    if (target.exists())
    {
        error = juce::String::fromUTF8 ("Já existe um projeto chamado \"") + legal + "\".";
        return false;
    }
    // reference music kept inside the project folder moves with it
    const juce::File backing (info.backingFile);
    const auto backingInside = info.backingFile.isNotEmpty() && backing.isAChildOf (old);
    const auto backingRelative = backing.getRelativePathFrom (old);
    close(); // the audio files must be closed before the folder can move (Windows)
    const bool moved = old.moveFileTo (target);
    if (! moved)
        error = juce::String::fromUTF8 ("Não foi possível renomear a pasta do projeto.");
    juce::String openErr;
    if (! open (moved ? target : old, openErr) && error.isEmpty())
        error = openErr;
    if (moved && backingInside)
    {
        info.backingFile = target.getChildFile (backingRelative).getFullPathName();
        save();
        juce::String e;
        if (rebuild (e))
            notice.clear(); // the "file not found" from opening was only the old path
    }
    return moved;
}

bool Project::moveToTrash (juce::String& error)
{
    if (! isOpen())
        return false;
    const auto old = folder;
    close();
    if (! old.moveToTrash())
    {
        error = juce::String::fromUTF8 ("Não foi possível mover o projeto para a lixeira.");
        juce::String e;
        open (old, e);
        return false;
    }
    return true;
}

bool Project::open (const juce::File& dir, juce::String& error)
{
    stop();
    if (! dir.createDirectory() || ! dir.getChildFile ("Takes").createDirectory())
    {
        error = juce::String::fromUTF8 ("Não foi possível criar a pasta do projeto: ") + dir.getFullPathName();
        return false;
    }
    SessionInfo loaded;
    const auto json = dir.getChildFile ("session.json");
    if (json.existsAsFile())
    {
        const auto v = juce::JSON::parse (json);
        if (v.getProperty ("format", {}).toString() != "guitar-studio-pro-session")
        {
            error = juce::String::fromUTF8 ("O arquivo session.json deste projeto não é válido.");
            return false;
        }
        loaded = SessionInfo::fromVar (v);
    }
    folder = dir;
    info = loaded;
    pushMixer();
    juce::String warn;
    rebuild (warn);
    engine.seek (0);
    notice = warn;
    return true;
}

void Project::save()
{
    if (isOpen())
        folder.getChildFile ("session.json").replaceWithText (juce::JSON::toString (info.toVar()));
}

void Project::pushMixerTo (StudioEngine& e) const
{
    for (int i = 0; i < maxTracks; ++i)
    {
        auto& p = e.trackParams[(size_t) i];
        if (i < (int) info.tracks.size())
        {
            const auto& t = info.tracks[(size_t) i];
            p.volumeDb = t.volumeDb;
            p.pan = t.pan;
            p.mute = t.mute;
            p.solo = t.solo;
        }
        else
        {
            p.volumeDb = 0.0f;
            p.pan = 0.0f;
            p.mute = false;
            p.solo = false;
        }
    }
    e.backingVolumeDb = info.backingVolumeDb;
    e.backingMute = info.backingMute;
    e.clickVolumeDb = info.clickVolumeDb;
}

void Project::pushMixer()
{
    pushMixerTo (engine);
    engine.clickOn = info.clickOn;
}

std::unique_ptr<FilePlayer> Project::makePlayer (const juce::File& f, juce::int64 start, bool rt, double rate, int block)
{
    if (! f.existsAsFile())
        return nullptr;
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return nullptr;
    return std::make_unique<FilePlayer> (std::move (reader), rate, block, rt ? &io : nullptr, start);
}

std::unique_ptr<StudioEngine::Graph> Project::buildGraph (bool rt, double rate, int block, int reampTrack, juce::String& error)
{
    auto g = std::make_unique<StudioEngine::Graph>();
    juce::StringArray missing;
    for (size_t i = 0; i < info.tracks.size() && i < (size_t) maxTracks; ++i)
    {
        const auto& t = info.tracks[i];
        if (t.wetFile.isEmpty())
            continue;
        g->tracks[i] = makePlayer (fileFor (t.wetFile), (juce::int64) std::llround (t.wetStartSeconds * rate), rt, rate, block);
        if (g->tracks[i] == nullptr)
            missing.add (t.name);
    }
    if (info.backingFile.isNotEmpty())
    {
        g->backing = makePlayer (juce::File (info.backingFile), 0, rt, rate, block);
        if (g->backing == nullptr)
            missing.add (juce::String::fromUTF8 ("música de referência"));
    }
    if (reampTrack >= 0 && reampTrack < (int) info.tracks.size())
    {
        const auto& t = info.tracks[(size_t) reampTrack];
        g->reampSource = makePlayer (fileFor (t.diFile), (juce::int64) std::llround (t.diStartSeconds * rate), rt, rate, block);
    }
    if (! missing.isEmpty())
        error = juce::String::fromUTF8 ("Arquivo não encontrado ou ilegível: ") + missing.joinIntoString (", ");
    return g;
}

namespace
{
juce::int64 lengthOf (const StudioEngine::Graph& g)
{
    juce::int64 len = 0;
    for (const auto& t : g.tracks)
        if (t != nullptr)
            len = juce::jmax (len, t->getEnd());
    if (len == 0 && g.backing != nullptr)
        len = g.backing->getEnd();
    return len;
}
} // namespace

bool Project::rebuild (juce::String& error)
{
    if (engine.getState() != StudioEngine::State::stopped)
        stop();
    auto g = buildGraph (realtime, engine.getSampleRate(), engine.getMaxBlock(), -1, error);
    graphLength = lengthOf (*g);
    engine.setGraph (std::move (g));
    engine.seek (engine.getPosition());
    return error.isEmpty();
}

juce::int64 Project::lengthSamples() const { return graphLength; }

juce::String Project::newTakeName (const juce::String& suffix)
{
    return "Takes/Take" + juce::String (info.nextTake).paddedLeft ('0', 3) + "_" + suffix + ".wav";
}

std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> Project::makeWriter (const juce::File& f, int channels, juce::String& error)
{
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (f);
    if (static_cast<juce::FileOutputStream*> (stream.get())->failedToOpen())
    {
        error = juce::String::fromUTF8 ("Não foi possível criar o arquivo ") + f.getFullPathName();
        return nullptr;
    }
    juce::WavAudioFormat wav;
    const auto options = juce::AudioFormatWriterOptions{}
                             .withSampleRate (engine.getSampleRate())
                             .withNumChannels (channels)
                             .withBitsPerSample (24);
    auto writer = wav.createWriterFor (stream, options);
    if (writer == nullptr)
    {
        error = juce::String::fromUTF8 ("Não foi possível gravar WAV em ") + f.getFullPathName();
        return nullptr;
    }
    return std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (writer.release(), io,
                                                                       (int) juce::jmax (65536.0, engine.getSampleRate() * 4.0));
}

void Project::play()
{
    if (engine.getState() == StudioEngine::State::stopped)
        engine.play();
}

void Project::seek (juce::int64 pos)
{
    if (engine.getState() != StudioEngine::State::stopped)
        stop();
    engine.seek (pos);
}

bool Project::record (int track, juce::String& error)
{
    if (! isOpen())
    {
        error = "Abra ou crie um projeto antes de gravar.";
        return false;
    }
    if (engine.getState() != StudioEngine::State::stopped)
        stop();
    if (track >= (int) info.tracks.size())
        track = -1;
    if (track < 0 && (int) info.tracks.size() >= maxTracks)
    {
        error = juce::String::fromUTF8 ("O projeto já tem ") + juce::String (maxTracks) + " faixas. Escolha uma faixa para gravar por cima ou apague uma.";
        return false;
    }
    pendingDi = newTakeName ("DI");
    pendingWet = newTakeName ("som");
    auto di = makeWriter (fileFor (pendingDi), 1, error);
    auto wet = di != nullptr ? makeWriter (fileFor (pendingWet), 2, error) : nullptr;
    if (di == nullptr || wet == nullptr)
        return false;
    ++info.nextTake;
    pendingTone = toneProvider ? toneProvider() : juce::var();
    save();
    engine.record (std::move (di), std::move (wet), track);
    return true;
}

bool Project::reamp (int track, juce::String& error)
{
    if (! isOpen() || track < 0 || track >= (int) info.tracks.size())
    {
        error = "Escolha uma faixa para o reamp.";
        return false;
    }
    if (engine.getState() != StudioEngine::State::stopped)
        stop();
    const auto& t = info.tracks[(size_t) track];
    if (t.diFile.isEmpty() || ! fileFor (t.diFile).existsAsFile())
    {
        error = juce::String::fromUTF8 ("Esta faixa não tem a gravação limpa (DI), então não dá para fazer reamp.");
        return false;
    }
    auto g = buildGraph (realtime, engine.getSampleRate(), engine.getMaxBlock(), track, error);
    if (g->reampSource == nullptr)
    {
        error = juce::String::fromUTF8 ("Não foi possível ler a gravação limpa (DI) desta faixa.");
        return false;
    }
    error.clear(); // a missing file on another track does not stop a re-amp
    const auto from = juce::jmax ((juce::int64) 0, g->reampSource->getStart());
    // keep playing a little after the DI ends so delay and reverb tails are recorded
    const auto end = g->reampSource->getEnd() + (juce::int64) (engine.getSampleRate() * 3.0);
    graphLength = lengthOf (*g);
    engine.setGraph (std::move (g));
    engine.seek (from);

    pendingWet = newTakeName ("reamp");
    pendingDi.clear();
    auto wet = makeWriter (fileFor (pendingWet), 2, error);
    if (wet == nullptr)
    {
        juce::String ignored;
        rebuild (ignored);
        return false;
    }
    ++info.nextTake;
    pendingTone = toneProvider ? toneProvider() : juce::var();
    save();
    engine.reamp (std::move (wet), track, end);
    return true;
}

void Project::poll()
{
    if (engine.reachedEnd() && engine.getState() == StudioEngine::State::stopped)
        finish (engine.stop());
}

void Project::stop()
{
    finish (engine.stop());
}

void Project::finish (StudioEngine::Finished f)
{
    const bool wasTake = f.was == StudioEngine::State::recording || f.was == StudioEngine::State::reamping;
    const auto dropped = engine.droppedSamples();
    f.di.reset();  // flushes and closes the files
    f.wet.reset();
    if (! wasTake)
        return;

    const double rate = engine.getSampleRate();
    const auto recorded = f.endPosition - f.startPosition;
    if (f.was == StudioEngine::State::reamping && ! engine.reachedEnd())
    {
        // stopped before the end: keep the track's previous sound
        fileFor (pendingWet).deleteFile();
        notice = "Reamp cancelado. A faixa continua com o som anterior.";
    }
    else if (recorded <= 0)
    {
        fileFor (pendingWet).deleteFile();
        if (pendingDi.isNotEmpty())
            fileFor (pendingDi).deleteFile();
        notice = "Nada foi gravado.";
    }
    else if (f.was == StudioEngine::State::recording)
    {
        // what was played at timeline position p reaches the input `latency` samples later
        const double start = (double) (f.startPosition - latency) / rate;
        TakeInfo take { pendingDi, pendingWet, start, start, pendingTone, takeLabelNow (juce::String::fromUTF8 ("Gravação")) };
        if (f.track >= 0 && f.track < (int) info.tracks.size())
        {
            auto& t = info.tracks[(size_t) f.track];
            if (t.wetFile.isNotEmpty())
                t.history.push_back (t.current()); // the take you recorded over stays available
            t.setCurrent (take);
        }
        else
        {
            TrackInfo t;
            t.name = "Faixa " + juce::String ((int) info.tracks.size() + 1);
            t.setCurrent (take);
            info.tracks.push_back (t);
        }
        notice = "Gravado: " + juce::String ((double) recorded / rate, 1) + " s.";
    }
    else if (f.track >= 0 && f.track < (int) info.tracks.size())
    {
        auto& t = info.tracks[(size_t) f.track];
        t.history.push_back (t.current()); // the sound before the re-amp stays available
        t.wetFile = pendingWet;
        t.wetStartSeconds = (double) f.startPosition / rate; // re-amp output is aligned with the timeline
        t.tone = pendingTone;
        t.takeLabel = takeLabelNow ("Reamp");
        notice = juce::String::fromUTF8 ("Reamp concluído na ") + t.name + ".";
    }
    if (dropped > 0)
        notice << juce::String::fromUTF8 (" Atenção: ") << juce::String ((double) dropped / rate, 2)
               << juce::String::fromUTF8 (" s não couberam no disco a tempo (disco lento?).");

    pendingDi.clear();
    pendingWet.clear();
    save();
    pushMixer();
    juce::String err;
    rebuild (err);
    if (f.was == StudioEngine::State::reamping)
        engine.seek (f.startPosition);
    if (onChanged)
        onChanged();
}

juce::String Project::takeLabelNow (const juce::String& kind)
{
    return kind + " " + juce::Time::getCurrentTime().formatted ("%d/%m %H:%M");
}

bool Project::useTake (int track, int historyIndex)
{
    if (track < 0 || track >= (int) info.tracks.size())
        return false;
    auto& t = info.tracks[(size_t) track];
    if (historyIndex < 0 || historyIndex >= (int) t.history.size())
        return false;
    stop();
    const auto chosen = t.history[(size_t) historyIndex];
    t.history.erase (t.history.begin() + historyIndex);
    t.history.push_back (t.current());
    t.setCurrent (chosen);
    save();
    juce::String err;
    rebuild (err);
    if (onChanged)
        onChanged();
    return true;
}

bool Project::removeTrack (int track)
{
    if (track < 0 || track >= (int) info.tracks.size())
        return false;
    stop();
    info.tracks.erase (info.tracks.begin() + track); // the takes stay on disk
    save();
    pushMixer();
    juce::String err;
    rebuild (err);
    if (onChanged)
        onChanged();
    return true;
}

bool Project::setBacking (const juce::File& file, juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        error = juce::String::fromUTF8 ("Formato de áudio não suportado: ") + file.getFileName();
        return false;
    }
    stop();
    info.backingFile = file.getFullPathName();
    save();
    pushMixer();
    rebuild (error);
    if (onChanged)
        onChanged();
    return error.isEmpty();
}

void Project::clearBacking()
{
    stop();
    info.backingFile.clear();
    save();
    juce::String err;
    rebuild (err);
    if (onChanged)
        onChanged();
}

bool Project::exportMix (const juce::File& out, juce::String& error, float* peakOut)
{
    const double rate = engine.getSampleRate();
    constexpr int block = 4096;
    StudioEngine offline;
    offline.prepare (rate, block);
    auto g = buildGraph (false, rate, block, -1, error);
    const auto total = lengthOf (*g);
    if (total <= 0)
    {
        error = juce::String::fromUTF8 ("Não há nada gravado para exportar.");
        return false;
    }
    error.clear();
    offline.setGraph (std::move (g));
    pushMixerTo (offline);
    offline.clickOn = false;
    offline.seek (0);
    offline.play();

    out.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (out);
    if (static_cast<juce::FileOutputStream*> (stream.get())->failedToOpen())
    {
        error = juce::String::fromUTF8 ("Não foi possível criar ") + out.getFullPathName();
        return false;
    }
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (2).withBitsPerSample (24));
    if (writer == nullptr)
    {
        error = juce::String::fromUTF8 ("Não foi possível gravar WAV em ") + out.getFullPathName();
        return false;
    }

    struct Silent final : StudioEngine::Chain
    {
        void run (const float*, float* l, float* r, int n) noexcept override
        {
            juce::FloatVectorOperations::clear (l, n);
            juce::FloatVectorOperations::clear (r, n);
        }
    } silent;

    juce::AudioBuffer<float> buf (2, block);
    std::vector<float> zeros ((size_t) block, 0.0f);
    float peak = 0.0f;
    for (juce::int64 done = 0; done < total;)
    {
        const int n = (int) juce::jmin ((juce::int64) block, total - done);
        offline.process (zeros.data(), buf.getWritePointer (0), buf.getWritePointer (1), n, silent);
        peak = juce::jmax (peak, buf.getMagnitude (0, n));
        if (! writer->writeFromAudioSampleBuffer (buf, 0, n))
        {
            error = "Erro ao escrever " + out.getFullPathName();
            return false;
        }
        done += n;
    }
    if (peakOut != nullptr)
        *peakOut = peak;
    return true;
}

} // namespace gsp::studio
