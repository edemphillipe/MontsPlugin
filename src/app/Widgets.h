#pragma once

#include "LookAndFeel.h"
#include "Realistic.h"

#include <functional>

namespace gsp::ui
{

// Rotary knob with a caption underneath and a value read-out.
class Knob final : public juce::Component
{
public:
    Knob (const juce::String& caption, double min, double max, double def, const juce::String& suffix)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
        slider.setRange (min, max, 0.1);
        slider.setValue (def, juce::dontSendNotification);
        slider.setDoubleClickReturnValue (true, def);
        slider.setTextValueSuffix (suffix);
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (slider);

        label.setText (caption, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        label.setColour (juce::Label::textColourId, colours::textDim);
        addAndMakeVisible (label);
    }

    ~Knob() override { slider.setLookAndFeel (nullptr); }

    // Draw the knob with another look (pedal / amp panel) and print the texts in `ink`.
    void setPanelStyle (juce::LookAndFeel* look, juce::Colour ink, const juce::String& cap = "silver")
    {
        slider.setLookAndFeel (look);
        slider.getProperties().set ("cap", cap);
        label.setColour (juce::Label::textColourId, ink);
        slider.setColour (juce::Slider::textBoxTextColourId, ink.withAlpha (0.85f));
    }

    void resized() override
    {
        auto r = getLocalBounds();
        label.setBounds (r.removeFromTop (16));
        slider.setBounds (r);
    }

    juce::Slider slider;
    juce::Label label;
};

// Vertical peak meter with decay.
class Meter final : public juce::Component
{
public:
    void setPeak (float linear)
    {
        const float db = juce::Decibels::gainToDecibels (linear, -60.0f);
        const float target = juce::jmap (db, -60.0f, 0.0f, 0.0f, 1.0f);
        level = juce::jmax (target, level * 0.85f);
        if (linear >= 0.999f)
            clipHold = 30;
        else if (clipHold > 0)
            --clipHold;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (colours::background);
        g.fillRoundedRectangle (r, 3.0f);
        auto clip = r.removeFromTop (6.0f);
        g.setColour (clipHold > 0 ? colours::meterHot : colours::ledOff);
        g.fillRoundedRectangle (clip.reduced (1.0f), 2.0f);
        r.removeFromTop (2.0f);
        const auto filled = r.removeFromBottom (r.getHeight() * juce::jlimit (0.0f, 1.0f, level));
        g.setGradientFill (juce::ColourGradient (colours::ledOn, 0, r.getBottom() + filled.getHeight(),
                                                 colours::accent, 0, r.getY(), false));
        g.fillRoundedRectangle (filled.reduced (1.0f, 0.0f), 2.0f);
    }

private:
    float level = 0.0f;
    int clipHold = 0;
};

// On/off switch drawn as an LED.
class LedButton final : public juce::Button
{
public:
    LedButton() : juce::Button ("on")
    {
        setClickingTogglesState (true);
        setToggleState (true, juce::dontSendNotification);
    }

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const auto r = getLocalBounds().toFloat();
        const float rad = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f - 3.0f;
        if (jewel)
        {
            // amp pilot light: faceted red jewel
            const auto c = r.getCentre();
            if (getToggleState())
            {
                g.setGradientFill (juce::ColourGradient (juce::Colour (0x99ff3020), c.x, c.y, juce::Colour (0x00ff3020), c.x + rad * 3.0f, c.y, true));
                g.fillEllipse (c.x - rad * 3.0f, c.y - rad * 3.0f, rad * 6.0f, rad * 6.0f);
            }
            g.setColour (juce::Colour (0xffb8b8b8));
            g.fillEllipse (c.x - rad - 2.0f, c.y - rad - 2.0f, rad * 2 + 4.0f, rad * 2 + 4.0f);
            g.setGradientFill (juce::ColourGradient (getToggleState() ? juce::Colour (0xffffc0b0) : juce::Colour (0xff7a3a30), c.x - rad * 0.4f, c.y - rad * 0.5f,
                                                     getToggleState() ? juce::Colour (0xffc00810) : juce::Colour (0xff300808), c.x + rad, c.y + rad, true));
            g.fillEllipse (c.x - rad, c.y - rad, rad * 2, rad * 2);
            return;
        }
        real::drawLed (g, r.getCentre(), juce::jmin (rad, 5.0f), getToggleState(), over ? colours::ledOn.brighter (0.2f) : colours::ledOn);
    }

    bool jewel = false;
};

// Rounded panel with a title and optional bypass LED, the visual unit of the signal chain.
class ModulePanel : public juce::Component
{
public:
    explicit ModulePanel (const juce::String& titleIn, bool hasBypass = true) : title (titleIn)
    {
        if (hasBypass)
            addAndMakeVisible (bypass);
        bypass.onClick = [this] { if (onBypassChanged) onBypassChanged (bypass.getToggleState()); repaint(); };
    }

