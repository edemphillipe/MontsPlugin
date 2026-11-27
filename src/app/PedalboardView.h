#pragma once

// Visual pedalboard in the realistic style: painted metal enclosures with knurled knobs,
// chrome footswitches, LEDs, side jacks and patch cables between pedals, plus a small amp
// head for the AMP slot.
//
// Each pedal shows its main knobs on the face. Clicking the pedal (without dragging) opens
// all of its controls; dragging the body changes the order.

#include "LookAndFeel.h"
#include "Realistic.h"
#include "Widgets.h"

#include "engine/Pedalboard.h"

#include <functional>

namespace gsp::ui
{

struct PedalStyle
{
    juce::String title, subtitle;
    juce::Colour body, ink;
    std::vector<std::string> face;   // parameter ids shown on the pedal itself
    juce::String cap = "black";      // knob caps: "black", "cream", "silver"
    int width = 180;
};

PedalStyle styleFor (const std::string& typeId);

// All controls of one pedal (shown in a pop-up when the pedal is clicked).
class PedalEditor final : public juce::Component
{
public:
    PedalEditor (fx::Effect& effect, const PedalStyle& style, std::function<void()> onChange);
    ~PedalEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Control
    {
        int param;
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::ComboBox> box;
        std::unique_ptr<juce::Label> label;
    };
    fx::Effect& fx;
    PedalStyle style;
    std::vector<Control> controls;
    real::PedalKnobLook knobLook;
};

class PedalComponent final : public juce::Component, public juce::SettableTooltipClient
{
public:
    PedalComponent (fx::Effect& effect, int index);
    ~PedalComponent() override;

    void refreshFromEffect();     // after a preset load or an edit in the pop-up
    int getPedalIndex() const noexcept { return pedalIndex; }
    int preferredWidth() const noexcept { return style.width; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    // jack positions (component coordinates) for the patch cables
    juce::Point<float> inputJack() const;
    juce::Point<float> outputJack() const;

    std::function<void (PedalComponent&, const juce::MouseEvent&)> onDragMove, onDragEnd;
    std::function<void()> onChanged;

    static constexpr int sideMargin = 8; // room for the jacks

private:
    class Footswitch final : public juce::Button
    {
    public:
        Footswitch() : juce::Button ("footswitch") { setClickingTogglesState (true); }
        void paintButton (juce::Graphics& g, bool, bool down) override
        {
            real::drawFootswitch (g, getLocalBounds().toFloat().reduced (2.0f), down);
        }
    };

    struct FaceKnob
    {
        int param;
        std::unique_ptr<juce::Slider> slider;
        juce::String label;
    };

    juce::Rectangle<float> body() const;
    juce::String modeText() const;
    void openEditor();

    fx::Effect& fx;
    int pedalIndex;
    PedalStyle style;
    std::vector<FaceKnob> knobs;
    Footswitch footswitch;
    real::PedalKnobLook knobLook;
    bool dragging = false;
};

class AmpTile final : public juce::Component, public juce::SettableTooltipClient
{
public:
    AmpTile();
    void setCaptureName (const juce::String& n) { captureName = n; repaint(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    juce::Point<float> inputJack() const { return { 4.0f, getHeight() * 0.3f }; }
    juce::Point<float> outputJack() const { return { getWidth() - 4.0f, getHeight() * 0.3f }; }
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
    std::pair<juce::Point<float>, juce::Point<float>> jacksFor (int slot); // input, output in strip coords

    Pedalboard& board;
    std::vector<std::unique_ptr<PedalComponent>> pedals;
    AmpTile amp;
    Strip strip { *this };
    juce::Viewport viewport;
    juce::Label title, hint;
    int dropMarkerX = -1;
};

} // namespace gsp::ui
