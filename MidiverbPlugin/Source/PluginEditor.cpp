#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    using juce::Colour;
    using juce::Rectangle;

    enum Device { MIDIVERB = 0, MIDIFEX = 1, MIDIVERB2 = 2 };

    const Colour studioTop   { 0xff1d1d20 };
    const Colour studioBot   { 0xff0b0b0c };
    const Colour printWhite  { 0xffe9e9e4 };
    const Colour printGrey   { 0xff9a9a96 };
    const Colour verbBlue    { 0xff5ab4e6 };
    const Colour fexGold     { 0xffdda23a };
    const Colour verb2Blue   { 0xff3f96dc };
    const Colour ledRed      { 0xffff3b22 };
    const Colour keyOrange   { 0xffd98a2b };

    Colour accentFor(int device)
    {
        switch (device)
        {
            case MIDIFEX:   return fexGold;
            case MIDIVERB2: return verb2Blue;
            default:        return verbBlue;
        }
    }

    juce::Font font(float height, bool bold = false)
    {
        return juce::Font(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
    }

    // Text as a path with a forward slant, baseline at y = 0.
    juce::Path slantedText(const juce::String& text, float height, float slant)
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText(font(height, true), text, 0.0f, 0.0f);
        juce::Path p;
        ga.createPath(p);
        p.applyTransform(juce::AffineTransform::shear(-slant, 0.0f));
        return p;
    }

    // Seven-segment glyphs, bit 0 = segment a ... bit 6 = segment g.
    juce::uint8 segmentsFor(juce::juce_wchar c)
    {
        static const juce::uint8 digits[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };
        if (c >= '0' && c <= '9') return digits[c - '0'];
        if (c == '-') return 0x40;
        return 0;
    }

    juce::Path segmentPath(Rectangle<float> r, juce::uint8 mask)
    {
        const float w = r.getWidth(), h = r.getHeight();
        const float t = w * 0.19f, gap = t * 0.14f, half = h * 0.5f;
        const Rectangle<float> seg[7] = {
            { t * 0.5f, 0.0f, w - t, t },                          // a
            { w - t, t * 0.5f, t, half - t * 0.5f },               // b
            { w - t, half, t, half - t * 0.5f },                   // c
            { t * 0.5f, h - t, w - t, t },                         // d
            { 0.0f, half, t, half - t * 0.5f },                    // e
            { 0.0f, t * 0.5f, t, half - t * 0.5f },                // f
            { t * 0.5f, half - t * 0.5f, w - t, t },               // g
        };
        juce::Path p;
        for (int i = 0; i < 7; ++i)
            if (mask & (1 << i))
                p.addRoundedRectangle(seg[i].reduced(gap), t * 0.4f);
        // Slight forward lean, like the real displays.
        p.applyTransform(juce::AffineTransform::shear(-0.08f, 0.0f).translated(r.getX() + h * 0.08f, r.getY()));
        return p;
    }

    void drawScrews(juce::Graphics& g, Rectangle<float> r, float inset)
    {
        const juce::Point<float> pts[] = {
            { r.getX() + inset, r.getY() + inset }, { r.getRight() - inset, r.getY() + inset },
            { r.getX() + inset, r.getBottom() - inset }, { r.getRight() - inset, r.getBottom() - inset },
            { r.getCentreX(), r.getY() + inset }, { r.getCentreX(), r.getBottom() - inset },
        };
        for (auto p : pts)
        {
            auto s = Rectangle<float>(7.0f, 7.0f).withCentre(p);
            g.setColour(Colour(0xff050505));
            g.fillEllipse(s.expanded(1.0f));
            g.setGradientFill(juce::ColourGradient(Colour(0xff4a4a4c), s.getX(), s.getY(),
                                                   Colour(0xff1a1a1b), s.getRight(), s.getBottom(), false));
            g.fillEllipse(s);
            g.setColour(Colour(0xff0a0a0a));
            g.drawLine(s.getX() + 1.5f, s.getCentreY(), s.getRight() - 1.5f, s.getCentreY(), 1.2f);
        }
    }
}

//==============================================================================
void UnitKnobLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                           float pos, float startAngle, float endAngle, juce::Slider&)
{
    auto bounds = juce::Rectangle<float>((float) x, (float) y, (float) width, (float) height);
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f - 7.0f;
    const auto c = bounds.getCentre();

    // Scale dots
    g.setColour(printGrey);
    for (int i = 0; i <= 10; ++i)
    {
        const float a = startAngle + (endAngle - startAngle) * (float) i / 10.0f;
        const auto p = c.getPointOnCircumference(radius + 4.5f, a);
        g.fillEllipse(p.x - 0.9f, p.y - 0.9f, 1.8f, 1.8f);
    }

    // Shadow
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.fillEllipse(Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(c.translated(0.0f, 2.0f)));

    // Knurled skirt
    auto skirt = Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(c);
    g.setColour(Colour(0xff0f0f10));
    g.fillEllipse(skirt);
    g.setColour(Colour(0xff2b2b2d));
    for (int i = 0; i < 36; ++i)
    {
        const float a = juce::MathConstants<float>::twoPi * (float) i / 36.0f;
        g.drawLine(juce::Line<float>(c.getPointOnCircumference(radius * 0.80f, a),
                                     c.getPointOnCircumference(radius * 0.98f, a)), 1.0f);
    }

    // Cap
    auto cap = skirt.reduced(radius * 0.24f);
    g.setGradientFill(juce::ColourGradient(Colour(0xff3c3c3f), cap.getX(), cap.getY(),
                                           Colour(0xff141415), cap.getRight(), cap.getBottom(), false));
    g.fillEllipse(cap);
    g.setColour(Colour(0x30ffffff));
    g.drawEllipse(cap, 0.8f);

    // Pointer
    const float angle = startAngle + pos * (endAngle - startAngle);
    g.setColour(printWhite);
    g.drawLine(juce::Line<float>(c.getPointOnCircumference(radius * 0.25f, angle),
                                 c.getPointOnCircumference(radius * 0.95f, angle)), 2.2f);
}

