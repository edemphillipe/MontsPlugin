#include "PedalboardView.h"

namespace gsp::ui
{

PedalStyle styleFor (const std::string& t)
{
    if (t == "compressor") return { "SQUEEZE", "compressor", juce::Colour (0xff2f6f73), juce::Colours::white };
    if (t == "boost")      return { "LIFT", "boost", juce::Colour (0xffb8bcc4), juce::Colour (0xff1b1c21) };
    if (t == "overdrive")  return { "SUNDRIVE", "overdrive", juce::Colour (0xffd6a72c), juce::Colour (0xff1b1c21) };
    if (t == "distortion") return { "CRUNCH", "distortion", juce::Colour (0xffc2452d), juce::Colours::white };
    if (t == "fuzz")       return { "BUZZ", "fuzz", juce::Colour (0xff6b3fa0), juce::Colours::white };
    if (t == "chorus")     return { "CHORUS 2", "chorus", juce::Colour (0xff4f9fd8), juce::Colours::white };
    if (t == "flanger")    return { "JETSTREAM", "flanger", juce::Colour (0xff7a8a99), juce::Colours::white };
    if (t == "phaser")     return { "PHASE 8", "phaser", juce::Colour (0xffe07a2e), juce::Colour (0xff1b1c21) };
    if (t == "tremolo")    return { "PULSO", "tremolo", juce::Colour (0xff3a7d44), juce::Colours::white };
    if (t == "delay")      return { "ECHOES", "delay", juce::Colour (0xff27303d), juce::Colour (0xffe8e6e1) };
    if (t == "reverb")     return { "NIMBUS", "reverb", juce::Colour (0xff1f3a5c), juce::Colour (0xffe8e6e1) };
    return { "PEDAL", "", colours::panelEdge, colours::text };
}

// =============================================================== PedalComponent
PedalComponent::PedalComponent (fx::Effect& effect, int index)
    : fx (effect), pedalIndex (index), style (styleFor (effect.getTypeId()))
{
    const auto& params = fx.getParams();
    for (int i = 0; i < (int) params.size(); ++i)
    {
        const auto& p = params[(size_t) i];
        if (! p.choices.empty())
        {
            auto box = std::make_unique<juce::ComboBox>();
            for (int c = 0; c < (int) p.choices.size(); ++c)
                box->addItem (utf8 (p.choices[(size_t) c].c_str()), c + 1);
            box->setSelectedId (fx.choice (i) + 1, juce::dontSendNotification);
            box->setTooltip (p.name);
            box->onChange = [this, i, b = box.get()] { fx.set (i, (float) (b->getSelectedId() - 1)); if (onChanged) onChanged(); };
            box->setColour (juce::ComboBox::backgroundColourId, juce::Colours::black.withAlpha (0.35f));
            box->setColour (juce::ComboBox::textColourId, style.ink);
            addAndMakeVisible (*box);
            selectors.emplace_back (i, std::move (box));
            continue;
        }

        auto k = std::make_unique<SmallKnob>();
        k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k->slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        k->slider.setRange (p.min, p.max, 0.0);
        if (p.logarithmic)
            k->slider.setSkewFactorFromMidPoint (std::sqrt (p.min * p.max));
        k->slider.setValue (fx.get (i), juce::dontSendNotification);
        k->slider.setDoubleClickReturnValue (true, p.def);
        k->slider.setPopupDisplayEnabled (true, true, this);
        k->slider.setTextValueSuffix (p.unit.empty() ? "" : " " + p.unit);
        k->slider.setNumDecimalPlacesToDisplay (p.max - p.min > 20.0f ? 0 : 2);
        k->slider.onValueChange = [this, i, s = &k->slider] { fx.set (i, (float) s->getValue()); };
        k->slider.onDragEnd = [this] { if (onChanged) onChanged(); };
        k->label.setText (p.name, juce::dontSendNotification);
        k->label.setJustificationType (juce::Justification::centred);
        k->label.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        k->label.setColour (juce::Label::textColourId, style.ink.withAlpha (0.85f));
        k->label.setInterceptsMouseClicks (false, false);
        k->addAndMakeVisible (k->slider);
        k->addAndMakeVisible (k->label);
        addAndMakeVisible (*k);
        knobParams.push_back (i);
        knobs.push_back (std::move (k));
    }

    footswitch.setClickingTogglesState (true);
    footswitch.setToggleState (fx.enabled.load(), juce::dontSendNotification);
    footswitch.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2b2d33));
    footswitch.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3b3e47));
    footswitch.onClick = [this] { fx.enabled = footswitch.getToggleState(); repaint(); if (onChanged) onChanged(); };
    addAndMakeVisible (footswitch);
}

int PedalComponent::preferredWidth() const noexcept
{
    const int n = (int) knobs.size();
    return n <= 2 ? 130 : (n <= 4 ? 150 : 230);
}

