#pragma once

// Drawing helpers for the realistic look: pedal enclosures, knurled pedal knobs,
// chicken-head amp knobs, chrome footswitches, LEDs, jacks and patch cables.
// Everything is drawn with vector graphics (no bitmaps), so it scales on Retina screens.

#include "LookAndFeel.h"

#include <random>

namespace gsp::ui::real
{

// Fine speckle used for powder-coated / tolex surfaces (built once, tiled).
inline const juce::Image& speckle()
{
    static const juce::Image img = []
    {
        juce::Image im (juce::Image::ARGB, 128, 128, true);
        std::mt19937 rng (7);
        std::uniform_int_distribution<int> pos (0, 127), alpha (8, 40);
        std::bernoulli_distribution light (0.5);
        for (int i = 0; i < 2600; ++i)
            im.setPixelAt (pos (rng), pos (rng), (light (rng) ? juce::Colours::white : juce::Colours::black).withAlpha ((juce::uint8) alpha (rng)));
        return im;
    }();
    return img;
}

inline void fillSpeckle (juce::Graphics& g, const juce::Path& area, float opacity)
{
    juce::Graphics::ScopedSaveState s (g);
    g.reduceClipRegion (area);
    g.setTiledImageFill (speckle(), 0, 0, opacity);
    g.fillPath (area);
}

// Painted metal box with a bevel and a soft shadow on the floor.
inline void drawEnclosure (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour paint, float corner = 12.0f)
{
    // shadow on the floor
    for (int i = 6; i > 0; --i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.07f));
        g.fillRoundedRectangle (r.translated (0.0f, 10.0f).expanded ((float) i * 1.6f, (float) i * 0.8f), corner + (float) i);
    }
    juce::Path box;
    box.addRoundedRectangle (r, corner);
    g.setGradientFill (juce::ColourGradient (paint.brighter (0.28f), r.getX(), r.getY(),
                                             paint.darker (0.45f), r.getRight(), r.getBottom(), false));
    g.fillPath (box);
    fillSpeckle (g, box, 0.9f);
    // bevel: light top/left edge, dark bottom/right edge
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.drawRoundedRectangle (r.reduced (1.5f).withTrimmedBottom (2.0f).withTrimmedRight (2.0f), corner - 1.0f, 1.6f);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawRoundedRectangle (r, corner, 1.4f);
    // a soft sheen across the top
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.16f), r.getX(), r.getY(),
                                             juce::Colours::transparentWhite, r.getX(), r.getY() + r.getHeight() * 0.35f, false));
    g.fillPath (box);
}

inline void drawScrew (juce::Graphics& g, juce::Point<float> c, float rad = 3.2f)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd9d9d9), c.x - rad, c.y - rad, juce::Colour (0xff5a5a5a), c.x + rad, c.y + rad, false));
    g.fillEllipse (c.x - rad, c.y - rad, rad * 2, rad * 2);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawLine (c.x - rad * 0.7f, c.y + rad * 0.2f, c.x + rad * 0.7f, c.y - rad * 0.2f, 1.0f);
}

enum class KnobCap { Black, Cream, Silver };

// Knurled pedal knob: skirt with grip ridges, domed cap and a pointer line.
inline void drawPedalKnob (juce::Graphics& g, juce::Rectangle<float> area, float angle, KnobCap cap)
{
    const float rad = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
    const auto c = area.getCentre();
    // shadow
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (c.x - rad * 0.98f, c.y - rad * 0.9f + rad * 0.14f, rad * 1.96f, rad * 1.96f);
    // knurled skirt
    juce::Path skirt;
    const int ridges = 28;
    for (int i = 0; i <= ridges * 2; ++i)
    {
        const float a = juce::MathConstants<float>::twoPi * (float) i / (float) (ridges * 2);
        const float rr = (i % 2 == 0) ? rad : rad * 0.94f;
        const auto p = c + juce::Point<float> (std::sin (a), -std::cos (a)) * rr;
        if (i == 0) skirt.startNewSubPath (p); else skirt.lineTo (p);
    }
    skirt.closeSubPath();
    const auto skirtCol = cap == KnobCap::Cream ? juce::Colour (0xff1c1c1c) : juce::Colour (0xff141414);
    g.setGradientFill (juce::ColourGradient (skirtCol.brighter (0.5f), c.x, c.y - rad, skirtCol, c.x, c.y + rad, false));
    g.fillPath (skirt);
    // cap
    const float capR = rad * 0.72f;
    juce::Colour top, bottom;
    switch (cap)
    {
        case KnobCap::Cream:  top = juce::Colour (0xfffffaee); bottom = juce::Colour (0xffbfb59c); break;
        case KnobCap::Silver: top = juce::Colour (0xfff2f2f2); bottom = juce::Colour (0xff7d7d7d); break;
        case KnobCap::Black:
        default:              top = juce::Colour (0xff5a5a5a); bottom = juce::Colour (0xff101010); break;
    }
    g.setGradientFill (juce::ColourGradient (top, c.x - capR * 0.4f, c.y - capR, bottom, c.x + capR * 0.3f, c.y + capR, true));
    g.fillEllipse (c.x - capR, c.y - capR, capR * 2, capR * 2);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawEllipse (c.x - capR, c.y - capR, capR * 2, capR * 2, 0.8f);
    // pointer
    juce::Path ptr;
    ptr.addRoundedRectangle (-1.3f, -rad * 0.98f, 2.6f, rad * 0.55f, 1.2f);
    g.setColour (cap == KnobCap::Black ? juce::Colours::white : juce::Colour (0xff151515));
    g.fillPath (ptr, juce::AffineTransform::rotation (angle).translated (c));
}

