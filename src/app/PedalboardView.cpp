#include "PedalboardView.h"

namespace gsp::ui
{
namespace
{
constexpr float knobStart = juce::MathConstants<float>::pi * -0.78f;
constexpr float knobEnd = juce::MathConstants<float>::pi * 0.78f;
constexpr int cableGap = 34; // space between pedals, where the patch cables hang

void setupKnob (juce::Slider& s, const fx::ParamInfo& p, float value)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setRotaryParameters (knobStart + juce::MathConstants<float>::twoPi, knobEnd + juce::MathConstants<float>::twoPi, true);
    s.setRange (p.min, p.max, 0.0);
    if (p.logarithmic)
        s.setSkewFactorFromMidPoint (std::sqrt (p.min * p.max));
    s.setValue (value, juce::dontSendNotification);
    s.setDoubleClickReturnValue (true, p.def);
    s.setTextValueSuffix (p.unit.empty() ? "" : " " + p.unit);
    s.setNumDecimalPlacesToDisplay (p.max - p.min > 20.0f ? 0 : 2);
    s.setMouseDragSensitivity (180);
}
} // namespace

PedalStyle styleFor (const std::string& t)
{
    PedalStyle s;
    auto set = [&] (const char* title, const char* sub, juce::uint32 body, juce::uint32 ink, std::vector<std::string> face, const char* cap, int width = 180)
    {
        s.title = title; s.subtitle = sub; s.body = juce::Colour (body); s.ink = juce::Colour (ink);
        s.face = std::move (face); s.cap = cap; s.width = width;
    };
    if (t == "compressor")      set ("SQUEEZE", "COMPRESSOR", 0xff2f6f73, 0xffffffff, { "sustain", "attack", "level" }, "silver");
    else if (t == "boost")      set ("LIFT", "BOOST", 0xffb8bcc4, 0xff1b1c21, { "gain", "bright" }, "black", 150);
    else if (t == "overdrive")  set ("SUNDRIVE", "OVERDRIVE", 0xffd9a52a, 0xff1d1606, { "drive", "tone", "level" }, "cream");
    else if (t == "distortion") set ("CRUNCH", "DISTORTION", 0xffc2452d, 0xffffffff, { "gain", "bass", "middle", "treble", "level" }, "black", 270);
    else if (t == "fuzz")       set ("BUZZ", "FUZZ", 0xff6b3fa0, 0xffffffff, { "fuzz", "tone", "level" }, "silver");
    else if (t == "chorus")     set ("CHORUS 2", "CHORUS", 0xff3d93d0, 0xffffffff, { "rate", "depth", "mix" }, "cream");
    else if (t == "flanger")    set ("JETSTREAM", "FLANGER", 0xff7a8a99, 0xffffffff, { "rate", "depth", "regen" }, "black");
    else if (t == "phaser")     set ("PHASE 8", "PHASER", 0xffe07a2e, 0xff1b1c21, { "rate", "depth", "feedback" }, "black");
    else if (t == "tremolo")    set ("PULSO", "TREMOLO", 0xff3a7d44, 0xffffffff, { "rate", "depth", "shape" }, "cream");
    else if (t == "delay")      set ("ECHOES", "DELAY", 0xff2b3440, 0xffe8e6e1, { "time", "feedback", "mix" }, "silver");
    else if (t == "dualdelay")  set ("GEMINI", "DUAL DELAY", 0xff4a3866, 0xffefe6ff, { "timeL", "feedbackL", "timeR", "feedbackR" }, "black", 250);
    else if (t == "reverb")     set ("NIMBUS", "REVERB", 0xff1f4470, 0xffe8f0ff, { "decay", "mix", "tone" }, "silver");
    else                        set ("PEDAL", "", 0xff444444, 0xffffffff, {}, "black");
    return s;
}