void PedalComponent::refreshFromEffect()
{
    for (size_t k = 0; k < knobs.size(); ++k)
        knobs[k]->slider.setValue (fx.get (knobParams[k]), juce::dontSendNotification);
    for (auto& [param, box] : selectors)
        box->setSelectedId (fx.choice (param) + 1, juce::dontSendNotification);
    footswitch.setToggleState (fx.enabled.load(), juce::dontSendNotification);
    repaint();
}

void PedalComponent::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (3.0f);
    if (dragging)
        g.setOpacity (0.6f);

    // enclosure with a subtle bevel
    g.setGradientFill (juce::ColourGradient (style.body.brighter (0.15f), r.getX(), r.getY(),
                                             style.body.darker (0.25f), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (r, 10.0f, 1.5f);

    // screws
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    for (auto p : { r.getTopLeft() + juce::Point<float> (9, 9), r.getTopRight() + juce::Point<float> (-9, 9),
                    r.getBottomLeft() + juce::Point<float> (9, -9), r.getBottomRight() + juce::Point<float> (-9, -9) })
        g.fillEllipse (p.x - 3, p.y - 3, 6, 6);

    // LED
    const bool on = fx.enabled.load();
    const auto led = juce::Rectangle<float> (10, 10).withCentre ({ r.getCentreX(), r.getY() + 14 });
    if (on)
    {
        g.setColour (juce::Colour (0xffff4a3a).withAlpha (0.35f));
        g.fillEllipse (led.expanded (4));
    }
    g.setColour (on ? juce::Colour (0xffff4a3a) : juce::Colour (0xff3a1a18));
    g.fillEllipse (led);

    // name plate (lower third, above the footswitch)
    auto plate = r.withTrimmedBottom (64).removeFromBottom (40);
    g.setColour (style.ink);
    g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    g.drawText (style.title, plate.removeFromTop (22), juce::Justification::centred);
    g.setColour (style.ink.withAlpha (0.65f));
    g.setFont (juce::FontOptions (10.5f, juce::Font::italic));
    g.drawText (style.subtitle, plate, juce::Justification::centredTop);
}

void PedalComponent::resized()
{
    auto r = getLocalBounds().reduced (12);
    r.removeFromTop (20); // LED
    for (auto& [param, box] : selectors)
    {
        juce::ignoreUnused (param);
        box->setBounds (r.removeFromTop (22).reduced (2, 0));
        r.removeFromTop (3);
    }

    footswitch.setBounds (juce::Rectangle<int> (44, 44).withCentre ({ getWidth() / 2, getHeight() - 38 }));
    auto knobArea = r.withTrimmedBottom (110);
    const int cols = knobs.size() <= 2 ? (int) knobs.size() : (knobs.size() <= 4 ? 2 : 3);
    const int rows = cols == 0 ? 0 : ((int) knobs.size() + cols - 1) / cols;
    if (rows == 0)
        return;
    const int cellW = knobArea.getWidth() / cols;
    const int cellH = juce::jmin (70, knobArea.getHeight() / rows);
    for (size_t k = 0; k < knobs.size(); ++k)
    {
        const int row = (int) k / cols, col = (int) k % cols;
        auto cell = juce::Rectangle<int> (knobArea.getX() + col * cellW, knobArea.getY() + row * cellH, cellW, cellH);
        knobs[k]->setBounds (cell);
        auto inner = knobs[k]->getLocalBounds();
        knobs[k]->label.setBounds (inner.removeFromBottom (13));
        knobs[k]->slider.setBounds (inner.withSizeKeepingCentre (juce::jmin (inner.getWidth(), inner.getHeight() + 6), inner.getHeight()));
    }
}

void PedalComponent::mouseDown (const juce::MouseEvent&) { dragging = false; }
void PedalComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging && e.getDistanceFromDragStart() > 6)
    {
        dragging = true;
        repaint();
    }
    if (dragging && onDragMove)
        onDragMove (*this, e);
}
void PedalComponent::mouseUp (const juce::MouseEvent& e)
{
    if (dragging)
    {
        dragging = false;
        repaint();
        if (onDragEnd)
            onDragEnd (*this, e);
    }
}

// =============================================================== AmpTile
AmpTile::AmpTile() { setMouseCursor (juce::MouseCursor::DraggingHandCursor); }

void AmpTile::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (3.0f);
    if (dragging) g.setOpacity (0.6f);
    g.setColour (colours::panel);
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (colours::accent);
    g.drawRoundedRectangle (r, 10.0f, 2.0f);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("AMP", r.removeFromTop (r.getHeight() * 0.45f), juce::Justification::centredBottom);
    g.setColour (colours::textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawFittedText (captureName.isEmpty() ? utf8 ("captura + gabinete + EQ") : captureName,
                      r.reduced (8, 6).toNearestInt(), juce::Justification::centredTop, 3);
}
void AmpTile::mouseDown (const juce::MouseEvent&) { dragging = false; }
void AmpTile::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging && e.getDistanceFromDragStart() > 6) { dragging = true; repaint(); }
    if (dragging && onDragMove) onDragMove (*this, e);
}
void AmpTile::mouseUp (const juce::MouseEvent& e)
{
    if (dragging) { dragging = false; repaint(); if (onDragEnd) onDragEnd (*this, e); }
}

