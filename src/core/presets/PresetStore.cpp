#include "presets/PresetStore.h"

namespace gsp
{

PresetStore::PresetStore (juce::File f) : folder (std::move (f)) {}

juce::String PresetStore::sanitise (const juce::String& name)
{
    auto s = name.trim().removeCharacters ("\\/:*?\"<>|").replaceCharacters ("\t\r\n", "   ");
    while (s.startsWithChar ('.'))
        s = s.substring (1);
    return s.substring (0, 80).trim();
}

juce::File PresetStore::fileFor (const juce::String& name) const
{
    return folder.getChildFile (sanitise (name) + ".json");
}

juce::StringArray PresetStore::names() const
{
    juce::StringArray result;
    for (const auto& f : folder.findChildFiles (juce::File::findFiles, false, "*.json"))
        result.add (f.getFileNameWithoutExtension());
    result.sortNatural();
    return result;
}

bool PresetStore::save (const juce::String& name, const juce::var& state, juce::String& error) const
{
    const auto clean = sanitise (name);
    if (clean.isEmpty())
    {
        error = juce::String::fromUTF8 ("Dê um nome para a pedaleira.");
        return false;
    }
    if (! folder.createDirectory())
    {
        error = juce::String::fromUTF8 ("Não foi possível criar a pasta ") + folder.getFullPathName();
        return false;
    }
    auto* root = new juce::DynamicObject();
    root->setProperty ("format", "guitar-studio-pro-preset");
    root->setProperty ("version", 1);
    root->setProperty ("name", clean);
    root->setProperty ("state", state);
    if (! fileFor (clean).replaceWithText (juce::JSON::toString (juce::var (root), false)))
    {
        error = juce::String::fromUTF8 ("Não foi possível salvar ") + fileFor (clean).getFullPathName();
        return false;
    }
    return true;
}

juce::var PresetStore::load (const juce::String& name) const
{
    const auto f = fileFor (name);
    if (! f.existsAsFile())
        return {};
    const auto v = juce::JSON::parse (f);
    if (! v.isObject() || v["format"].toString() != "guitar-studio-pro-preset")
        return {};
    return v["state"];
}

bool PresetStore::remove (const juce::String& name) const
{
    return fileFor (name).deleteFile();
}

} // namespace gsp