//==============================================================================
UnitPanel::UnitPanel(MidiverbAudioProcessor& p) : processor(p)
{
    for (auto* knob : { &inputKnob, &mixKnob, &feedbackKnob, &outputKnob })
    {
        knob->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        knob->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        knob->setLookAndFeel(&knobLook);
        knob->setMouseDragSensitivity(180);
        addAndMakeVisible(knob);
    }
    for (auto* knob : { &mixKnob, &feedbackKnob })
    {
        knob->onValueChange = [this, knob]
        {
            if (knob->isMouseButtonDown())
            {
                knobValueShown = juce::roundToInt(knob->getValue() * 99.0);
                knobValueSince = juce::Time::getMillisecondCounter();
                repaint();
            }
        };
    }
    // The gain knobs show their value in dB in a bubble while turning
    for (auto* knob : { &inputKnob, &outputKnob })
    {
        knob->setPopupDisplayEnabled(true, true, this);
        knob->setDoubleClickReturnValue(true, 0.0);
    }
    mixKnob.setDoubleClickReturnValue(true, 0.5);
    feedbackKnob.setDoubleClickReturnValue(true, 0.0);
    mixKnob.setTooltip("Mix (dry/wet)");
    feedbackKnob.setTooltip("Feedback");
    inputKnob.setTooltip("Input gain into the converter. Drive it until the clip LED just flickers.");
    outputKnob.setTooltip("Output trim of the wet signal");

    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.getAPVTS(), "dryWet", mixKnob);
    feedbackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.getAPVTS(), "feedback", feedbackKnob);
    inputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.getAPVTS(), "inputGain", inputKnob);
    outputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.getAPVTS(), "outputGain", outputKnob);

    setSize(logicalWidth, logicalHeight);
    refresh(false);
}

UnitPanel::~UnitPanel()
{
    for (auto* knob : { &inputKnob, &mixKnob, &feedbackKnob, &outputKnob })
        knob->setLookAndFeel(nullptr);
}

int UnitPanel::currentDevice() const
{
    return juce::jlimit(0, MidiverbAudioProcessor::NUM_DEVICES - 1,
                        (int) *processor.getAPVTS().getRawParameterValue("device"));
}

int UnitPanel::currentProgram() const
{
    return (int) *processor.getAPVTS().getRawParameterValue("program");
}

void UnitPanel::setIntParam(const juce::String& id, int value)
{
    if (auto* param = processor.getAPVTS().getParameter(id))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost(param->convertTo0to1((float) value));
        param->endChangeGesture();
    }
}

void UnitPanel::selectProgram(int index)
{
    const int n = MidiverbAudioProcessor::getDeviceNumEffects(currentDevice());
    setIntParam("program", juce::jlimit(0, n - 1, index));
    knobValueShown = -1;
    refresh(false);
}

void UnitPanel::step(int delta)
{
    const int device = currentDevice();
    // On the desktop units the last slot is DEFEAT, reached with its own button.
    const int last = MidiverbAudioProcessor::getDeviceNumEffects(device) - (device == MIDIVERB2 ? 1 : 2);
    int p = currentProgram();
    if (device != MIDIVERB2 && p > last)
        p = programBeforeDefeat;
    p += delta;
    if (p < 0) p = last;
    if (p > last) p = 0;
    pendingDigit = -1;
    selectProgram(p);
}

void UnitPanel::pressKey(int digit)
{
    const auto now = juce::Time::getMillisecondCounter();
    if (pendingDigit >= 0 && now - pendingSince < 2500)
    {
        const int value = pendingDigit * 10 + digit;
        pendingDigit = -1;
        selectProgram(value);
    }
    else
    {
        pendingDigit = digit;
        pendingSince = now;
        knobValueShown = -1;
        repaint();
    }
}

void UnitPanel::refresh(bool overload)
{
    const int device = currentDevice();
    const int n = MidiverbAudioProcessor::getDeviceNumEffects(device);
    int program = currentProgram();
    if (program >= n)
    {
        program = 0;
        setIntParam("program", program);
    }

    bool needsRepaint = false;
    if (device != shownDevice)
    {
        const int oldHeight = currentLogicalHeight();
        shownDevice = device;
        pendingDigit = -1;
        layout();
        needsRepaint = true;
        if (currentLogicalHeight() != oldHeight)
        {
            setSize(logicalWidth, currentLogicalHeight());
            if (onHeightChanged) onHeightChanged();
        }
    }
    if (program != shownProgram) { shownProgram = program; needsRepaint = true; }
    const auto now = juce::Time::getMillisecondCounter();
    if (overload) overloadSince = now;
    const bool clipLit = overloadSince != 0 && now - overloadSince < 250;
    if (clipLit != shownOverload) { shownOverload = clipLit; needsRepaint = true; }

    if (pendingDigit >= 0)
    {
        if (now - pendingSince >= 2500) pendingDigit = -1;
        needsRepaint = true;   // blinking
    }
    if (knobValueShown >= 0 && now - knobValueSince >= 1200)
    {
        knobValueShown = -1;
        needsRepaint = true;
    }

    if (needsRepaint)
        repaint();
}

//==============================================================================
// Layout: hit areas and knob positions for the current unit.