    enum class Look { Rack, AmpHead, Cabinet };
    void setLook (Look l) { look = l; bypass.jewel = (l == Look::AmpHead); repaint(); }
    void setHeadline (const juce::String& h) { headline = h; repaint(); }
    // where the AmpHead look puts its gold control panel (component coordinates)
    juce::Rectangle<int> controlPanelArea() const
    {
        auto r = getLocalBounds().reduced (14);
        return r.withTrimmedTop ((int) (r.getHeight() * 0.40f));
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        const bool on = ! bypass.isVisible() || bypass.getToggleState();
        if (look == Look::AmpHead || look == Look::Cabinet)
        {
            // tolex-covered box with piping
            real::drawEnclosure (g, r, juce::Colour (0xff201b18), 10.0f);
            g.setColour (juce::Colour (0xff8a7a5a).withAlpha (0.55f));
            g.drawRoundedRectangle (r.reduced (6.0f), 7.0f, 1.2f);
            if (look == Look::AmpHead)
            {
                auto grill = r.reduced (13.0f).removeFromTop (r.getHeight() * 0.40f - 14.0f);
                drawCloth (g, grill, juce::Colour (0xff3a2f27), juce::Colour (0xff2a211b));
                if (headline.isNotEmpty())
                {
                    g.setColour (juce::Colour (0xffefe3c0).withAlpha (on ? 1.0f : 0.5f));
                    g.setFont (juce::FontOptions (30.0f, juce::Font::bold | juce::Font::italic));
                    g.drawText (headline, grill.withTrimmedLeft (grill.getWidth() * 0.42f).reduced (10.0f, 0.0f), juce::Justification::centred);
                }
                const auto panel = controlPanelArea().toFloat();
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe2cd95), panel.getX(), panel.getY(), juce::Colour (0xffae9456), panel.getX(), panel.getBottom(), false));
                g.fillRoundedRectangle (panel, 5.0f);
                g.setColour (juce::Colours::white.withAlpha (0.5f));
                g.drawLine (panel.getX() + 4.0f, panel.getY() + 1.0f, panel.getRight() - 4.0f, panel.getY() + 1.0f, 1.0f);
                g.setColour (juce::Colours::black.withAlpha (0.4f));
                g.drawRoundedRectangle (panel, 5.0f, 1.0f);
            }
            else
            {
                drawCloth (g, r.reduced (13.0f), juce::Colour (0xff4a4036), juce::Colour (0xff332a22));
            }
            g.setColour (juce::Colour (0xffd8c9a8));
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (title, getLocalBounds().reduced (20, 0).removeFromTop (30), juce::Justification::centredLeft);
            return;
        }

        // rack unit: dark brushed metal with screws and a silkscreened title
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2e2f34), r.getX(), r.getY(), juce::Colour (0xff17181b), r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.025f));
        for (float y = r.getY() + 2.0f; y < r.getBottom(); y += 2.0f)
            g.drawHorizontalLine ((int) y, r.getX() + 2.0f, r.getRight() - 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
        g.setColour (juce::Colours::black);
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
        for (auto p : { r.getTopLeft() + juce::Point<float> (7, 7), r.getTopRight() + juce::Point<float> (-7, 7),
                        r.getBottomLeft() + juce::Point<float> (7, -7), r.getBottomRight() + juce::Point<float> (-7, -7) })
            real::drawScrew (g, p, 2.6f);
        g.setColour (on ? colours::accent : colours::textDim);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (title, getLocalBounds().reduced (16, 0).removeFromTop (30), juce::Justification::centredLeft);
    }

    static void drawCloth (juce::Graphics& g, juce::Rectangle<float> a, juce::Colour light, juce::Colour dark)
    {
        g.setColour (dark);
        g.fillRoundedRectangle (a, 5.0f);
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (a.toNearestInt());
        g.setColour (light);
        for (float d = -a.getHeight(); d < a.getWidth(); d += 4.0f)
        {
            g.drawLine (a.getX() + d, a.getBottom(), a.getX() + d + a.getHeight(), a.getY(), 1.0f);
        }
        g.setColour (light.darker (0.3f).withAlpha (0.6f));
        for (float d = 0.0f; d < a.getWidth() + a.getHeight(); d += 4.0f)
            g.drawLine (a.getX() + d, a.getBottom(), a.getX() + d - a.getHeight(), a.getY(), 0.8f);
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.0f), a.getCentreX(), a.getCentreY(),
                                                 juce::Colours::black.withAlpha (0.55f), a.getX(), a.getY(), true));
        g.fillRoundedRectangle (a, 5.0f);
    }

    void resized() override
    {
        if (look == Look::AmpHead)
        {
            const auto p = controlPanelArea();
            bypass.setBounds (p.getX() + 8, p.getCentreY() - 13, 26, 26);
        }
        else
        {
            bypass.setBounds (getWidth() - 30, 6, 18, 18);
        }
        layout (getLocalBounds().reduced (10).withTrimmedTop (22));
    }

    virtual void layout (juce::Rectangle<int>) {}

    LedButton bypass;
    std::function<void (bool)> onBypassChanged;

private:
    juce::String title, headline;
    Look look = Look::Rack;
};

} // namespace gsp::ui
