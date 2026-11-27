#include "CaptureBrowser.h"

namespace gsp::ui
{

CaptureBrowser::CaptureBrowser()
{
    title.setText ("BIBLIOTECA DE CAPTURAS", juce::dontSendNotification);
    title.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    addAndMakeVisible (title);

    folderButton.onClick = [this] { if (onChooseFolder) onChooseFolder(); };
    rescanButton.onClick = [this] { rescan(); };
    favouritesOnly.onClick = [this] { applyFilter(); };
    addAndMakeVisible (folderButton);
    addAndMakeVisible (rescanButton);
    addAndMakeVisible (favouritesOnly);

    search.setTextToShowWhenEmpty ("Buscar por nome, fabricante ou modelo", colours::textDim);
    search.onTextChange = [this] { applyFilter(); };
    addAndMakeVisible (search);

    list.setRowHeight (34);
    list.setColour (juce::ListBox::backgroundColourId, colours::background);
    addAndMakeVisible (list);

    status.setColour (juce::Label::textColourId, colours::textDim);
    status.setFont (juce::FontOptions (11.0f));
    addAndMakeVisible (status);
}

void CaptureBrowser::setLibraryFolder (const juce::File& folder)
{
    root = folder;
    rescan();
}

void CaptureBrowser::rescan()
{
    all.clear();

    if (root.isDirectory())
    {
        for (const auto& item : juce::RangedDirectoryIterator (root, true, "*.nam;*.json;*.aidax", juce::File::findFiles))
        {
            const auto f = item.getFile();
            Entry e;
            e.file = f;
            e.name = f.getFileNameWithoutExtension();
            e.format = f.hasFileExtension ("nam") ? "NAM" : "AIDA-X";

            juce::StringArray parts;
            for (auto dir = f.getParentDirectory(); dir != root && dir.isAChildOf (root); dir = dir.getParentDirectory())
                parts.insert (0, dir.getFileName());
            e.group = parts.joinIntoString (" / ");
            all.add (e);
        }

        std::sort (all.begin(), all.end(), [] (const Entry& a, const Entry& b)
        {
            const int g = a.group.compareNatural (b.group);
            return g != 0 ? g < 0 : a.name.compareNatural (b.name) < 0;
        });
    }

    applyFilter();
}

void CaptureBrowser::setFavourites (const juce::StringArray& paths)
{
    favourites = paths;
    applyFilter();
}

void CaptureBrowser::applyFilter()
{
    const auto query = search.getText().trim().toLowerCase();
    visible.clear();

    for (const auto& e : all)
    {
        if (favouritesOnly.getToggleState() && ! isFavourite (e.file))
            continue;
        if (query.isNotEmpty() && ! (e.name + " " + e.group + " " + e.format).toLowerCase().contains (query))
            continue;
        visible.add (e);
    }

    list.updateContent();
    list.repaint();

    if (! root.isDirectory())
        status.setText (utf8 ("Clique em Pasta... e escolha onde você guarda suas capturas .nam e AIDA-X (.json)."), juce::dontSendNotification);
    else
        status.setText (juce::String (visible.size()) + " de " + juce::String (all.size()) + " capturas  -  "
                            + root.getFullPathName(), juce::dontSendNotification);
}

void CaptureBrowser::highlight (const juce::File& file)
{
    for (int i = 0; i < visible.size(); ++i)
        if (visible.getReference (i).file == file)
        {
            list.selectRow (i);
            return;
        }
}

bool CaptureBrowser::step (int delta)
{
    if (visible.isEmpty())
        return false;
    int row = list.getSelectedRow();
    row = row < 0 ? 0 : (row + delta + visible.size()) % visible.size();
    list.selectRow (row);
    if (onLoad)
        onLoad (visible.getReference (row).file);
    return true;
}

void CaptureBrowser::paint (juce::Graphics& g)
{
    g.setColour (colours::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 8.0f);
}

void CaptureBrowser::resized()
{
    auto r = getLocalBounds().reduced (10);
    auto top = r.removeFromTop (26);
    title.setBounds (top.removeFromLeft (220));
    rescanButton.setBounds (top.removeFromRight (80));
    top.removeFromRight (6);
    folderButton.setBounds (top.removeFromRight (80));
    r.removeFromTop (6);
    auto row = r.removeFromTop (26);
    favouritesOnly.setBounds (row.removeFromRight (100));
    row.removeFromRight (6);
    search.setBounds (row);
    r.removeFromTop (6);
    status.setBounds (r.removeFromBottom (18));
    list.setBounds (r);
}

void CaptureBrowser::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (! juce::isPositiveAndBelow (row, visible.size()))
        return;
    const auto& e = visible.getReference (row);

    if (selected)
    {
        g.setColour (colours::accent.withAlpha (0.18f));
        g.fillRect (0, 0, w, h);
    }

    // favourite star
    g.setColour (isFavourite (e.file) ? colours::accent : colours::ledOff);
    g.setFont (juce::FontOptions (16.0f));
    g.drawText (juce::String::fromUTF8 ("\xe2\x98\x85"), 4, 0, 22, h, juce::Justification::centred);

    g.setColour (colours::text);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (e.name, 30, 2, w - 110, h / 2, juce::Justification::bottomLeft, true);
    g.setColour (colours::textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (e.group.isEmpty() ? juce::String ("(raiz)") : e.group, 30, h / 2, w - 110, h / 2 - 2,
                juce::Justification::topLeft, true);

    g.setColour (e.format == "NAM" ? colours::accent : colours::ledOn);
    g.drawText (e.format, w - 74, 0, 66, h, juce::Justification::centredRight);
}

void CaptureBrowser::listBoxItemClicked (int row, const juce::MouseEvent& event)
{
    if (! juce::isPositiveAndBelow (row, visible.size()) || event.x > 28)
        return;

    const auto path = visible.getReference (row).file.getFullPathName();
    if (favourites.contains (path))
        favourites.removeString (path);
    else
        favourites.add (path);

    list.repaintRow (row);
    if (onFavouritesChanged)
        onFavouritesChanged();
}

void CaptureBrowser::listBoxItemDoubleClicked (int row, const juce::MouseEvent&) { returnKeyPressed (row); }

void CaptureBrowser::returnKeyPressed (int row)
{
    if (juce::isPositiveAndBelow (row, visible.size()) && onLoad)
        onLoad (visible.getReference (row).file);
}

} // namespace gsp::ui
