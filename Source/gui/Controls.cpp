#include "Controls.h"
#include "BinaryData.h"
#include "Layout.h"

namespace gui
{
// ============================================================================= assets
juce::Image Assets::image (const char* resourceName)
{
    int size = 0;
    if (auto* data = BinaryData::getNamedResource (resourceName, size))
        return juce::ImageCache::getFromMemory (data, size);
    jassertfalse;
    return {};
}

juce::Font Assets::font (float height, bool semiBold)
{
    static juce::Typeface::Ptr semi = juce::Typeface::createSystemTypefaceFor (BinaryData::OswaldSemiBold_ttf, BinaryData::OswaldSemiBold_ttfSize);
    static juce::Typeface::Ptr med = juce::Typeface::createSystemTypefaceFor (BinaryData::OswaldMedium_ttf, BinaryData::OswaldMedium_ttfSize);
    return juce::Font (juce::FontOptions (semiBold ? semi : med).withHeight (height));
}

const juce::Image& ScaledImage::get (int pxW, int pxH)
{
    pxW = juce::jmax (1, pxW); pxH = juce::jmax (1, pxH);
    if (cached.isNull() || cached.getWidth() != pxW || cached.getHeight() != pxH)
        cached = src.isValid() ? src.rescaled (pxW, pxH, juce::Graphics::highResamplingQuality) : juce::Image();
    return cached;
}

void drawRotated (juce::Graphics& g, ScaledImage& img, float cx, float cy, float drawnW, float angle, float alpha)
{
    if (! img.source().isValid()) return;
    const float phys = g.getInternalContext().getPhysicalPixelScaleFactor();
    const float aspect = (float) img.source().getHeight() / (float) img.source().getWidth();
    const int pw = juce::roundToInt (drawnW * phys), ph = juce::roundToInt (drawnW * aspect * phys);
    const auto& im = img.get (pw, ph);
    const float s = 1.0f / phys;
    g.setOpacity (alpha);
    g.drawImageTransformed (im, juce::AffineTransform::translation (-pw * 0.5f, -ph * 0.5f)
                                     .scaled (s)
                                     .rotated (angle)
                                     .translated (cx, cy));
    g.setOpacity (1.0f);
}

static void softShadow (juce::Graphics& g, juce::Point<float> c, float r, float dy, float strength = 0.55f)
{
    juce::ColourGradient grad (juce::Colours::black.withAlpha (strength), c.x, c.y + dy,
                               juce::Colours::transparentBlack, c.x + r * 1.18f, c.y + dy, true);
    grad.addColour (0.72, juce::Colours::black.withAlpha (strength * 0.75f));
    g.setGradientFill (grad);
    g.fillEllipse (c.x - r * 1.18f, c.y + dy - r * 1.18f, r * 2.36f, r * 2.36f);
}

// ============================================================================= FilmKnob
FilmKnob::FilmKnob (const juce::String& id, const char* sprite, float fillRatio)
    : ParamControl (id), fill (fillRatio)
{
    img.setSource (Assets::image ((juce::String (sprite) + "_png").toRawUTF8()));
    setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    setRotaryParameters (juce::degreesToRadians (layout::knobStartDeg), juce::degreesToRadians (layout::knobEndDeg), true);
    setMouseDragSensitivity (260);
    setScrollWheelEnabled (true);
    setPopupMenuEnabled (false);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    onValueChange = [this] { if (onShowValue && (dragging || isMouseOver())) onShowValue (*this, true); };
}

bool FilmKnob::hitTest (int x, int y)
{
    return getLocalBounds().toFloat().getCentre().getDistanceFrom ({ (float) x, (float) y }) <= knobRadius() * 1.05f;
}

void FilmKnob::paint (juce::Graphics& g)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    const float r = knobRadius();
    softShadow (g, c, r, r * 0.08f);
    const double prop = valueToProportionOfLength (getValue());
    const float ang = juce::degreesToRadians (layout::knobStartDeg + (float) prop * (layout::knobEndDeg - layout::knobStartDeg));
    drawRotated (g, img, c.x, c.y, 2.0f * r / fill, ang, dimmed ? 0.55f : 1.0f);
    if (dimmed)
    {
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
    }
}