void UnitPanel::layout()
{
    hits.clear();
    hasPressed = false;

    for (int d = 0; d < MidiverbAudioProcessor::NUM_DEVICES; ++d)
        hits.push_back({ { 30.0f + 150.0f * (float) d, 10.0f, 144.0f, 28.0f }, HitType::Tab, d });
    hits.push_back({ { 770.0f, 10.0f, 160.0f, 28.0f }, HitType::Collapse, 0 });

    const int device = shownDevice;
    if (device == MIDIVERB2)
    {
        const float x0 = 50.0f, y0 = 84.0f, colW = 172.0f, rowH = 17.2f;
        for (int i = 0; i < 100; ++i)
            hits.push_back({ { x0 + colW * (float) (i / 20), y0 + rowH * (float) (i % 20), colW, rowH },
                             HitType::Program, i });

        hits.push_back({ { 628.0f, 506.0f, 36.0f, 28.0f }, HitType::Up, 0 });
        hits.push_back({ { 628.0f, 546.0f, 36.0f, 28.0f }, HitType::Down, 0 });
        for (int k = 0; k < 10; ++k)
            hits.push_back({ { 690.0f + 42.0f * (float) (k % 5), (k < 5 ? 506.0f : 546.0f), 34.0f, 28.0f },
                             HitType::Key, k });

        // Right of the wordmark box (x <= 276), left of the clip LED (x ~ 505).
        // Knobs with their labels are centred vertically on the display (y 508..570).
        inputKnob.setBounds(292, 523, 46, 46);
        mixKnob.setBounds(342, 523, 46, 46);
        feedbackKnob.setBounds(392, 523, 46, 46);
        outputKnob.setBounds(442, 523, 46, 46);
    }
    else
    {
        const float x0 = 60.0f, y0 = 160.0f, colW = 280.0f, rowH = 15.6f;
        for (int i = 0; i < 63; ++i)
            hits.push_back({ { x0 + colW * (float) (i / 21), y0 + rowH * (float) (i % 21), colW, rowH },
                             HitType::Program, i });

        hits.push_back({ { 150.0f, 534.0f, 42.0f, 30.0f }, HitType::Defeat, 0 });
        hits.push_back({ { 378.0f, 534.0f, 42.0f, 30.0f }, HitType::Down, 0 });
        hits.push_back({ { 436.0f, 534.0f, 42.0f, 30.0f }, HitType::Up, 0 });

        inputKnob.setBounds(500, 522, 60, 60);
        mixKnob.setBounds(582, 522, 60, 60);
        feedbackKnob.setBounds(664, 522, 60, 60);
        outputKnob.setBounds(746, 522, 60, 60);
    }

    // All knobs sit below the collapsible band
    if (collapsed)
        for (auto* k : { &inputKnob, &mixKnob, &feedbackKnob, &outputKnob })
            k->setTopLeftPosition(k->getX(), k->getY() - juce::roundToInt(collapseShift()));
}

const UnitPanel::Hit* UnitPanel::hitAt(juce::Point<float> p) const
{
    if (collapsed && p.y >= cutFor(shownDevice).start)
        p.y += collapseShift();   // back to full-size coordinates
    for (const auto& h : hits)
        if (h.area.contains(p))
            return &h;
    return nullptr;
}

bool UnitPanel::isPressed(HitType type, int value) const
{
    return hasPressed && pressedCopy.type == type && pressedCopy.value == value;
}

void UnitPanel::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu menu;
        menu.addItem(1, juce::String("Build: ") + __DATE__ + " " + __TIME__, false);
        menu.showMenuAsync(juce::PopupMenu::Options());
        return;
    }

    const auto* h = hitAt(e.position);
    if (h == nullptr)
        return;

    pressedCopy = *h;
    hasPressed = true;

    switch (h->type)
    {
        case HitType::Collapse:
            setCollapsed(! collapsed);
            return;
        case HitType::Tab:
            if (h->value != currentDevice())
            {
                setIntParam("device", h->value);
                refresh(false);
            }
            break;
        case HitType::Program:
            selectProgram(h->value);
            break;
        case HitType::Up:   step(+1); break;
        case HitType::Down: step(-1); break;
        case HitType::Key:  pressKey(h->value); break;
        case HitType::Defeat:
        {
            const int defeatIndex = MidiverbAudioProcessor::getDeviceNumEffects(currentDevice()) - 1;
            if (currentProgram() == defeatIndex)
                selectProgram(programBeforeDefeat);
            else
            {
                programBeforeDefeat = currentProgram();
                selectProgram(defeatIndex);
            }
            break;
        }
    }
    repaint();
}

void UnitPanel::mouseUp(const juce::MouseEvent&)
{
    if (hasPressed)
    {
        hasPressed = false;
        repaint();
    }
}

//==============================================================================
// Painting

juce::String UnitPanel::displayText() const
{
    const auto now = juce::Time::getMillisecondCounter();
    if (knobValueShown >= 0)
        return juce::String(knobValueShown).paddedLeft('0', 2);

    const int device = shownDevice;
    if (device == MIDIVERB2)
    {
        if (pendingDigit >= 0)
            return juce::String(pendingDigit) + (((now / 250) % 2) == 0 ? "-" : " ");
        return juce::String(shownProgram).paddedLeft('0', 2);
    }

    if (shownProgram >= MidiverbAudioProcessor::getDeviceNumEffects(device) - 1)
        return "--";   // DEFEAT
    return juce::String(shownProgram + 1).paddedLeft(' ', 2);
}

// Vertical band (in full-size coordinates) removed when the program chart is
// collapsed: everything below it moves up by (end - start).
UnitPanel::Cut UnitPanel::cutFor(int device) const
{
    if (device == MIDIVERB2)
        return { 46.0f, 458.0f };    // the lid with the program list
    return { 156.0f, 490.0f };       // the chart printed on the lid, below the wordmark
}

float UnitPanel::collapseShift() const
{
    if (! collapsed) return 0.0f;
    const auto c = cutFor(shownDevice);
    return c.end - c.start;
}

int UnitPanel::currentLogicalHeight() const
{
    return logicalHeight - juce::roundToInt(collapseShift());
}

void UnitPanel::setCollapsed(bool shouldCollapse)
{
    if (collapsed == shouldCollapse) return;
    collapsed = shouldCollapse;
    layout();
    setSize(logicalWidth, currentLogicalHeight());
    repaint();
    if (onHeightChanged) onHeightChanged();
}