// =============================================================== PedalEditor
PedalEditor::PedalEditor (fx::Effect& effect, const PedalStyle& st, std::function<void()> onChange)
    : fx (effect), style (st)
{
    const auto& params = fx.getParams();
    for (int i = 0; i < (int) params.size(); ++i)
    {
        const auto& p = params[(size_t) i];
        Control c { i, nullptr, nullptr, std::make_unique<juce::Label>() };
        c.label->setText (p.name, juce::dontSendNotification);
        c.label->setJustificationType (juce::Justification::centred);
        c.label->setFont (juce::FontOptions (11.0f, juce::Font::bold));
        c.label->setColour (juce::Label::textColourId, style.ink.withAlpha (0.9f));
        addAndMakeVisible (*c.label);
        if (! p.choices.empty())
        {
            c.box = std::make_unique<juce::ComboBox>();
            for (int k = 0; k < (int) p.choices.size(); ++k)
                c.box->addItem (utf8 (p.choices[(size_t) k].c_str()), k + 1);
            c.box->setSelectedId (fx.choice (i) + 1, juce::dontSendNotification);
            c.box->onChange = [this, i, b = c.box.get(), onChange] { fx.setFromUser (i, (float) (b->getSelectedId() - 1)); refreshControls(); onChange(); };
            addAndMakeVisible (*c.box);
        }
        else
        {
            c.slider = std::make_unique<juce::Slider>();
            setupKnob (*c.slider, p, fx.get (i));
            c.slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
            c.slider->setColour (juce::Slider::textBoxTextColourId, style.ink);
            c.slider->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            c.slider->getProperties().set ("cap", style.cap);
            c.slider->setLookAndFeel (&knobLook);
            c.slider->onValueChange = [this, i, s = c.slider.get(), onChange] { fx.setFromUser (i, (float) s->getValue()); refreshControls(); onChange(); };
            addAndMakeVisible (*c.slider);
        }
        controls.push_back (std::move (c));
    }
    int boxes = 0, knobCount = 0;
    for (auto& c : controls) (c.box ? boxes : knobCount)++;
    const int cols = juce::jmin (5, juce::jmax (3, knobCount));
    const int rows = (knobCount + cols - 1) / cols;
    setSize (cols * 92 + 32, 70 + ((boxes + 1) / 2) * 44 + rows * 112 + 28);
}

void PedalEditor::refreshControls()
{
    for (auto& c : controls)
    {
        if (c.slider != nullptr)
            c.slider->setValue (fx.get (c.param), juce::dontSendNotification);
        if (c.box != nullptr)
            c.box->setSelectedId (fx.choice (c.param) + 1, juce::dontSendNotification);
    }
}

PedalEditor::~PedalEditor()
{
    for (auto& c : controls)
        if (c.slider) c.slider->setLookAndFeel (nullptr);
}

void PedalEditor::paint (juce::Graphics& g)
{
    real::drawEnclosure (g, getLocalBounds().toFloat().reduced (6.0f).withTrimmedBottom (8.0f), style.body, 10.0f);
    g.setColour (style.ink);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText (style.title, getLocalBounds().removeFromTop (44).withTrimmedTop (12), juce::Justification::centred);
    g.setColour (style.ink.withAlpha (0.7f));
    g.setFont (juce::FontOptions (10.0f));
    g.drawText (style.subtitle + utf8 ("  ·  duplo clique num knob volta ao padrão"), getLocalBounds().removeFromTop (62).withTrimmedTop (42),
                juce::Justification::centred);
}

void PedalEditor::resized()
{
    auto r = getLocalBounds().reduced (20, 14);
    r.removeFromTop (56);
    // selectors two per row
    std::vector<Control*> boxes, knobsList;
    for (auto& c : controls) (c.box ? boxes : knobsList).push_back (&c);
    for (size_t i = 0; i < boxes.size(); i += 2)
    {
        auto row = r.removeFromTop (40);
        for (size_t k = i; k < std::min (boxes.size(), i + 2); ++k)
        {
            auto cell = row.removeFromLeft (r.getWidth() / 2).reduced (4, 0);
            boxes[k]->label->setBounds (cell.removeFromTop (14));
            boxes[k]->box->setBounds (cell.removeFromTop (22));
        }
        r.removeFromTop (4);
    }
    const int cols = juce::jmin (5, juce::jmax (3, (int) knobsList.size()));
    for (size_t k = 0; k < knobsList.size(); ++k)
    {
        const int row = (int) k / cols, col = (int) k % cols;
        auto cell = juce::Rectangle<int> (r.getX() + col * (r.getWidth() / cols), r.getY() + row * 112, r.getWidth() / cols, 108);
        knobsList[k]->label->setBounds (cell.removeFromTop (14));
        knobsList[k]->slider->setBounds (cell.reduced (6, 0));
    }
}