void FilmKnob::mouseDown (const juce::MouseEvent& e)
{
    if (handlePopup (e)) return;
    dragging = true;
    if (onShowValue) onShowValue (*this, true);
    Slider::mouseDown (e);
}

void FilmKnob::mouseUp (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    dragging = false;
    Slider::mouseUp (e);
    if (onShowValue) onShowValue (*this, isMouseOver());
}

void FilmKnob::mouseEnter (const juce::MouseEvent& e) { Slider::mouseEnter (e); if (onShowValue) onShowValue (*this, true); }
void FilmKnob::mouseExit (const juce::MouseEvent& e)  { Slider::mouseExit (e); if (onShowValue && ! dragging) onShowValue (*this, false); }

// ============================================================================= LeverSwitch
LeverSwitch::LeverSwitch (const juce::String& id, std::vector<float> anglesDeg, float armToBase, bool shouldWrap)
    : ParamControl (id), angles (std::move (anglesDeg)), armRatio (armToBase), wrap (shouldWrap)
{
    base.setSource (Assets::image ("lever_base_png"));
    arm.setSource (Assets::image ("lever_arm_png"));
    shownAngle = angles.front();
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void LeverSwitch::attach (juce::RangedAudioParameter& p)
{
    param = &p;
    attachment = std::make_unique<juce::ParameterAttachment> (p, [this] (float v)
    {
        index = juce::jlimit (0, (int) angles.size() - 1, juce::roundToInt (v));
        repaint();
    });
    attachment->sendInitialUpdate();
    shownAngle = angles[(size_t) index];
}

void LeverSwitch::setIndex (int i, bool /*gesture*/)
{
    const int n = (int) angles.size();
    i = wrap ? ((i % n) + n) % n : juce::jlimit (0, n - 1, i);
    if (attachment) attachment->setValueAsCompleteGesture ((float) i);
}

void LeverSwitch::paint (juce::Graphics& g)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    const float baseW = (float) getWidth() / 1.6f;
    softShadow (g, c, baseW * 0.48f, baseW * 0.05f, 0.5f);
    drawRotated (g, base, c.x, c.y, baseW, 0.0f);
    // arm sprite points down (180 deg) in the artwork
    {
        // fake shadow of the arm
        juce::Graphics::ScopedSaveState s (g);
        const float a = juce::degreesToRadians (shownAngle);
        const float L = baseW * 0.55f;
        juce::Path p;
        p.startNewSubPath (c);
        p.lineTo (c.x + std::sin (a) * L + 2.0f, c.y - std::cos (a) * L + 4.0f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.strokePath (p, juce::PathStrokeType (baseW * 0.12f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    drawRotated (g, arm, c.x, c.y, baseW * armRatio, juce::degreesToRadians (shownAngle - 180.0f));
}

void LeverSwitch::tick (double dt)
{
    const float target = angles[(size_t) index];
    if (std::abs (target - shownAngle) > 0.05f)
    {
        shownAngle += (target - shownAngle) * (float) (1.0 - std::exp (-dt * 22.0));
        repaint();
    }
    else if (shownAngle != target) { shownAngle = target; repaint(); }
}

void LeverSwitch::mouseDown (const juce::MouseEvent& e)
{
    if (handlePopup (e)) return;
    dragStartIndex = (float) index;
}

void LeverSwitch::mouseDrag (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    const float steps = (float) (e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY()) / juce::jmax (8.0f, getWidth() * 0.22f);
    const int n = (int) angles.size();
    const int target = juce::jlimit (0, n - 1, juce::roundToInt (dragStartIndex + steps));
    if (target != index && attachment) attachment->setValueAsCompleteGesture ((float) target);
}

void LeverSwitch::mouseUp (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || e.mouseWasDraggedSinceMouseDown()) return;
    // click: left half = previous, right half = next (2-position switches simply flip)
    if (angles.size() == 2) { setIndex (1 - index, true); return; }
    const bool right = e.position.x >= getWidth() * 0.5f;
    setIndex (index + (right ? 1 : -1), true);
}

void LeverSwitch::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const float d = w.deltaY != 0 ? w.deltaY : -w.deltaX;
    if (d == 0) return;
    const int n = (int) angles.size();
    setIndex (juce::jlimit (0, n - 1, index + (d > 0 ? 1 : -1)), true);
}

// ============================================================================= BatToggle
BatToggle::BatToggle (const juce::String& id) : ParamControl (id)
{
    bat.setSource (Assets::image ("toggle_bat_png"));
    nut.setSource (Assets::image ("toggle_nut_png"));
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void BatToggle::attach (juce::RangedAudioParameter& p)
{
    attachment = std::make_unique<juce::ParameterAttachment> (p, [this] (float v) { on = v > 0.5f; repaint(); });
    attachment->sendInitialUpdate();
    shown = on ? 1.0f : -1.0f;
}

void BatToggle::paint (juce::Graphics& g)
{
    // sprite square: pivot at centre. Draw nut, then the bat scaled vertically (-1 = pointing down).
    const auto c = getLocalBounds().toFloat().getCentre();
    const float w = (float) getWidth();
    const float phys = g.getInternalContext().getPhysicalPixelScaleFactor();
    softShadow (g, c.translated (0, w * 0.12f), w * 0.22f, 0, 0.5f);
    drawRotated (g, nut, c.x, c.y, w, 0.0f);

    const int pw = juce::roundToInt (w * phys);
    const auto& im = bat.get (pw, pw);
    const float s = 1.0f / phys;
    const float sy = std::abs (shown) < 0.12f ? (shown < 0 ? -0.12f : 0.12f) : shown;
    auto t = juce::AffineTransform::translation (-pw * 0.5f, -pw * 0.5f).scaled (s, s * sy * (sy < 0 ? 0.82f : 1.0f)).translated (c.x, c.y);
    if (sy < 0)
    {
        // shadow below the flipped bat
        g.setOpacity (0.35f);
        g.drawImageTransformed (im, t.translated (2.0f, 3.0f));
    }
    g.setOpacity (1.0f);
    g.drawImageTransformed (im, t);
}

void BatToggle::tick (double dt)
{
    const float target = on ? 1.0f : -1.0f;
    if (std::abs (target - shown) > 0.002f)
    {
        shown += (target - shown) * (float) (1.0 - std::exp (-dt * 26.0));
        repaint();
    }
}

void BatToggle::mouseDown (const juce::MouseEvent& e)
{
    if (handlePopup (e)) return;
    if (attachment) attachment->setValueAsCompleteGesture (on ? 0.0f : 1.0f);
}

// ============================================================================= Lamp
Lamp::Lamp()
{
    off.setSource (Assets::image ("lamp_off_png"));
    on.setSource (Assets::image ("lamp_on_png"));
}

void Lamp::paint (juce::Graphics& g)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    const float w = (float) juce::jmin (getWidth(), getHeight()) / 1.6f;   // component has room for the glow
    if (brightness > 0.01f)
    {
        juce::ColourGradient glow (juce::Colour (0xffff8a1a).withAlpha (0.55f * brightness), c.x, c.y,
                                   juce::Colour (0x00ff6a00), c.x + w * 0.8f, c.y, true);
        g.setGradientFill (glow);
        g.fillEllipse (c.x - w * 0.8f, c.y - w * 0.8f, w * 1.6f, w * 1.6f);
    }
    softShadow (g, c, w * 0.5f, w * 0.05f, 0.45f);
    drawRotated (g, off, c.x, c.y, w, 0.0f);
    if (brightness > 0.003f)
        drawRotated (g, on, c.x, c.y, w, 0.0f, brightness);
}

void Lamp::tick (double dt)
{
    if (std::abs (target - brightness) > 0.002f)
    {
        const double rate = target > brightness ? 30.0 : 9.0;   // incandescent: fast on, slow off
        brightness += (target - brightness) * (float) (1.0 - std::exp (-dt * rate));
        repaint();
    }
}

LampButton::LampButton (const juce::String& id, bool isMomentary) : ParamControl (id), momentary (isMomentary)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void LampButton::attach (juce::RangedAudioParameter& p)
{
    attachment = std::make_unique<juce::ParameterAttachment> (p, [this] (float v) { state = v > 0.5f; setTarget (state ? 1.0f : 0.0f); });
    attachment->sendInitialUpdate();
}

void LampButton::paint (juce::Graphics& g)
{
    const float press = pressed ? 0.94f : 1.0f;
    if (press != 1.0f)
    {
        const auto c = getLocalBounds().toFloat().getCentre();
        g.addTransform (juce::AffineTransform::scale (press, press, c.x, c.y));
    }
    Lamp::paint (g);
}

void LampButton::mouseDown (const juce::MouseEvent& e)
{
    if (handlePopup (e)) return;
    pressed = true;
    if (attachment)
    {
        if (momentary) attachment->setValueAsCompleteGesture (1.0f);
        else attachment->setValueAsCompleteGesture (state ? 0.0f : 1.0f);
    }
    repaint();
}

void LampButton::mouseUp (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    pressed = false;
    if (momentary && attachment) attachment->setValueAsCompleteGesture (0.0f);
    repaint();
}

// ============================================================================= JackSelector
JackSelector::JackSelector (const juce::String& id) : ParamControl (id)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void JackSelector::attach (juce::RangedAudioParameter& p)
{
    attachment = std::make_unique<juce::ParameterAttachment> (p, [this] (float v) { value = juce::roundToInt (v); repaint(); });
    attachment->sendInitialUpdate();
}

bool JackSelector::hitTest (int x, int y)
{
    const juce::Point<float> pt ((float) x / getWidth(), (float) y / getHeight());
    return pt.getDistanceFrom (mic) < rad * 1.4f || pt.getDistanceFrom (line) < rad * 1.4f;
}

void JackSelector::mouseDown (const juce::MouseEvent& e)
{
    if (handlePopup (e)) return;
    const juce::Point<float> pt (e.position.x / getWidth(), e.position.y / getHeight());
    const int v = pt.getDistanceFrom (mic) < pt.getDistanceFrom (line) ? 0 : 1;
    if (attachment) attachment->setValueAsCompleteGesture ((float) v);
}

void JackSelector::paint (juce::Graphics& g)
{
    // a 1/4" plug sitting in the selected jack, seen from the front
    const auto p = (value == 0 ? mic : line);
    const float cx = p.x * getWidth(), cy = p.y * getHeight(), r = rad * getWidth();
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (cx - r * 0.95f + 2, cy - r * 0.95f + 4, r * 1.9f, r * 1.9f);
    juce::ColourGradient boot (juce::Colour (0xff3a3a3a), cx - r * 0.4f, cy - r * 0.5f, juce::Colour (0xff050505), cx + r * 0.5f, cy + r * 0.7f, true);
    g.setGradientFill (boot);
    g.fillEllipse (cx - r * 0.86f, cy - r * 0.86f, r * 1.72f, r * 1.72f);
    // knurled rings
    g.setColour (juce::Colour (0xff6b6b6b).withAlpha (0.6f));
    g.drawEllipse (cx - r * 0.64f, cy - r * 0.64f, r * 1.28f, r * 1.28f, juce::jmax (1.0f, r * 0.06f));
    // cable stub
    juce::ColourGradient cab (juce::Colour (0xff242424), cx - r * 0.3f, cy, juce::Colour (0xff0a0a0a), cx + r * 0.3f, cy, false);
    g.setGradientFill (cab);
    g.fillEllipse (cx - r * 0.38f, cy - r * 0.38f, r * 0.76f, r * 0.76f);
    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.fillEllipse (cx - r * 0.5f, cy - r * 0.62f, r * 0.5f, r * 0.28f);
    // red indicator ring around the active jack
    g.setColour (colours::amber.withAlpha (0.55f));
    g.drawEllipse (cx - r * 1.12f, cy - r * 1.12f, r * 2.24f, r * 2.24f, juce::jmax (1.0f, r * 0.07f));
}

// ============================================================================= Reel
Reel::Reel (const char* sprite, float fillRatio, float startAngle) : fill (fillRatio), angle (startAngle)
{
    img.setSource (Assets::image (sprite));
    setInterceptsMouseClicks (false, false);
}

void Reel::paint (juce::Graphics& g)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    const float r = getWidth() * 0.5f / 1.06f;
    softShadow (g, c, r, r * 0.03f, 0.6f);
    drawRotated (g, img, c.x, c.y, 2.0f * r / fill, angle);
}

void Reel::tick (double dt, float speed, float flutter)
{
    if (speed <= 0.0001f) return;
    // ~0.75 rev/s at nominal tape speed; tiny irregularity from wow & flutter
    angle += (float) (dt * speed * 0.75 * juce::MathConstants<double>::twoPi) * (1.0f + flutter);
    if (angle > juce::MathConstants<float>::twoPi) angle -= juce::MathConstants<float>::twoPi;
    repaint();
}

// ============================================================================= VU meter
VUMeter::VUMeter() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

float VUMeter::angleForDb (float db) const
{
    const int n = (int) std::size (layout::vuDb);
    if (db <= layout::vuDb[0])
    {
        // below -20: continue down to the rest position
        const float t = juce::jlimit (0.0f, 1.0f, (layout::vuDb[0] - db) / 20.0f);
        return layout::vuDeg[0] - 6.0f * t;
    }
    for (int i = 1; i < n; ++i)
        if (db <= layout::vuDb[i])
        {
            const float t = (db - layout::vuDb[i - 1]) / (layout::vuDb[i] - layout::vuDb[i - 1]);
            return layout::vuDeg[i - 1] + t * (layout::vuDeg[i] - layout::vuDeg[i - 1]);
        }
    return layout::vuDeg[n - 1] + 3.0f;   // pinned
}

void VUMeter::tick (double dt, float rms)
{
    // VU ballistics: ~300 ms integration, slight overshoot via 2nd order response
    const float db = juce::Decibels::gainToDecibels (rms * 1.41f, -60.0f) + 6.0f;   // 0 VU ~ -6 dBFS RMS
    const float a = (float) (1.0 - std::exp (-dt / 0.065));
    level += (db - level) * a;
    const float prev = needle;
    needle += (level - needle) * (float) (1.0 - std::exp (-dt / 0.09));
    if (std::abs (needle - prev) > 0.01f) repaint();
}

void VUMeter::paint (juce::Graphics& g)
{
    const float w = (float) getWidth(), h = (float) getHeight();
    const juce::Point<float> pv (pivot.x * w, pivot.y * h);
    const float L = len * w;
    const float a = juce::degreesToRadians (angleForDb (needle));
    const juce::Point<float> tip (pv.x + std::sin (a) * L, pv.y - std::cos (a) * L);
    const juce::Point<float> tail (pv.x + std::sin (a) * L * 0.22f, pv.y - std::cos (a) * L * 0.22f);

    g.reduceClipRegion (getLocalBounds());
    // shadow
    juce::Path sh; sh.startNewSubPath (tail.translated (3, 4)); sh.lineTo (tip.translated (3, 4));
    g.setColour (juce::Colours::black.withAlpha (0.18f));
    g.strokePath (sh, juce::PathStrokeType (juce::jmax (1.5f, w * 0.008f)));
    // needle
    juce::Path n; n.startNewSubPath (tail); n.lineTo (tip);
    g.setColour (juce::Colour (0xff15120e));
    g.strokePath (n, juce::PathStrokeType (juce::jmax (1.2f, w * 0.0055f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // source label in the corner
    g.setFont (Assets::font (h * 0.13f, false));
    g.setColour (juce::Colour (0xff3b2a17).withAlpha (0.75f));
    g.drawText (sourceLabel, juce::Rectangle<float> (w * 0.03f, h * 0.70f, w * 0.2f, h * 0.18f), juce::Justification::centredLeft);
}

void VUMeter::mouseDown (const juce::MouseEvent&) { if (onClick) onClick(); }

// ============================================================================= Readout
void Readout::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (r, r.getHeight() * 0.2f);
    g.setColour (colours::lcdBg);
    g.fillRoundedRectangle (r.reduced (1.0f), r.getHeight() * 0.18f);
    g.setColour (juce::Colour (0xff4a3a24));
    g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.2f, 1.0f);
    g.setFont (Assets::font (r.getHeight() * 0.72f, false));
    g.setColour (colours::amber.withAlpha (0.25f));
    g.drawText (text, r.translated (0, 0.5f), juce::Justification::centred);   // glow
    g.setColour (colours::amber);
    g.drawText (text, r, juce::Justification::centred);
}

// ============================================================================= ValueBubble
void ValueBubble::show (juce::Component& target, const juce::String& t, const juce::String& v)
{
    title = t; value = v;
    auto* parent = getParentComponent();
    if (parent == nullptr) return;
    const auto tb = parent->getLocalArea (&target, target.getLocalBounds());
    const float hgt = juce::jmax (22.0f, parent->getHeight() * 0.032f);
    const auto f1 = Assets::font (hgt * 0.42f, false), f2 = Assets::font (hgt * 0.62f);
    const float wTxt = juce::jmax (juce::GlyphArrangement::getStringWidth (f1, title), juce::GlyphArrangement::getStringWidth (f2, value));
    const int w = juce::roundToInt (wTxt + hgt * 0.9f);
    const int h = juce::roundToInt (hgt * 1.25f);
    auto b = juce::Rectangle<int> (w, h).withCentre ({ tb.getCentreX(), tb.getY() - h / 2 - 4 });
    if (b.getY() < 0) b.setY (tb.getBottom() + 4);
    b = b.constrainedWithin (parent->getLocalBounds());
    setBounds (b);
    setVisible (true);
    repaint();
}

void ValueBubble::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (r.translated (1.5f, 2.0f), 5.0f);
    g.setColour (juce::Colour (0xf01a1612));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (colours::cream.withAlpha (0.55f));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
    const float h = r.getHeight();
    g.setColour (colours::cream.withAlpha (0.7f));
    g.setFont (Assets::font (h * 0.34f, false));
    g.drawText (title, r.withHeight (h * 0.42f).translated (0, h * 0.04f), juce::Justification::centred);
    g.setColour (colours::amber);
    g.setFont (Assets::font (h * 0.5f));
    g.drawText (value, r.withTrimmedTop (h * 0.40f), juce::Justification::centred);
}

// ============================================================================= MidiLearnOverlay
void MidiLearnOverlay::setActive (bool a)
{
    active = a;
    setInterceptsMouseClicks (a, false);
    repaint();
}

void MidiLearnOverlay::paint (juce::Graphics& g)
{
    if (! active) return;
    g.fillAll (juce::Colours::black.withAlpha (0.18f));
    const float pulse = 0.5f + 0.5f * std::sin (blink * 8.0f);
    for (auto* c : controls)
    {
        auto& comp = c->component();
        if (! comp.isVisible()) continue;
        auto b = getLocalArea (&comp, comp.getLocalBounds()).toFloat();
        const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
        const auto ctr = b.getCentre();
        const bool armed = isArmed && isArmed (c->paramId);
        const auto txt = describe ? describe (c->paramId) : juce::String();
        juce::Colour col = armed ? juce::Colours::orange.withAlpha (0.35f + 0.65f * pulse)
                                 : (txt.isNotEmpty() ? colours::learn : colours::learn.withAlpha (0.45f));
        g.setColour (col.withMultipliedAlpha (0.18f));
        g.fillEllipse (ctr.x - r, ctr.y - r, 2 * r, 2 * r);
        g.setColour (col);
        g.drawEllipse (ctr.x - r, ctr.y - r, 2 * r, 2 * r, armed ? 3.0f : 2.0f);
        const auto label = armed ? juce::String ("move a control...") : txt.upToFirstOccurrenceOf (" ·", false, false);
        if (label.isNotEmpty())
        {
            const float fh = juce::jmax (11.0f, getHeight() * 0.016f);
            g.setFont (Assets::font (fh));
            const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), label) + fh;
            juce::Rectangle<float> tag (tw, fh * 1.4f);
            tag.setCentre (ctr.x, ctr.y + r * 0.62f);
            g.setColour (juce::Colours::black.withAlpha (0.8f));
            g.fillRoundedRectangle (tag, 4.0f);
            g.setColour (armed ? juce::Colours::orange : colours::learn);
            g.drawText (label, tag, juce::Justification::centred);
        }
    }
    g.setColour (colours::learn);
    g.setFont (Assets::font (juce::jmax (13.0f, getHeight() * 0.02f)));
    g.drawText ("MIDI LEARN: click a control, then move a knob/fader/pad on your controller. Right-click a control to clear it.",
                getLocalBounds().removeFromBottom (juce::roundToInt (getHeight() * 0.035f)), juce::Justification::centred);
}

void MidiLearnOverlay::mouseDown (const juce::MouseEvent& e)
{
    for (auto* c : controls)
    {
        auto& comp = c->component();
        auto b = getLocalArea (&comp, comp.getLocalBounds());
        if (b.contains (e.getPosition()))
        {
            if (e.mods.isPopupMenu()) { if (c->onContextMenu) c->onContextMenu (*c); }
            else if (onPick) onPick (c->paramId);
            repaint();
            return;
        }
    }
}
} // namespace gui
