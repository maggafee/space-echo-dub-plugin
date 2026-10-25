#pragma once
#include <JuceHeader.h>
#include <functional>

namespace gui
{
// ----------------------------------------------------------------------------- assets
struct Assets
{
    static juce::Image image (const char* resourceName);
    static juce::Font font (float height, bool semiBold = true);
};

namespace colours
{
    const juce::Colour cream { 0xffeac48e };
    const juce::Colour amber { 0xffffb347 };
    const juce::Colour lcdBg { 0xff15110c };
    const juce::Colour panel { 0xff1d1b17 };
    const juce::Colour learn { 0xff52e07a };
}

// keeps a high quality rescaled copy of an image for the current physical size
class ScaledImage
{
public:
    void setSource (juce::Image s) { src = std::move (s); cached = {}; }
    const juce::Image& get (int pxW, int pxH);
    const juce::Image& source() const { return src; }

private:
    juce::Image src, cached;
};

// draws image centred at (cx,cy) with a given drawn width, rotated by angle (radians)
void drawRotated (juce::Graphics& g, ScaledImage& img, float cx, float cy, float drawnW, float angle, float alpha = 1.0f);

// ----------------------------------------------------------------------------- base for parameter controls
class ParamControl
{
public:
    explicit ParamControl (juce::String id) : paramId (std::move (id)) {}
    virtual ~ParamControl() = default;
    virtual juce::Component& component() = 0;

    juce::String paramId;
    std::function<void (ParamControl&)> onContextMenu;
    std::function<void (ParamControl&, bool show)> onShowValue;

protected:
    bool handlePopup (const juce::MouseEvent& e)
    {
        if (e.mods.isPopupMenu()) { if (onContextMenu) onContextMenu (*this); return true; }
        return false;
    }
};

// ----------------------------------------------------------------------------- knob
class FilmKnob : public juce::Slider, public ParamControl
{
public:
    FilmKnob (const juce::String& paramId, const char* sprite, float fillRatio);
    juce::Component& component() override { return *this; }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    bool hitTest (int x, int y) override;
    void setDimmed (bool d) { if (d != dimmed) { dimmed = d; repaint(); } }
    float knobRadius() const { return getWidth() * 0.5f / 1.18f; }

private:
    ScaledImage img;
    float fill;
    bool dimmed = false, dragging = false;
};

// ----------------------------------------------------------------------------- rotary lever switch
class LeverSwitch : public juce::Component, public ParamControl
{
public:
    LeverSwitch (const juce::String& paramId, std::vector<float> anglesDeg, float armToBase, bool wrap);
    juce::Component& component() override { return *this; }

    void attach (juce::RangedAudioParameter& p);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override { if (onShowValue) onShowValue (*this, true); }
    void mouseExit (const juce::MouseEvent&) override { if (onShowValue) onShowValue (*this, false); }
    void tick (double dt);
    int getIndex() const { return index; }

private:
    void setIndex (int i, bool gesture);
    std::vector<float> angles;
    float armRatio;
    bool wrap;
    int index = 0;
    float shownAngle = 0, dragStartIndex = 0;
    ScaledImage base, arm;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    juce::RangedAudioParameter* param = nullptr;
};

// ----------------------------------------------------------------------------- bat toggle (power)
class BatToggle : public juce::Component, public ParamControl
{
public:
    explicit BatToggle (const juce::String& paramId);
    juce::Component& component() override { return *this; }
    void attach (juce::RangedAudioParameter& p);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void tick (double dt);

private:
    bool on = true;
    float shown = 1.0f;     // 1 = up/on, -1 = down/off
    ScaledImage bat, nut;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

// ----------------------------------------------------------------------------- lamp (indicator) & lamp push button
class Lamp : public juce::Component
{
public:
    Lamp();
    void paint (juce::Graphics&) override;
    void setTarget (float b) { target = juce::jlimit (0.0f, 1.0f, b); }
    void tick (double dt);
    float lensFraction = 0.66f;

protected:
    float brightness = 0, target = 0;
    ScaledImage off, on;
};

class LampButton : public Lamp, public ParamControl
{
public:
    LampButton (const juce::String& paramId, bool momentary);
    juce::Component& component() override { return *this; }
    void attach (juce::RangedAudioParameter& p);
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void paint (juce::Graphics&) override;

private:
    bool momentary, state = false, pressed = false;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

// ----------------------------------------------------------------------------- MIC / LINE jack selector
class JackSelector : public juce::Component, public ParamControl
{
public:
    explicit JackSelector (const juce::String& paramId);
    juce::Component& component() override { return *this; }
    void attach (juce::RangedAudioParameter& p);
    void setJackCentres (juce::Point<float> micRel, juce::Point<float> lineRel, float radiusRel) { mic = micRel; line = lineRel; rad = radiusRel; }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool hitTest (int x, int y) override;

private:
    juce::Point<float> mic, line; float rad = 0.1f;
    int value = 1;   // 0 mic, 1 line
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

// ----------------------------------------------------------------------------- tape reel
class Reel : public juce::Component
{
public:
    Reel (const char* sprite, float fillRatio, float startAngle);
    void paint (juce::Graphics&) override;
    void tick (double dt, float speed, float flutter);
    bool hitTest (int, int) override { return false; }

private:
    ScaledImage img;
    float fill, angle;
};

// ----------------------------------------------------------------------------- VU meter needle
class VUMeter : public juce::Component
{
public:
    VUMeter();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void tick (double dt, float rmsLevel);
    void setPivot (juce::Point<float> rel, float lenRel) { pivot = rel; len = lenRel; }
    std::function<void()> onClick;
    juce::String sourceLabel { "IN" };

private:
    float angleForDb (float db) const;
    juce::Point<float> pivot; float len = 0.5f;
    float level = 0, needle = -60.0f, peakHold = 0;
};

// ----------------------------------------------------------------------------- small amber LCD readout
class Readout : public juce::Component
{
public:
    void setText (const juce::String& t) { if (t != text) { text = t; repaint(); } }
    void paint (juce::Graphics&) override;
    bool hitTest (int, int) override { return false; }

private:
    juce::String text;
};

// ----------------------------------------------------------------------------- floating value bubble
class ValueBubble : public juce::Component
{
public:
    ValueBubble() { setInterceptsMouseClicks (false, false); setAlwaysOnTop (true); }
    void show (juce::Component& target, const juce::String& title, const juce::String& value);
    void paint (juce::Graphics&) override;

private:
    juce::String title, value;
};

// ----------------------------------------------------------------------------- MIDI learn overlay
class MidiLearnOverlay : public juce::Component
{
public:
    MidiLearnOverlay() { setInterceptsMouseClicks (false, false); }
    void setActive (bool a);
    bool isActive() const { return active; }
    void setControls (std::vector<ParamControl*> c) { controls = std::move (c); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void tick (double dt) { blink += (float) dt; if (active) repaint(); }

    std::function<juce::String (const juce::String& paramId)> describe;
    std::function<bool (const juce::String& paramId)> isArmed;
    std::function<void (const juce::String& paramId)> onPick;

private:
    bool active = false;
    float blink = 0;
    std::vector<ParamControl*> controls;
};
} // namespace gui
