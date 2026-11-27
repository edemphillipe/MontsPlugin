#pragma once

// Capture library: scans a folder (recursively) for .nam / AIDA-X .json captures.
// Organisation follows the folder tree, e.g.  Library/<Manufacturer>/<Model>/capture.nam
// Favourites are remembered across sessions.

#include "Widgets.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace gsp::ui
{

class CaptureBrowser final : public juce::Component,
                             private juce::ListBoxModel
{
public:
    struct Entry
    {
        juce::File file;
        juce::String name, group, format; // group = "Manufacturer / Model" from folders
    };

    CaptureBrowser();

    void setLibraryFolder (const juce::File& folder);
    juce::File getLibraryFolder() const { return root; }
    void rescan();

    void setFavourites (const juce::StringArray& paths);
    const juce::StringArray& getFavourites() const { return favourites; }

    // Selects the entry for `file` if present (e.g. after loading from elsewhere).
    void highlight (const juce::File& file);
    // Moves the selection by +1/-1 and loads it. Returns false if there is nothing to step to.
    bool step (int delta);

    std::function<void (const juce::File&)> onLoad;
    std::function<void()> onChooseFolder;
    std::function<void()> onFavouritesChanged;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    int getNumRows() override { return visible.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed (int row) override;

    void applyFilter();
    bool isFavourite (const juce::File& f) const { return favourites.contains (f.getFullPathName()); }

    juce::File root;
    juce::Array<Entry> all, visible;
    juce::StringArray favourites;

    juce::Label title;
    juce::TextButton folderButton { "Pasta..." }, rescanButton { "Atualizar" };
    juce::ToggleButton favouritesOnly { "Favoritos" };
    juce::TextEditor search;
    juce::ListBox list { "captures", this };
    juce::Label status;
};

} // namespace gsp::ui
