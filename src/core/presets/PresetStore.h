#pragma once

// Whole-board presets ("pedaleiras"): one JSON file per preset in a folder.
// The content is produced and applied by the app; this class only stores and lists them.

#include <juce_core/juce_core.h>

namespace gsp
{

class PresetStore
{
public:
    explicit PresetStore (juce::File folder);

    juce::File getFolder() const { return folder; }
    juce::StringArray names() const;                       // sorted, without extension
    bool save (const juce::String& name, const juce::var& state, juce::String& error) const;
    juce::var load (const juce::String& name) const;       // void var if missing/invalid
    bool remove (const juce::String& name) const;

    static juce::String sanitise (const juce::String& name); // safe file name, keeps accents

private:
    juce::File fileFor (const juce::String& name) const;
    juce::File folder;
};

} // namespace gsp