// =============================================================== PedalboardView
PedalboardView::PedalboardView (Pedalboard& b) : board (b)
{
    title.setText ("PEDALEIRA", juce::dontSendNotification);
    title.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    addAndMakeVisible (title);
    hint.setText (utf8 ("Arraste um pedal para mudar a ordem. Clique no botão de pé para ligar/desligar. Duplo clique no knob volta ao padrão."),
                  juce::dontSendNotification);
    hint.setColour (juce::Label::textColourId, colours::textDim);
    hint.setFont (juce::FontOptions (11.0f));
    addAndMakeVisible (hint);

    viewport.setViewedComponent (&strip, false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);

    for (int i = 0; i < board.size(); ++i)
    {
        auto p = std::make_unique<PedalComponent> (board.pedal (i), i);
        p->onDragMove = [this] (PedalComponent& c, const juce::MouseEvent& e) { dragMoved (c, e); };
        p->onDragEnd = [this] (PedalComponent& c, const juce::MouseEvent& e) { dragEnded (c.getPedalIndex(), c, e); };
        p->onChanged = [this] { if (onChanged) onChanged(); };
        strip.addAndMakeVisible (*p);
        pedals.push_back (std::move (p));
    }
    amp.onDragMove = [this] (AmpTile& c, const juce::MouseEvent& e) { dragMoved (c, e); };
    amp.onDragEnd = [this] (AmpTile& c, const juce::MouseEvent& e) { dragEnded (Pedalboard::ampSlot, c, e); };
    strip.addAndMakeVisible (amp);
}

void PedalboardView::rebuild()
{
    for (auto& p : pedals)
        p->refreshFromEffect();
    layoutStrip();
}

juce::Component* PedalboardView::componentForSlot (int slot)
{
    if (slot == Pedalboard::ampSlot)
        return &amp;
    return pedals[(size_t) slot].get();
}

void PedalboardView::layoutStrip()
{
    const int h = strip.getHeight();
    int x = 8;
    for (int slot : board.getOrder())
    {
        auto* c = componentForSlot (slot);
        const int w = slot == Pedalboard::ampSlot ? 120 : pedals[(size_t) slot]->preferredWidth();
        c->setBounds (x, 4, w, h - 8);
        x += w + 10;
    }
    if (strip.getWidth() != x)
        strip.setSize (x, h);
}

int PedalboardView::dropPositionFor (int xInStrip) const
{
    int position = 0;
    for (int slot : board.getOrder())
    {
        auto* c = slot == Pedalboard::ampSlot ? static_cast<const juce::Component*> (&amp) : pedals[(size_t) slot].get();
        if (xInStrip > c->getBounds().getCentreX())
            ++position;
    }
    return position;
}

void PedalboardView::dragMoved (juce::Component&, const juce::MouseEvent& e)
{
    const auto p = e.getEventRelativeTo (&strip).getPosition();
    const int position = dropPositionFor (p.x);
    const auto order = board.getOrder();
    if (position >= (int) order.size())
        dropMarkerX = strip.getWidth() - 4;
    else
        dropMarkerX = (position < (int) order.size() ? componentForSlot (order[(size_t) position])->getX() : strip.getWidth()) - 5;
    strip.repaint();
}

void PedalboardView::dragEnded (int slot, juce::Component&, const juce::MouseEvent& e)
{
    const auto p = e.getEventRelativeTo (&strip).getPosition();
    int position = dropPositionFor (p.x);
    const auto order = board.getOrder();
    const auto current = (int) std::distance (order.begin(), std::find (order.begin(), order.end(), slot));
    if (position > current)
        --position; // removing the dragged item shifts later positions left
    board.movePedal (slot, position);
    dropMarkerX = -1;
    layoutStrip();
    strip.repaint();
    if (onChanged)
        onChanged();
}

void PedalboardView::Strip::paint (juce::Graphics& g)
{
    // pedalboard rails
    g.setColour (juce::Colour (0xff0f1013));
    g.fillRect (getLocalBounds());
    g.setColour (juce::Colour (0xff24262c));
    for (int y = 30; y < getHeight(); y += 60)
        g.fillRect (0, y, getWidth(), 10);
    if (owner.dropMarkerX >= 0)
    {
        g.setColour (colours::accent);
        g.fillRoundedRectangle ((float) owner.dropMarkerX - 2.0f, 6.0f, 4.0f, (float) getHeight() - 12.0f, 2.0f);
    }
}

void PedalboardView::paint (juce::Graphics& g)
{
    g.setColour (colours::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 8.0f);
}

void PedalboardView::resized()
{
    auto r = getLocalBounds().reduced (10);
    auto top = r.removeFromTop (22);
    title.setBounds (top.removeFromLeft (120));
    hint.setBounds (top);
    r.removeFromTop (4);
    viewport.setBounds (r);
    strip.setSize (juce::jmax (strip.getWidth(), r.getWidth()), r.getHeight() - viewport.getScrollBarThickness());
    layoutStrip();
}

} // namespace gsp::ui