// =============================================================== PedalComponent
PedalComponent::PedalComponent (fx::Effect& effect, int index)
    : fx (effect), pedalIndex (index), style (styleFor (effect.getTypeId()))
{
    setTooltip (utf8 ("Clique para ver todos os controles. Arraste para mudar a ordem."));
    for (const auto& id : style.face)
    {
        const int i = fx.indexOf (id);
        if (i < 0)
            continue;
        const auto& p = fx.getParams()[(size_t) i];
        FaceKnob k { i, std::make_unique<juce::Slider>(), p.name };
        setupKnob (*k.slider, p, fx.get (i));
        k.slider->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        k.slider->setPopupDisplayEnabled (true, true, this);
        k.slider->getProperties().set ("cap", style.cap);
        k.slider->setLookAndFeel (&knobLook);
        k.slider->onValueChange = [this, i, s = k.slider.get()] { fx.setFromUser (i, (float) s->getValue()); refreshFromEffect(); };
        k.slider->onDragEnd = [this] { if (onChanged) onChanged(); };
        addAndMakeVisible (*k.slider);
        knobs.push_back (std::move (k));
    }

    footswitch.setToggleState (fx.enabled.load(), juce::dontSendNotification);
    footswitch.setTooltip ("Liga / desliga");
    footswitch.onClick = [this] { fx.enabled = footswitch.getToggleState(); repaint(); if (onChanged) onChanged(); };
    addAndMakeVisible (footswitch);
}

PedalComponent::~PedalComponent()
{
    for (auto& k : knobs)
        k.slider->setLookAndFeel (nullptr);
}

juce::Rectangle<float> PedalComponent::body() const
{
    return getLocalBounds().toFloat().reduced ((float) sideMargin, 4.0f).withTrimmedBottom (12.0f);
}

juce::Point<float> PedalComponent::inputJack() const  { return { 2.0f, body().getY() + body().getHeight() * 0.3f }; }
juce::Point<float> PedalComponent::outputJack() const { return { (float) getWidth() - 2.0f, body().getY() + body().getHeight() * 0.3f }; }

juce::String PedalComponent::modeText() const
{
    juce::String extra;
    const int mode = fx.indexOf ("mode");
    if (mode >= 0)
        extra = utf8 (fx.getParams()[(size_t) mode].choices[(size_t) fx.choice (mode)].c_str()).toUpperCase();
    if (fx.getTypeId() == "dualdelay")
    {
        const auto& names = fx.getParams()[(size_t) fx.indexOf ("voiceL")].choices;
        extra = "L " + utf8 (names[(size_t) fx.choice (fx.indexOf ("voiceL"))].c_str()).toUpperCase()
              + " / R " + utf8 (names[(size_t) fx.choice (fx.indexOf ("voiceR"))].c_str()).toUpperCase();
    }
    const int link = fx.indexOf ("link");
    if (link >= 0 && fx.choice (link) == 1)
        extra << utf8 (" · LINK");
    const int sync = fx.indexOf ("sync");
    if (sync >= 0 && fx.choice (sync) == 1)
        extra << (extra.isEmpty() ? "" : utf8 (" · ")) << "TAP";
    return extra;
}

void PedalComponent::refreshFromEffect()
{
    for (auto& k : knobs)
        k.slider->setValue (fx.get (k.param), juce::dontSendNotification);
    footswitch.setToggleState (fx.enabled.load(), juce::dontSendNotification);
    repaint();
}

