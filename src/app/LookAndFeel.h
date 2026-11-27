#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace gsp::ui
{

// UI strings are written in Portuguese with accents; source files are UTF-8.
inline juce::String utf8 (const char* text) { return juce::String::fromUTF8 (text); }

namespace colours
{
    const juce::Colour background { 0xff15161a };
    const juce::Colour panel      { 0xff202228 };
    const juce::Colour panelEdge  { 0xff33363f };
    const juce::Colour text       { 0xffe8e6e1 };
    const juce::Colour textDim    { 0xff8d8f97 };
    const juce::Colour accent     { 0xffe0a03a }; // amber
    const juce::Colour ledOn      { 0xff58d26b };
    const juce::Colour ledOff     { 0xff3a3d45 };
    const juce::Colour meterHot   { 0xffe05a3a };
}

class StudioLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    StudioLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, colours::background);
        setColour (juce::Label::textColourId, colours::text);
        setColour (juce::TextButton::buttonColourId, colours::panelEdge);
        setColour (juce::TextButton::textColourOffId, colours::text);
        setColour (juce::TextButton::buttonOnColourId, colours::accent);
        setColour (juce::TextButton::textColourOnId, colours::background);
        setColour (juce::ComboBox::backgroundColourId, colours::panelEdge);
        setColour (juce::ComboBox::outlineColourId, colours::panelEdge);
        setColour (juce::ComboBox::textColourId, colours::text);
        setColour (juce::PopupMenu::backgroundColourId, colours::panel);
        setColour (juce::ListBox::backgroundColourId, colours::panel);
        setColour (juce::TextEditor::backgroundColourId, colours::background);
        setColour (juce::TextEditor::outlineColourId, colours::panelEdge);
        setColour (juce::TextEditor::textColourId, colours::text);
        setColour (juce::ToggleButton::textColourId, colours::text);
        setColour (juce::ToggleButton::tickColourId, colours::accent);
        setColour (juce::Slider::textBoxTextColourId, colours::textDim);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::ScrollBar::thumbColourId, colours::panelEdge);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider&) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (6.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float angle = startAngle + pos * (endAngle - startAngle);

        // track
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
        g.setColour (colours::ledOff);
        g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path value;
        value.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, angle, true);
        g.setColour (colours::accent);
        g.strokePath (value, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // knob body
        const float body = radius * 0.72f;
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3b3e47), centre.x, centre.y - body,
                                                 juce::Colour (0xff1b1c21), centre.x, centre.y + body, false));
        g.fillEllipse (centre.x - body, centre.y - body, body * 2, body * 2);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawEllipse (centre.x - body, centre.y - body, body * 2, body * 2, 1.0f);

        // pointer
        juce::Path pointer;
        pointer.addRoundedRectangle (-1.5f, -body + 3.0f, 3.0f, body * 0.5f, 1.5f);
        g.setColour (colours::text);
        g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
    }
};

} // namespace gsp::ui