void UnitPanel::paint(juce::Graphics& g)
{
    if (! collapsed)
    {
        paintContent(g);
        return;
    }
    const auto cut = cutFor(shownDevice);
    const float shift = cut.end - cut.start;
    // Background once over the visible height, so its shading has no seam
    g.setGradientFill(juce::ColourGradient(studioTop, 0.0f, 0.0f, studioBot, 0.0f, (float) getHeight(), false));
    g.fillAll();
    {
        juce::Graphics::ScopedSaveState s(g);
        g.reduceClipRegion(0, 0, logicalWidth, (int) cut.start);
        paintContent(g);
    }
    {
        juce::Graphics::ScopedSaveState s(g);
        g.reduceClipRegion(0, (int) cut.start, logicalWidth, logicalHeight);
        g.addTransform(juce::AffineTransform::translation(0.0f, -shift));
        paintContent(g);
    }
}

void UnitPanel::paintContent(juce::Graphics& g)
{
    if (! collapsed)
    {
        g.setGradientFill(juce::ColourGradient(studioTop, 0.0f, 0.0f, studioBot, 0.0f, (float) logicalHeight, false));
        g.fillAll();
    }

    paintTabs(g);

    if (shownDevice == MIDIVERB2)
        paintRackUnit(g);
    else
        paintDesktopUnit(g, shownDevice == MIDIFEX);
}

void UnitPanel::paintTabs(juce::Graphics& g)
{
    static const char* labels[] = { "MIDIVERB", "MIDIFEX", "VITIVERB II" };
    for (const auto& h : hits)
    {
        if (h.type != HitType::Tab) continue;
        const bool active = h.value == shownDevice;
        const auto accent = accentFor(h.value);
        auto r = h.area;

        g.setColour(active ? Colour(0xff28282b) : Colour(0xff151517));
        g.fillRoundedRectangle(r, 4.0f);
        g.setColour(active ? accent : Colour(0xff2e2e31));
        g.drawRoundedRectangle(r.reduced(0.5f), 4.0f, 1.0f);
        if (active)
            g.fillRect(r.getX() + 10.0f, r.getBottom() - 4.0f, r.getWidth() - 20.0f, 2.0f);

        auto text = slantedText(labels[h.value], 14.0f, 0.22f);
        const auto tb = text.getBounds();
        text.applyTransform(juce::AffineTransform::translation(r.getCentreX() - tb.getCentreX(),
                                                               r.getCentreY() - tb.getCentreY() - 1.0f));
        g.setColour(active ? printWhite : printGrey.withAlpha(0.7f));
        g.fillPath(text);
    }

    g.setColour(printGrey.withAlpha(0.6f));
    g.setFont(font(11.0f));
    g.drawText("DASP EMULATOR", Rectangle<float>(560.0f, 10.0f, 190.0f, 28.0f), juce::Justification::centredRight);

    // Program chart show/hide toggle
    for (const auto& h : hits)
    {
        if (h.type != HitType::Collapse) continue;
        auto r = h.area;
        const bool down = isPressed(HitType::Collapse);
        g.setColour(down ? Colour(0xff28282b) : Colour(0xff151517));
        g.fillRoundedRectangle(r, 4.0f);
        g.setColour(Colour(0xff2e2e31));
        g.drawRoundedRectangle(r.reduced(0.5f), 4.0f, 1.0f);

        // Triangle: pointing down = chart hidden (click to show), up = chart shown
        juce::Path tri;
        const float cx = r.getX() + 18.0f, cy = r.getCentreY();
        if (collapsed) tri.addTriangle(cx - 5.0f, cy - 3.0f, cx + 5.0f, cy - 3.0f, cx, cy + 4.0f);
        else           tri.addTriangle(cx - 5.0f, cy + 3.0f, cx + 5.0f, cy + 3.0f, cx, cy - 4.0f);
        g.setColour(accentFor(shownDevice));
        g.fillPath(tri);

        g.setColour(printWhite.withAlpha(0.85f));
        g.setFont(font(10.5f, true));
        g.drawText(collapsed ? "SHOW PROGRAMS" : "HIDE PROGRAMS", r.withTrimmedLeft(30.0f).withTrimmedRight(8.0f),
                   juce::Justification::centredLeft);
    }
}

void UnitPanel::paintButton(juce::Graphics& g, Rectangle<float> r, Colour cap, bool pressed)
{
    g.setColour(Colour(0xff050505));
    g.fillRoundedRectangle(r.expanded(2.0f), 4.0f);
    auto face = pressed ? r.translated(0.0f, 1.0f).reduced(0.5f) : r;
    const auto top = pressed ? cap.darker(0.35f) : cap.brighter(0.18f);
    const auto bottom = pressed ? cap.darker(0.6f) : cap.darker(0.35f);
    g.setGradientFill(juce::ColourGradient(top, face.getX(), face.getY(), bottom, face.getX(), face.getBottom(), false));
    g.fillRoundedRectangle(face, 3.0f);
    g.setColour(Colour(0x22ffffff));
    g.drawRoundedRectangle(face.reduced(0.5f), 3.0f, 0.8f);
}

void UnitPanel::paintLed(juce::Graphics& g, juce::Point<float> c, Colour on, bool lit)
{
    auto r = Rectangle<float>(7.0f, 7.0f).withCentre(c);
    if (lit)
    {
        g.setColour(on.withAlpha(0.25f));
        g.fillEllipse(r.expanded(4.0f));
    }
    g.setColour(lit ? on : on.darker(0.85f).withAlpha(0.8f));
    g.fillEllipse(r);
    g.setColour(Colour(0x40ffffff));
    g.fillEllipse(r.reduced(2.2f).translated(-0.8f, -0.8f));
}

