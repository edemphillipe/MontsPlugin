#pragma once

// Header tuner: an LCD that shows the note and a cents needle. Click to start tuning
// (the output is muted while tuning, like a tuner pedal); click again to play.

#include "LookAndFeel.h"

#include "tuner/PitchDetector.h"

#include <functional>

namespace gsp::ui
{

class TunerDisplay final : public juce::Component, public juce::SettableTooltipClient
{
public:
    TunerDisplay() { setTooltip (utf8 ("Clique para afinar (a saída fica muda). Clique de novo para tocar.")); }

    void setResult (const PitchResult& r)
    {
        if (r.valid)
        {
            // smooth the needle a little so it doesn't jitter
            cents = hold > 0 ? cents * 0.6 + r.cents * 0.4 : r.cents;
            note = juce::String (r.note) + juce::String (r.octave);
            hz = r.frequency;
            hold = 12;
        }
        else if (hold > 0 && --hold == 0)
        {
            note.clear();
        }
        repaint();
    }

    bool isActive() const noexcept { return active; }
    std::function<void (bool)> onActiveChanged;

    void mouseUp (const juce::MouseEvent&) override
    {
        active = ! active;
        note.clear();
        hold = 0;
        if (onActiveChanged)
            onActiveChanged (active);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colour (0xff0b140d));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (juce::Colours::black);
        g.drawRoundedRectangle (r, 5.0f, 1.5f);
        const auto green = juce::Colour (0xff6dff8a);
        if (! active)
        {
            g.setColour (green.withAlpha (0.55f));
            g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::plain));
            g.drawText (utf8 ("♪ AFINADOR"), r, juce::Justification::centred);
            return;
        }
        const bool inTune = note.isNotEmpty() && std::abs (cents) < 3.0;
        auto left = r.removeFromLeft (64.0f);
        g.setColour (note.isEmpty() ? green.withAlpha (0.3f) : (inTune ? green : juce::Colour (0xffffd060)));
        g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 20.0f, juce::Font::bold));
        g.drawText (note.isEmpty() ? "--" : note, left, juce::Justification::centred);
        // needle scale -50..+50 cents
        auto scale = r.reduced (8.0f, 8.0f);
        g.setColour (green.withAlpha (0.25f));
        for (int i = -5; i <= 5; ++i)
        {
            const float x = scale.getCentreX() + scale.getWidth() * 0.5f * (float) i / 5.0f;
            g.drawLine (x, scale.getY() + (i == 0 ? 0.0f : 4.0f), x, scale.getBottom() - (i == 0 ? 0.0f : 4.0f), i == 0 ? 2.0f : 1.0f);
        }
        if (note.isNotEmpty())
        {
            const float x = scale.getCentreX() + scale.getWidth() * 0.5f * (float) juce::jlimit (-50.0, 50.0, cents) / 50.0f;
            g.setColour (inTune ? green : juce::Colour (0xffff6040));
            g.fillRoundedRectangle (x - 2.0f, scale.getY() - 2.0f, 4.0f, scale.getHeight() + 4.0f, 2.0f);
            g.setFont (juce::FontOptions (10.0f));
            g.setColour (green.withAlpha (0.7f));
            g.drawText ((cents >= 0 ? "+" : "") + juce::String (cents, 1) + " c", scale.withTrimmedTop (scale.getHeight() - 10.0f).translated (0.0f, 8.0f),
                        juce::Justification::centredRight);
        }
    }

private:
    bool active = false;
    juce::String note;
    double cents = 0.0, hz = 0.0;
    int hold = 0;
};

} // namespace gsp::ui