void PedalComponent::paint (juce::Graphics& g)
{
    const auto b = body();
    if (dragging)
        g.setOpacity (0.65f);

    // jacks on both sides
    real::drawJack (g, juce::Rectangle<float> (10.0f, 16.0f).withCentre (inputJack().translated (3.0f, 0.0f)));
    real::drawJack (g, juce::Rectangle<float> (10.0f, 16.0f).withCentre (outputJack().translated (-3.0f, 0.0f)));

    real::drawEnclosure (g, b, style.body);
    for (auto p : { b.getTopLeft() + juce::Point<float> (9, 9), b.getTopRight() + juce::Point<float> (-9, 9),
                    b.getBottomLeft() + juce::Point<float> (9, -9), b.getBottomRight() + juce::Point<float> (-9, -9) })
        real::drawScrew (g, p);

    real::drawLed (g, { b.getCentreX(), b.getY() + 16.0f }, 4.5f, fx.enabled.load());

    // screen-printed knob labels
    g.setColour (style.ink.withAlpha (0.9f));
    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    for (auto& k : knobs)
    {
        const auto kb = k.slider->getBounds().toFloat();
        g.drawText (k.label, juce::Rectangle<float> (kb.getX() - 12.0f, kb.getBottom() - 1.0f, kb.getWidth() + 24.0f, 12.0f),
                    juce::Justification::centredTop);
    }

    // name plate
    const float nameY = b.getY() + b.getHeight() * 0.56f;
    g.setColour (style.ink);
    g.setFont (juce::FontOptions (style.title.length() > 8 ? 19.0f : 23.0f, juce::Font::bold));
    g.drawText (style.title, juce::Rectangle<float> (b.getX(), nameY, b.getWidth(), 28.0f), juce::Justification::centred);
    g.setColour (style.ink.withAlpha (0.7f));
    g.setFont (juce::FontOptions (9.5f));
    g.drawText (style.subtitle, juce::Rectangle<float> (b.getX(), nameY + 27.0f, b.getWidth(), 12.0f), juce::Justification::centred);
    const auto mode = modeText();
    if (mode.isNotEmpty())
    {
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        g.setColour (style.ink.withAlpha (0.85f));
        g.drawText (mode, juce::Rectangle<float> (b.getX() + 6.0f, nameY + 40.0f, b.getWidth() - 12.0f, 12.0f), juce::Justification::centred);
    }
}

void PedalComponent::resized()
{
    const auto b = body().toNearestInt();
    const int n = (int) knobs.size();
    if (n > 0)
    {
        // up to 3 knobs in one row; 4 knobs as one row on wide pedals, else 2 + 2
        const int perRow = (n == 4 && b.getWidth() < 200) ? 2 : n;
        const int size = juce::jmin (56, (b.getWidth() - 14) / juce::jmax (1, perRow));
        for (int k = 0; k < n; ++k)
        {
            const int row = k / perRow, col = k % perRow;
            const int inRow = juce::jmin (perRow, n - row * perRow);
            const int rowW = inRow * size;
            const int x = b.getCentreX() - rowW / 2 + col * size;
            const int y = b.getY() + 32 + row * (size + 14);
            knobs[(size_t) k].slider->setBounds (x, y, size, size);
        }
    }
    footswitch.setBounds (juce::Rectangle<int> (50, 50).withCentre ({ b.getCentreX(), b.getBottom() - 38 }));
}

void PedalComponent::openEditor()
{
    auto editor = std::make_unique<PedalEditor> (fx, style, [this] { refreshFromEffect(); if (onChanged) onChanged(); });
    auto* top = getTopLevelComponent();
    juce::CallOutBox::launchAsynchronously (std::move (editor), top->getLocalArea (this, getLocalBounds()), top);
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
        return;
    }
    openEditor();
}

// =============================================================== AmpTile (small amp head)
AmpTile::AmpTile()
{
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    setTooltip (utf8 ("Amplificador e gabinete (ajuste no cabeçote acima). Arraste para mudar a posição na corrente."));
}