// Amp-style "chicken head" pointer knob.
inline void drawChickenHead (juce::Graphics& g, juce::Rectangle<float> area, float angle)
{
    const float rad = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
    const auto c = area.getCentre();
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillEllipse (c.x - rad * 0.62f, c.y - rad * 0.5f, rad * 1.3f, rad * 1.3f);
    // round base
    const float baseR = rad * 0.58f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4a4a), c.x - baseR * 0.5f, c.y - baseR, juce::Colour (0xff0b0b0b), c.x + baseR * 0.4f, c.y + baseR, true));
    g.fillEllipse (c.x - baseR, c.y - baseR, baseR * 2, baseR * 2);
    // tapered pointer bar with a white line
    juce::Path bar;
    bar.startNewSubPath (-rad * 0.16f, rad * 0.5f);
    bar.lineTo (-rad * 0.09f, -rad * 0.98f);
    bar.quadraticTo (0.0f, -rad * 1.04f, rad * 0.09f, -rad * 0.98f);
    bar.lineTo (rad * 0.16f, rad * 0.5f);
    bar.quadraticTo (0.0f, rad * 0.62f, -rad * 0.16f, rad * 0.5f);
    const auto t = juce::AffineTransform::rotation (angle).translated (c);
    g.setColour (juce::Colour (0xff181818));
    g.fillPath (bar, t);
    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.strokePath (bar, juce::PathStrokeType (1.0f), t);
    juce::Path line;
    line.addRoundedRectangle (-0.9f, -rad * 0.92f, 1.8f, rad * 0.55f, 0.9f);
    g.setColour (juce::Colour (0xfff4efe2));
    g.fillPath (line, t);
}

// Scale marks around a knob (0..10 style).
inline void drawScale (juce::Graphics& g, juce::Point<float> c, float rad, float start, float end, juce::Colour col, int marks = 11)
{
    g.setColour (col);
    for (int i = 0; i < marks; ++i)
    {
        const float a = start + (end - start) * (float) i / (float) (marks - 1);
        const auto d = juce::Point<float> (std::sin (a), -std::cos (a));
        g.drawLine ({ c + d * rad, c + d * (rad + (i % 5 == 0 ? 5.0f : 3.0f)) }, i % 5 == 0 ? 1.6f : 1.0f);
    }
}

// Chrome stomp switch seen from above; pressed = slightly smaller and darker.
inline void drawFootswitch (juce::Graphics& g, juce::Rectangle<float> area, bool pressed)
{
    const float rad = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
    const auto c = area.getCentre();
    // hex nut
    juce::Path nut;
    for (int i = 0; i < 6; ++i)
    {
        const float a = juce::MathConstants<float>::pi / 3.0f * (float) i + 0.52f;
        const auto p = c + juce::Point<float> (std::cos (a), std::sin (a)) * rad;
        if (i == 0) nut.startNewSubPath (p); else nut.lineTo (p);
    }
    nut.closeSubPath();
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillPath (nut, juce::AffineTransform::translation (0.0f, 3.0f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe6e6e6), c.x - rad, c.y - rad, juce::Colour (0xff5c5c5c), c.x + rad, c.y + rad, false));
    g.fillPath (nut);
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.strokePath (nut, juce::PathStrokeType (0.8f));
    // plunger
    const float pr = rad * (pressed ? 0.56f : 0.62f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white, c.x - pr * 0.5f, c.y - pr * 0.7f,
                                             pressed ? juce::Colour (0xff5d5d5d) : juce::Colour (0xff8c8c8c), c.x + pr * 0.4f, c.y + pr, true));
    g.fillEllipse (c.x - pr, c.y - pr, pr * 2, pr * 2);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.drawEllipse (c.x - pr, c.y - pr, pr * 2, pr * 2, 1.0f);
}

