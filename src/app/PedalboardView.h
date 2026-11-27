#pragma once

// Visual pedalboard: one enclosure per pedal (original designs), plus the AMP tile.
// Drag a pedal by its body to change the order; knobs and selectors act immediately.

#include "LookAndFeel.h"
#include "Widgets.h"

#include "engine/Pedalboard.h"

#include <functional>

namespace gsp::ui
{

struct PedalStyle
{
    juce::String title, subtitle;
    juce::Colour body, ink;
};

PedalStyle styleFor (const std::string& typeId);

class PedalComponent final : public juce::Component
{
public:
    PedalComponent (fx::Effect& effect, int index);

    void refreshFromEffect();     // after a preset load
    int getPedalIndex() const noexcept { return pedalIndex; }
    int preferredWidth() const noexcept;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    std::function<void (PedalComponent&, const juce::MouseEvent&)> onDragMove, onDragEnd;
    std::function<void()> onChanged;

private:
    struct SmallKnob : juce::Component
    {
        juce::Slider slider;
        juce::Label label;
    };

    fx::Effect& fx;
    int pedalIndex;
    PedalStyle style;
    std::vector<std::unique_ptr<SmallKnob>> knobs;
    std::vector<std::pair<int, std::unique_ptr<juce::ComboBox>>> selectors;
    std::vector<int> knobParams;
    juce::TextButton footswitch;
    bool dragging = false;
};

class AmpTile final : public juce::Component
{
public:
    AmpTile();
    void setCaptureName (const juce::String& n) { captureName = n; repaint(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    std::function<void (AmpTile&, const juce::MouseEvent&)> onDragMove, onDragEnd;
private:
    juce::String captureName;
    bool dragging = false;
};

class PedalboardView final : public juce::Component
{
public:
    explicit PedalboardView (Pedalboard& board);

    void rebuild();                   // re-reads order and parameters from the board
    void setCaptureName (const juce::String& n) { amp.setCaptureName (n); }
    std::function<void()> onChanged;  // something changed (for saving)

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Strip final : public juce::Component
    {
    public:
        explicit Strip (PedalboardView& o) : owner (o) {}
        void paint (juce::Graphics&) override;
        void resized() override { owner.layoutStrip(); }
        PedalboardView& owner;
    };

    void layoutStrip();
    void dragMoved (juce::Component& c, const juce::MouseEvent& e);
    void dragEnded (int slot, juce::Component& c, const juce::MouseEvent& e);
    int dropPositionFor (int xInStrip) const;
    juce::Component* componentForSlot (int slot);

    Pedalboard& board;
    std::vector<std::unique_ptr<PedalComponent>> pedals;
    AmpTile amp;
    Strip strip { *this };
    juce::Viewport viewport;
    juce::Label title, hint;
    int dropMarkerX = -1;
};

} // namespace gsp::ui