void AmpTile::paint (juce::Graphics& g)
{
    if (dragging) g.setOpacity (0.65f);
    auto r = getLocalBounds().toFloat().reduced (8.0f, 4.0f).withTrimmedBottom (12.0f);
    real::drawJack (g, juce::Rectangle<float> (10.0f, 16.0f).withCentre (inputJack().translated (3.0f, 0.0f)));
    real::drawJack (g, juce::Rectangle<float> (10.0f, 16.0f).withCentre (outputJack().translated (-3.0f, 0.0f)));
    real::drawEnclosure (g, r, juce::Colour (0xff221d1a), 8.0f);
    // grill cloth
    auto grill = r.reduced (10.0f).removeFromTop (r.getHeight() * 0.42f);
    g.setColour (juce::Colour (0xff3a2f27));
    g.fillRoundedRectangle (grill, 4.0f);
    g.setColour (juce::Colour (0xff2a211b));
    for (float y = grill.getY() + 2.0f; y < grill.getBottom(); y += 4.0f)
        g.fillRect (grill.getX(), y, grill.getWidth(), 2.0f);
    g.setColour (juce::Colour (0xffefe3c0));
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold | juce::Font::italic));
    g.drawText ("AMP", grill, juce::Justification::centred);
    // gold control panel with small knobs
    auto panel = r.reduced (10.0f).withTrimmedTop (r.getHeight() * 0.46f).removeFromTop (30.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffdcc690), panel.getX(), panel.getY(), juce::Colour (0xffae9558), panel.getX(), panel.getBottom(), false));
    g.fillRoundedRectangle (panel, 3.0f);
    for (int i = 0; i < 5; ++i)
    {
        const float x = panel.getX() + panel.getWidth() * (0.12f + 0.19f * (float) i);
        real::drawChickenHead (g, juce::Rectangle<float> (18.0f, 18.0f).withCentre ({ x, panel.getCentreY() }), -0.6f + 0.4f * (float) i);
    }
    // what is loaded
    g.setColour (juce::Colour (0xffd8c9a8));
    g.setFont (juce::FontOptions (11.0f));
    g.drawFittedText (captureName, r.reduced (10.0f).withTrimmedTop (r.getHeight() * 0.46f + 36.0f).toNearestInt(), juce::Justification::centredTop, 4);
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
    title.setColour (juce::Label::textColourId, colours::accent);
    addAndMakeVisible (title);
    hint.setText (utf8 ("Clique num pedal para ver todos os controles · arraste para mudar a ordem · pise no botão para ligar"),
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
        p->onChanged = [this] { strip.repaint(); if (onChanged) onChanged(); };
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

std::pair<juce::Point<float>, juce::Point<float>> PedalboardView::jacksFor (int slot)
{
    if (slot == Pedalboard::ampSlot)
        return { amp.inputJack() + amp.getPosition().toFloat(), amp.outputJack() + amp.getPosition().toFloat() };
    auto& p = *pedals[(size_t) slot];
    return { p.inputJack() + p.getPosition().toFloat(), p.outputJack() + p.getPosition().toFloat() };
}

void PedalboardView::layoutStrip()
{
    const int h = juce::jmin (strip.getHeight(), 330);
    const int top = juce::jmax (0, (strip.getHeight() - h) / 2);
    int x = 40;
    for (int slot : board.getOrder())
    {
        auto* c = componentForSlot (slot);
        const int w = slot == Pedalboard::ampSlot ? 170 : pedals[(size_t) slot]->preferredWidth();
        c->setBounds (x, top, w, h);
        x += w + cableGap;
    }
    x += 6;
    if (strip.getWidth() != x)
        strip.setSize (juce::jmax (x, viewport.getMaximumVisibleWidth()), strip.getHeight());
    strip.repaint();
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
        dropMarkerX = componentForSlot (order.back())->getRight() + cableGap / 2;
    else
        dropMarkerX = componentForSlot (order[(size_t) position])->getX() - cableGap / 2;
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
    if (onChanged)
        onChanged();
}

void PedalboardView::Strip::paint (juce::Graphics& g)
{
    real::drawFloor (g, getLocalBounds().toFloat());

    // patch cables: guitar -> first pedal, between neighbours, last pedal -> output
    const auto order = owner.board.getOrder();
    if (order.empty())
        return;
    const auto first = owner.jacksFor (order.front()).first;
    real::drawCable (g, { 0.0f, first.y }, first, 30.0f);
    for (size_t i = 0; i + 1 < order.size(); ++i)
        real::drawCable (g, owner.jacksFor (order[i]).second, owner.jacksFor (order[i + 1]).first, 46.0f);
    const auto last = owner.jacksFor (order.back()).second;
    real::drawCable (g, last, { (float) getWidth(), last.y }, 30.0f);

    if (owner.dropMarkerX >= 0)
    {
        g.setColour (colours::accent);
        g.fillRoundedRectangle ((float) owner.dropMarkerX - 2.0f, 10.0f, 4.0f, (float) getHeight() - 20.0f, 2.0f);
    }
}

void PedalboardView::paint (juce::Graphics& g)
{
    g.setColour (juce::Colour (0xff121110));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 10.0f);
}

void PedalboardView::resized()
{
    auto r = getLocalBounds().reduced (10);
    auto top = r.removeFromTop (22);
    title.setBounds (top.removeFromLeft (110));
    hint.setBounds (top);
    r.removeFromTop (4);
    viewport.setBounds (r);
    strip.setSize (juce::jmax (strip.getWidth(), r.getWidth()), r.getHeight() - viewport.getScrollBarThickness());
    layoutStrip();
}

} // namespace gsp::ui