inline void drawLed (juce::Graphics& g, juce::Point<float> c, float rad, bool on, juce::Colour colour = juce::Colour (0xffff3a1e))
{
    if (on)
    {
        g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.55f), c.x, c.y, colour.withAlpha (0.0f), c.x + rad * 4.0f, c.y, true));
        g.fillEllipse (c.x - rad * 4.0f, c.y - rad * 4.0f, rad * 8.0f, rad * 8.0f);
    }
    g.setColour (juce::Colour (0xff1a1a1a));
    g.fillEllipse (c.x - rad - 1.5f, c.y - rad - 1.5f, rad * 2 + 3.0f, rad * 2 + 3.0f);
    const auto base = on ? colour : colour.darker (2.2f).withSaturation (0.6f);
    g.setGradientFill (juce::ColourGradient (on ? base.brighter (0.9f) : base.brighter (0.3f), c.x - rad * 0.4f, c.y - rad * 0.5f, base, c.x + rad, c.y + rad, true));
    g.fillEllipse (c.x - rad, c.y - rad, rad * 2, rad * 2);
}

// 1/4" jack plug on the side of a pedal.
inline void drawJack (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff8a8a8a), r.getX(), r.getY(), juce::Colour (0xffececec), r.getX(), r.getCentreY(), false));
    g.fillRoundedRectangle (r, 2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (r, 2.0f, 0.8f);
}

// Patch cable hanging between two jacks.
inline void drawCable (juce::Graphics& g, juce::Point<float> a, juce::Point<float> b, float sag)
{
    juce::Path p;
    p.startNewSubPath (a);
    p.cubicTo (a + juce::Point<float> (std::abs (b.x - a.x) * 0.35f + 10.0f, sag), b + juce::Point<float> (-std::abs (b.x - a.x) * 0.35f - 10.0f, sag), b);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.strokePath (p, juce::PathStrokeType (8.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), juce::AffineTransform::translation (0.0f, 6.0f));
    g.setColour (juce::Colour (0xff0d0d0d));
    g.strokePath (p, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), juce::AffineTransform::translation (-0.5f, -1.8f));
}

// Dark rubber-mat floor for the pedalboard.
inline void drawFloor (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1b1816), r.getCentreX(), r.getY(),
                                             juce::Colour (0xff090807), r.getCentreX(), r.getBottom(), false));
    g.fillRect (r);
    g.setColour (juce::Colours::white.withAlpha (0.018f));
    for (float x = r.getX(); x < r.getRight(); x += 7.0f)
        g.fillRect (x, r.getY(), 1.5f, r.getHeight());
    juce::Path p;
    p.addRectangle (r);
    fillSpeckle (g, p, 0.5f);
}

// Rotary slider look for pedals (knurled knob). The cap colour is read from the slider's
// "cap" property: "cream", "silver" or anything else for black.
class PedalKnobLook final : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s) override
    {
        const auto area = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (2.0f);
        const auto capProp = s.getProperties()["cap"].toString();
        const auto cap = capProp == "cream" ? KnobCap::Cream : (capProp == "silver" ? KnobCap::Silver : KnobCap::Black);
        const float rad = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
        drawPedalKnob (g, juce::Rectangle<float> (rad * 2, rad * 2).withCentre (area.getCentre()).reduced (1.0f),
                       start + pos * (end - start), cap);
    }
};

// Rotary slider look for the amp panel: chicken-head pointer with a printed 0..10 scale.
class ChickenHeadLook final : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override
    {
        const auto area = juce::Rectangle<int> (x, y, w, h).toFloat();
        const float rad = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f - 7.0f;
        const auto c = area.getCentre();
        drawScale (g, c, rad + 1.0f, start, end, juce::Colour (0xff3a2c14));
        drawChickenHead (g, juce::Rectangle<float> (rad * 2, rad * 2).withCentre (c), start + pos * (end - start));
    }
};

} // namespace gsp::ui::real