void UnitPanel::paintDisplay(juce::Graphics& g, Rectangle<float> r)
{
    // Bezel and red filter glass
    g.setColour(Colour(0xff050505));
    g.fillRoundedRectangle(r.expanded(3.0f), 4.0f);
    g.setGradientFill(juce::ColourGradient(Colour(0xff2a0606), r.getX(), r.getY(),
                                           Colour(0xff120202), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle(r, 3.0f);

    const auto text = displayText();
    const float digitH = r.getHeight() * 0.68f;
    const float digitW = digitH * 0.55f;
    const float spacing = digitW * 0.38f;
    const float totalW = digitW * 2.0f + spacing;
    float x = r.getCentreX() - totalW * 0.5f;
    const float y = r.getCentreY() - digitH * 0.5f;

    for (int i = 0; i < 2; ++i)
    {
        Rectangle<float> cell(x, y, digitW, digitH);
        const auto lit = segmentsFor(i < text.length() ? text[i] : ' ');
        g.setColour(Colour(0xff3a0b07));
        g.fillPath(segmentPath(cell, 0x7F));
        const auto litPath = segmentPath(cell, lit);
        g.setColour(ledRed.withAlpha(0.25f));
        g.strokePath(litPath, juce::PathStrokeType(digitW * 0.16f));
        g.setColour(ledRed);
        g.fillPath(litPath);
        x += digitW + spacing;
    }

    // Glass reflection
    g.setGradientFill(juce::ColourGradient(Colour(0x0cffffff), r.getX(), r.getY(),
                                           Colour(0x00ffffff), r.getX(), r.getY() + r.getHeight() * 0.5f, false));
    g.fillRoundedRectangle(r.reduced(2.0f, 1.0f), 2.0f);
}

void UnitPanel::paintKnobLabel(juce::Graphics& g, const juce::Slider& knob, const juce::String& text,
                               Colour colour, bool above)
{
    // Painting happens in full-size coordinates; knob bounds are shifted when collapsed
    auto b = knob.getBounds().toFloat().translated(0.0f, collapseShift());
    auto r = above ? Rectangle<float>(b.getX() - 20.0f, b.getY() - 14.0f, b.getWidth() + 40.0f, 12.0f)
                   : Rectangle<float>(b.getX() - 20.0f, b.getBottom() + 1.0f, b.getWidth() + 40.0f, 12.0f);
    g.setColour(colour);
    g.setFont(font(10.0f, true));
    g.drawText(text, r, juce::Justification::centred);
}

//------------------------------------------------------------------------------
// MIDIVERB and MIDIFEX: black desktop box seen from above, program chart on the
// lid, small front strip with the controls.

void UnitPanel::paintDesktopUnit(juce::Graphics& g, bool isMidifex)
{
    const auto accent = isMidifex ? fexGold : verbBlue;
    const juce::String product = isMidifex ? "MIDIFEX" : "MIDIVERB";
    const juce::String descriptor = isMidifex ? "DIGITAL EFFECTS" : "DIGITAL REVERB";

    // Body
    const Rectangle<float> body(30.0f, 52.0f, 900.0f, 574.0f);
    g.setColour(juce::Colours::black.withAlpha(0.55f));
    g.fillRoundedRectangle(body.translated(0.0f, 6.0f).expanded(2.0f), 26.0f);
    {
        const Colour top(0xff26262a), bottom(0xff101012);
        juce::ColourGradient grad(top, body.getX(), body.getY(), bottom, body.getX(), body.getBottom(), false);
        if (collapsed)
        {
            // Hold the shade constant across the cut-out band so the two halves meet cleanly
            const auto cut = cutFor(shownDevice);
            const float visibleH = body.getHeight() - (cut.end - cut.start);
            const auto atCut = top.interpolatedWith(bottom, (cut.start - body.getY()) / visibleH);
            grad.addColour((cut.start - body.getY()) / body.getHeight(), atCut);
            grad.addColour((cut.end - body.getY()) / body.getHeight(), atCut);
        }
        g.setGradientFill(grad);
        g.fillRoundedRectangle(body, 24.0f);
    }
    g.setColour(Colour(0x18ffffff));
    g.drawRoundedRectangle(body.reduced(1.0f), 24.0f, 1.2f);

    // Printed lid area
    const Rectangle<float> lid(48.0f, 68.0f, 864.0f, 428.0f);
    g.setColour(Colour(0xff0c0c0d));
    g.fillRoundedRectangle(lid, 6.0f);

    // Connector legends along the rear edge
    g.setFont(font(8.5f, true));
    g.setColour(printGrey);
    struct Legend { float x; const char* text; };
    const Legend legends[] = { { 62, "LEFT" }, { 120, "RIGHT" }, { 205, "MIX" }, { 300, "LEFT" },
                               { 356, "RIGHT" }, { 520, "MIDI IN" }, { 610, "MIDI THRU" }, { 860, "16V CT" } };
    for (const auto& l : legends)
        g.drawText(l.text, Rectangle<float>(l.x, 73.0f, 70.0f, 10.0f), juce::Justification::left);
    g.drawText("INPUT", Rectangle<float>(92.0f, 84.0f, 50.0f, 10.0f), juce::Justification::left);
    g.drawText("OUTPUT", Rectangle<float>(328.0f, 84.0f, 50.0f, 10.0f), juce::Justification::left);
    g.drawLine(62.0f, 89.0f, 90.0f, 89.0f, 0.7f);
    g.drawLine(124.0f, 89.0f, 148.0f, 89.0f, 0.7f);
    g.drawLine(300.0f, 89.0f, 326.0f, 89.0f, 0.7f);
    g.drawLine(366.0f, 89.0f, 390.0f, 89.0f, 0.7f);

    // Speed stripes and wordmark
    for (int i = 0; i < 4; ++i)
    {
        g.setColour(accent.withAlpha(i == 3 ? 0.9f : 0.75f));
        g.fillRect(56.0f, 106.0f + 9.0f * (float) i, 848.0f, i == 3 ? 3.0f : 2.0f);
    }

    auto word = slantedText(product, 50.0f, 0.24f);
    word.applyTransform(juce::AffineTransform::translation(70.0f, 145.0f));
    g.setColour(Colour(0xff0c0c0d));
    g.strokePath(word, juce::PathStrokeType(7.0f, juce::PathStrokeType::curved));
    g.setColour(printWhite);
    g.fillPath(word);
    {
        // Thin dark cuts through the lower half of the letters
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(word);
        g.setColour(Colour(0xff0c0c0d));
        for (int i = 0; i < 4; ++i)
            g.fillRect(56.0f, 125.0f + 5.0f * (float) i, 400.0f, 1.6f);
    }

    auto desc = slantedText(descriptor, 15.0f, 0.24f);
    desc.applyTransform(juce::AffineTransform::translation(720.0f, 145.0f));
    g.setColour(Colour(0xff0c0c0d));
    g.strokePath(desc, juce::PathStrokeType(5.0f));
    g.setColour(printWhite);
    g.fillPath(desc);
    g.setColour(accent);
    g.fillRect(56.0f, 152.0f, 848.0f, 2.0f);

    // Program chart: 3 columns x 21 rows
    const int device = shownDevice;
    const float x0 = 60.0f, y0 = 160.0f, colW = 280.0f, rowH = 15.6f;
    g.setColour(printWhite.withAlpha(0.55f));
    g.drawRect(Rectangle<float>(x0, y0, colW * 3.0f, rowH * 21.0f), 0.8f);
    for (int c = 0; c < 3; ++c)
    {
        g.drawLine(x0 + colW * (float) c, y0, x0 + colW * (float) c, y0 + rowH * 21.0f, 0.8f);
        g.drawLine(x0 + colW * (float) c + 28.0f, y0, x0 + colW * (float) c + 28.0f, y0 + rowH * 21.0f, 0.6f);
    }
    for (int r = 1; r < 21; ++r)
        g.drawLine(x0, y0 + rowH * (float) r, x0 + colW * 3.0f, y0 + rowH * (float) r, 0.5f);

    g.setFont(font(10.5f, true));
    for (int i = 0; i < 63; ++i)
    {
        const Rectangle<float> cell(x0 + colW * (float) (i / 21), y0 + rowH * (float) (i % 21), colW, rowH);
        const bool selected = i == shownProgram;
        if (selected)
        {
            g.setColour(accent);
            g.fillRect(cell.reduced(1.0f));
        }
        g.setColour(selected ? Colour(0xff0c0c0d) : printWhite);
        g.drawText(juce::String(i + 1), cell.withWidth(24.0f), juce::Justification::centredRight);
        g.drawText(juce::String(MidiverbAudioProcessor::getEffectName(device, i)).toUpperCase(),
                   cell.withTrimmedLeft(36.0f), juce::Justification::centredLeft);
    }

    // Front strip
    const Rectangle<float> strip(110.0f, 508.0f, 740.0f, 102.0f);
    g.setColour(Colour(0xff050506));
    g.fillRoundedRectangle(strip.expanded(3.0f), 8.0f);
    g.setGradientFill(juce::ColourGradient(Colour(0xff17171a), strip.getX(), strip.getY(),
                                           Colour(0xff0e0e10), strip.getX(), strip.getBottom(), false));
    g.fillRoundedRectangle(strip, 6.0f);
    g.setColour(accent.withAlpha(0.8f));
    g.fillRect(strip.getX() + 8.0f, 514.0f, strip.getWidth() - 16.0f, 2.0f);
    g.fillRect(strip.getX() + 8.0f, 519.0f, strip.getWidth() - 16.0f, 1.2f);
    g.fillRect(strip.getX() + 8.0f, 601.0f, strip.getWidth() - 16.0f, 1.2f);

    const int defeatIndex = MidiverbAudioProcessor::getDeviceNumEffects(device) - 1;
    for (const auto& h : hits)
    {
        if (h.type == HitType::Defeat || h.type == HitType::Up || h.type == HitType::Down)
            paintButton(g, h.area, Colour(0xff313134), isPressed(h.type));
    }

    g.setFont(font(9.5f, true));
    g.setColour(printWhite);
    g.drawText("DEFEAT", Rectangle<float>(141.0f, 568.0f, 60.0f, 12.0f), juce::Justification::centred);
    g.drawText("DOWN", Rectangle<float>(369.0f, 568.0f, 60.0f, 12.0f), juce::Justification::centred);
    g.drawText("UP", Rectangle<float>(427.0f, 568.0f, 60.0f, 12.0f), juce::Justification::centred);
    g.drawText("PROGRAM", Rectangle<float>(250.0f, 584.0f, 100.0f, 12.0f), juce::Justification::centred);

    paintLed(g, { 220.0f, 540.0f }, ledRed, shownOverload);
    paintLed(g, { 220.0f, 562.0f }, Colour(0xff3ddc5a), shownProgram == defeatIndex);
    g.setFont(font(8.5f, true));
    g.setColour(printGrey);
    g.drawText("0", Rectangle<float>(228.0f, 535.0f, 20.0f, 10.0f), juce::Justification::left);
    g.drawText("DEF", Rectangle<float>(228.0f, 557.0f, 20.0f, 10.0f), juce::Justification::left);

    paintDisplay(g, { 256.0f, 532.0f, 88.0f, 48.0f });

    paintKnobLabel(g, inputKnob, "INPUT", printWhite, false);
    paintKnobLabel(g, mixKnob, "MIX", printWhite, false);
    paintKnobLabel(g, feedbackKnob, "FEEDBACK", printWhite, false);
    paintKnobLabel(g, outputKnob, "OUTPUT", printWhite, false);
}

//------------------------------------------------------------------------------
// MIDIVERB II: 1U rack unit. The lid (program list) seen from above, and the
// front panel below it.

void UnitPanel::paintRackUnit(juce::Graphics& g)
{
    const int device = shownDevice;

    // Lid
    const Rectangle<float> lid(30.0f, 52.0f, 900.0f, 400.0f);
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.fillRect(lid.translated(0.0f, 5.0f));
    g.setGradientFill(juce::ColourGradient(Colour(0xff1d1d20), lid.getX(), lid.getY(),
                                           Colour(0xff141416), lid.getX(), lid.getBottom(), false));
    g.fillRect(lid);
    g.setColour(Colour(0x07ffffff));
    for (float y = lid.getY() + 2.0f; y < lid.getBottom(); y += 3.0f)
        g.drawHorizontalLine((int) y, lid.getX(), lid.getRight());
    drawScrews(g, lid, 9.0f);

    // Program list printed on the lid
    const float x0 = 50.0f, y0 = 84.0f, colW = 172.0f, rowH = 17.2f;
    g.setFont(font(9.0f, true));
    g.setColour(verb2Blue);
    g.drawText("PROGRAMS", Rectangle<float>(x0, 66.0f, 200.0f, 12.0f), juce::Justification::left);
    g.fillRect(x0 + 62.0f, 71.5f, colW * 5.0f - 62.0f, 1.2f);

    g.setColour(printWhite.withAlpha(0.35f));
    for (int c = 0; c <= 5; ++c)
        g.drawLine(x0 + colW * (float) c, y0, x0 + colW * (float) c, y0 + rowH * 20.0f, 0.7f);
    g.drawLine(x0, y0, x0 + colW * 5.0f, y0, 0.7f);
    g.drawLine(x0, y0 + rowH * 20.0f, x0 + colW * 5.0f, y0 + rowH * 20.0f, 0.7f);

    g.setFont(font(10.0f, true));
    for (int i = 0; i < 100; ++i)
    {
        const Rectangle<float> cell(x0 + colW * (float) (i / 20), y0 + rowH * (float) (i % 20), colW, rowH);
        const bool selected = i == shownProgram;
        if (selected)
        {
            g.setColour(verb2Blue);
            g.fillRect(cell.reduced(1.0f));
        }
        else if (i % 10 == 0 && i > 0)
        {
            g.setColour(printWhite.withAlpha(0.22f));
            g.drawLine(cell.getX(), cell.getY(), cell.getRight(), cell.getY(), 0.6f);
        }
        g.setColour(selected ? Colour(0xff0c0c0d) : verb2Blue.brighter(0.3f));
        g.drawText(juce::String(i).paddedLeft('0', 2), cell.withWidth(24.0f), juce::Justification::centredRight);
        g.setColour(selected ? Colour(0xff0c0c0d) : printWhite);
        g.drawFittedText(juce::String(MidiverbAudioProcessor::getEffectName(device, i)).toUpperCase(),
                         cell.withTrimmedLeft(30.0f).withTrimmedRight(4.0f).toNearestInt(),
                         juce::Justification::centredLeft, 1, 0.8f);
    }

    // Front panel
    const Rectangle<float> face(14.0f, 466.0f, 932.0f, 152.0f);
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.fillRect(face.translated(0.0f, 5.0f));
    g.setGradientFill(juce::ColourGradient(Colour(0xff222225), face.getX(), face.getY(),
                                           Colour(0xff0f0f11), face.getX(), face.getBottom(), false));
    g.fillRect(face);
    g.setColour(Colour(0x22ffffff));
    g.drawHorizontalLine((int) face.getY() + 1, face.getX(), face.getRight());

    // Rack ears with slots
    for (float earX : { face.getX(), face.getRight() - 40.0f })
    {
        g.setColour(Colour(0x30000000));
        g.drawVerticalLine((int) (earX < 100.0f ? earX + 40.0f : earX), face.getY(), face.getBottom());
        for (float sy : { face.getY() + 24.0f, face.getBottom() - 34.0f })
        {
            auto slot = Rectangle<float>(earX + 9.0f, sy, 22.0f, 10.0f);
            g.setColour(Colour(0xff050505));
            g.fillRoundedRectangle(slot, 5.0f);
            g.setColour(Colour(0x18ffffff));
            g.drawRoundedRectangle(slot.translated(0.0f, 1.0f), 5.0f, 0.6f);
        }
    }

    // Wordmark
    // The default typeface differs per OS (and so does the text width), so the
    // wordmark and subtitle are fitted into a fixed box left of the knobs.
    constexpr float brandLeft = 74.0f, brandRight = 276.0f;
    auto fitToBrandBox = [&](juce::Path& p, float baselineY)
    {
        const auto b = p.getBounds();
        const float scale = juce::jmin(1.0f, (brandRight - brandLeft) / b.getWidth());
        p.applyTransform(juce::AffineTransform::translation(-b.getX(), 0.0f)
                             .scaled(scale)
                             .translated(brandLeft, baselineY));
    };

    auto word = slantedText("VITIVERB", 40.0f, 0.24f);
    auto numeral = slantedText("II", 40.0f, 0.24f);
    numeral.applyTransform(juce::AffineTransform::translation(word.getBounds().getRight() + 6.0f, 0.0f));
    juce::Path brand(word);
    brand.addPath(numeral);
    fitToBrandBox(brand, 552.0f);
    auto sub = slantedText("16 BIT DIGITAL EFFECTS PROCESSOR", 11.0f, 0.2f);
    fitToBrandBox(sub, 570.0f);

    // Centre the two lines (wordmark top to subtitle underline) on the display's
    // centre line, which the knobs are centred on too.
    constexpr float rowCentreY = 539.0f;   // display: y 508..570
    const float blockTop = brand.getBounds().getY();
    const float blockBottom = 576.0f + 1.0f;   // underline below the subtitle
    const float shiftY = rowCentreY - (blockTop + blockBottom) * 0.5f;
    brand.applyTransform(juce::AffineTransform::translation(0.0f, shiftY));
    sub.applyTransform(juce::AffineTransform::translation(0.0f, shiftY));
    const float wordBaseline = 552.0f + shiftY;

    const auto brandBounds = brand.getBounds();
    g.setGradientFill(juce::ColourGradient(verb2Blue.brighter(0.5f), 0.0f, brandBounds.getY(),
                                           verb2Blue.darker(0.2f), 0.0f, wordBaseline, false));
    g.fillPath(brand);
    g.setColour(verb2Blue.brighter(0.8f).withAlpha(0.7f));
    g.strokePath(brand, juce::PathStrokeType(0.8f));

    g.setColour(verb2Blue);
    g.fillPath(sub);
    g.fillRect(brandLeft, 576.0f + shiftY, sub.getBounds().getWidth() + 4.0f, 1.0f);

    // Knobs
    paintKnobLabel(g, inputKnob, "INPUT", printWhite, true);
    paintKnobLabel(g, mixKnob, "MIX", printWhite, true);
    paintKnobLabel(g, feedbackKnob, "FEEDBACK", printWhite, true);
    paintKnobLabel(g, outputKnob, "OUTPUT", printWhite, true);

    // Clip LED and display
    paintLed(g, { 512.0f, 520.0f }, ledRed, shownOverload);
    g.setFont(font(8.5f, true));
    g.setColour(printWhite);
    g.drawText("CLIP", Rectangle<float>(498.0f, 528.0f, 28.0f, 10.0f), juce::Justification::centred);
    paintDisplay(g, { 530.0f, 508.0f, 80.0f, 62.0f });
    g.setColour(printWhite);
    g.setFont(font(8.5f, true));
    g.drawText("PROGRAM", Rectangle<float>(530.0f, 576.0f, 80.0f, 10.0f), juce::Justification::centred);

    // Up / down and keypad
    for (const auto& h : hits)
    {
        if (h.type == HitType::Up || h.type == HitType::Down)
        {
            paintButton(g, h.area, keyOrange, isPressed(h.type));
            g.setColour(Colour(0xff1a1208));
            g.setFont(font(8.5f, true));
            g.drawText(h.type == HitType::Up ? "UP" : "DOWN",
                       h.area.translated(0.0f, isPressed(h.type) ? 1.0f : 0.0f), juce::Justification::centred);
        }
        else if (h.type == HitType::Key)
        {
            paintButton(g, h.area, Colour(0xff3b3b3f), isPressed(HitType::Key, h.value));
            g.setColour(printWhite);
            g.setFont(font(12.0f, true));
            g.drawText(juce::String(h.value),
                       h.area.translated(0.0f, isPressed(HitType::Key, h.value) ? 1.0f : 0.0f), juce::Justification::centred);
        }
    }

    // Program group legends around the keypad (first digit of the program number)
    g.setFont(font(8.5f, true));
    g.setColour(printWhite);
    auto keyX = [](int col) { return 690.0f + 42.0f * (float) col; };
    auto legend = [&](int fromCol, int toCol, float y, const char* text, bool bracketBelow)
    {
        const float x1 = keyX(fromCol), x2 = keyX(toCol) + 34.0f;
        g.drawText(text, Rectangle<float>(x1 - 6.0f, y, x2 - x1 + 12.0f, 10.0f), juce::Justification::centred);
        if (toCol > fromCol)
        {
            const float ly = bracketBelow ? y + 12.0f : y - 2.0f;
            g.drawLine(x1 + 2.0f, ly, x2 - 2.0f, ly, 0.7f);
        }
    };
    legend(0, 2, 490.0f, "REVERB", true);
    legend(3, 3, 490.0f, "GATE", true);
    legend(4, 4, 490.0f, "REVERSE", true);
    legend(0, 0, 580.0f, "FLANGE", false);
    legend(1, 1, 580.0f, "CHORUS", false);
    legend(2, 3, 580.0f, "DELAY", false);
    legend(4, 4, 580.0f, "MULTI", false);

    drawScrews(g, Rectangle<float>(54.0f, face.getY(), 852.0f, face.getHeight()), 8.0f);
}

//==============================================================================
// Editor size and chart state are stored as properties of the parameter state,
// so they are saved with the plugin's settings (and the host project).
static const juce::Identifier editorWidthId { "editorWidth" };
static const juce::Identifier chartCollapsedId { "programsCollapsed" };

MidiverbAudioProcessorEditor::MidiverbAudioProcessorEditor(MidiverbAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p), panel(p)
{
    addAndMakeVisible(panel);

    auto& state = audioProcessor.getAPVTS().state;
    panel.setCollapsed((bool) state.getProperty(chartCollapsedId, false));
    panel.onHeightChanged = [this]
    {
        audioProcessor.getAPVTS().state.setProperty(chartCollapsedId, panel.isCollapsed(), nullptr);
        const int width = getWidth();   // read before new limits can adjust the window
        applySizeLimits();
        setSize(width, heightForWidth(width));
    };

    setResizable(true, true);
    applySizeLimits();
    const int savedWidth = juce::jlimit(UnitPanel::logicalWidth / 2, UnitPanel::logicalWidth * 2,
                                        (int) state.getProperty(editorWidthId, defaultWidth));
    setSize(savedWidth, heightForWidth(savedWidth));
    restoring = false;

    startTimerHz(30);
}

MidiverbAudioProcessorEditor::~MidiverbAudioProcessorEditor()
{
    stopTimer();
}

int MidiverbAudioProcessorEditor::heightForWidth(int width) const
{
    return juce::roundToInt((double) width * panel.currentLogicalHeight() / (double) UnitPanel::logicalWidth);
}

void MidiverbAudioProcessorEditor::applySizeLimits()
{
    const int minW = UnitPanel::logicalWidth / 2, maxW = UnitPanel::logicalWidth * 2;
    setResizeLimits(minW, heightForWidth(minW), maxW, heightForWidth(maxW));
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio((double) UnitPanel::logicalWidth / (double) panel.currentLogicalHeight());
}

void MidiverbAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(studioBot);
}

void MidiverbAudioProcessorEditor::resized()
{
    const float scale = (float) getWidth() / (float) UnitPanel::logicalWidth;
    panel.setTransform({});
    panel.setBounds(0, 0, UnitPanel::logicalWidth, panel.currentLogicalHeight());
    panel.setTransform(juce::AffineTransform::scale(scale));

    if (! restoring)
        audioProcessor.getAPVTS().state.setProperty(editorWidthId, getWidth(), nullptr);
}

void MidiverbAudioProcessorEditor::timerCallback()
{
    panel.refresh(audioProcessor.getAndClearInputOverload());
}
