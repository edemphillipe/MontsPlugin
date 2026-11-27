#pragma once

#include "LookAndFeel.h"

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
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        const float d = juce::jmin (r.getWidth(), r.getHeight());
        auto led = juce::Rectangle<float> (d, d).withCentre (r.getCentre());
        const auto c = getToggleState() ? colours::ledOn : colours::ledOff;
        if (getToggleState())
        {
            g.setColour (c.withAlpha (0.3f));
            g.fillEllipse (led.expanded (3.0f));
        }
        g.setColour (over ? c.brighter (0.2f) : c);
        g.fillEllipse (led);
    }
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

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (colours::panel);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (bypass.isVisible() && ! bypass.getToggleState() ? colours::panelEdge : colours::accent.withAlpha (0.5f));
        g.drawRoundedRectangle (r, 8.0f, 1.2f);
        g.setColour (colours::text);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (title, getLocalBounds().reduced (12, 0).removeFromTop (28), juce::Justification::centredLeft);
    }

    void resized() override
    {
        bypass.setBounds (getWidth() - 30, 6, 18, 18);
        layout (getLocalBounds().reduced (10).withTrimmedTop (22));
    }

    virtual void layout (juce::Rectangle<int>) {}

    LedButton bypass;
    std::function<void (bool)> onBypassChanged;

private:
    juce::String title;
};

} // namespace gsp::ui
