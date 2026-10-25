#include "PluginEditor.h"
#include "Layout.h"

// ============================================================================= help overlay (German, the user's language)
class HelpOverlay : public juce::Component
{
public:
    HelpOverlay() { setAlwaysOnTop (true); }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black.withAlpha (0.82f));
        auto r = getLocalBounds().reduced (getWidth() / 14, getHeight() / 16);
        const float h = (float) getHeight();
        g.setColour (gui::colours::amber);
        g.setFont (gui::Assets::font (h * 0.04f));
        g.drawText ("DUBASCO TAPE ECHO - KURZANLEITUNG", r.removeFromTop (juce::roundToInt (h * 0.06f)), juce::Justification::centredLeft);
        g.setColour (gui::colours::cream);
        g.setFont (gui::Assets::font (h * 0.0205f, false));
        const auto text = juce::String::fromUTF8 (
            "HAUPTPANEL\n"
            "INPUT: Aufnahmepegel aufs Band (5 = 0 dB). Mit der MIC/LINE-Buchse wählst du den Vorverstärker (MIC = mehr Gain und Zerre).\n"
            "DELAY TIME: Bandgeschwindigkeit wie beim RE-201. Beim Drehen pitcht das Echo hoch und runter (Motorträgheit unter TAPE).\n"
            "FEEDBACK: Wiederholungen. Ab etwa 7 geht das Band in die Eigenschwingung, der klassische Dub-Runaway.\n"
            "BASS / TREBLE: Klangregelung im Echo-Kreis, wirkt auf jede Wiederholung. ECHO LEVEL: Lautstärke der Echos.\n"
            "TAPE SAT: Bandsättigung von sauber bis dreckig. POWER aus: Das Band läuft langsam aus (Tape-Stop).\n"
            "VU-METER: anklicken zum Umschalten zwischen IN / ECHO / OUT.\n\n"
            "DUB SECTION\n"
            "ECHO MODE 1-12 wie am Space Echo: 1-4 nur Echo (Köpfe 1, 2, 3, 2+3), 5-11 Echo mit Federhall, 12 nur Hall.\n"
            "SPRING REVERB: Hallanteil und Abklingzeit. KICK ist ein Tritt gegen den Federhall (Crash).\n"
            "TAPE: Wow & Flutter (Gleichlaufschwankungen), Tape Age (Höhenverlust, Rauschen, Dropouts), Inertia (Motorträgheit).\n"
            "TIME: FREE/SYNC. SYNC folgt dem Songtempo, NOTE wählt den Notenwert (T = Triole, D = punktiert).\n"
            "STEREO: MONO (original), WIDE (Köpfe im Panorama verteilt), STEREO (zwei Bänder), PING (Ping-Pong).\n"
            "MIX: Low Cut im Echo-Kreis, Ducking (Echos weichen dem trockenen Signal), Dry-Pegel (0 = nur Effekt), Output.\n"
            "DUB: Mit Schalter THR geht nur Signal ins Echo, solange THROW gedrückt ist (Dub-Throw). Bei ALL lässt THROW das Feedback anschwellen.\n"
            "     HOLD = Endlosschleife: Eingang zu, das Band dreht weiter.\n\n"
            "MIDI LEARN\n"
            "Oben MIDI LEARN anklicken, Regler anklicken, am Controller bewegen. Oder Rechtsklick auf einen Regler.\n"
            "Das Plugin hört direkt auf alle angeschlossenen MIDI-Controller, es ist kein Routing im Host nötig. Die grüne Lampe am MIDI-Knopf\n"
            "zeigt eingehende Daten, im MIDI-Menü steht die letzte empfangene Nachricht. Windows: Ist der Controller in Ableton als Eingang\n"
            "aktiv, ist er für andere Programme gesperrt. Dann in Ableton eine MIDI-Spur mit 'MIDI To' auf das Plugin legen.\n"
            "Mappings werden mit dem Projekt gespeichert. Unter MIDI > 'Save mappings as default' gelten sie für neue Instanzen.\n\n"
            "TIPPS: Doppelklick setzt einen Regler zurück. Das Mausrad funktioniert auf allen Reglern und Wahlschaltern. Klick schließt diese Hilfe.");
        g.drawFittedText (text, r, juce::Justification::topLeft, 60, 1.0f);
    }
    void mouseDown (const juce::MouseEvent&) override { setVisible (false); }
};

// ============================================================================= editor
namespace
{
    const layout::Ctl* findCtl (const char* id)
    {
        for (auto& c : layout::controls)
            if (juce::String (c.id) == id) return &c;
        jassertfalse;
        return nullptr;
    }

    juce::Rectangle<float> squareAt (const layout::Ctl& c, float halfSize)
    {
        return { c.x - halfSize, c.y + layout::topBarH - halfSize, 2 * halfSize, 2 * halfSize };
    }

    float fillFor (const char* sprite)
    {
        const juce::String s (sprite);
        if (s == "knob_large") return layout::knobFillLarge;
        if (s == "knob_medium") return layout::knobFillMedium;
        return layout::knobFillSmall;
    }
}

template <typename T>
T* DubascoEditor::registerControl (std::unique_ptr<T> c, juce::Rectangle<float> rect)
{
    T* raw = c.get();
    addAndMakeVisible (*raw);
    placements.push_back ({ raw, rect });
    if constexpr (std::is_base_of_v<gui::ParamControl, T>)
    {
        raw->onContextMenu = [this] (gui::ParamControl& pc) { showParamMenu (pc); };
        raw->onShowValue = [this] (gui::ParamControl& pc, bool s) { showValue (pc, s); };
        paramControls.push_back (raw);
    }
    owned.push_back (std::move (c));
    return raw;
}

DubascoEditor::DubascoEditor (DubascoProcessor& p)
    : AudioProcessorEditor (&p), proc (p), topBar (p)
{
    const float initialScale = juce::jlimit (0.4f, 1.3f, proc.editorScale.load());
    background.setSource (gui::Assets::image ("background_jpg"));
    addAndMakeVisible (topBar);

    auto& apvts = proc.apvts;

    // ---- moving tape texture (under the reels, which cover the tape ends)
    {
        const juce::Rectangle<float> box (layout::tapeBox[0], layout::tapeBox[1], layout::tapeBox[2] - layout::tapeBox[0], layout::tapeBox[3] - layout::tapeBox[1]);
        std::vector<juce::Point<float>> pts;
        for (auto& p : layout::tapePath) pts.push_back ({ p[0], p[1] });
        tapeRun = registerControl (std::make_unique<gui::TapeRun> (box, pts, layout::tapeW), box.translated (0, (float) layout::topBarH));
    }

    // ---- reels
    for (auto* id : { "reelL", "reelR" })
    {
        auto* c = findCtl (id);
        auto reel = std::make_unique<gui::Reel> (juce::String (id) == "reelL" ? "reel_a_png" : "reel_b_png", layout::reelFill,
                                                 juce::String (id) == "reelL" ? 0.3f : 1.1f);
        auto* r = registerControl (std::move (reel), squareAt (*c, c->r * 1.06f));
        (juce::String (id) == "reelL" ? reelL : reelR) = r;
    }

    // ---- VU needle
    {
        auto* c = findCtl ("vu");
        auto meter = std::make_unique<gui::VUMeter>();
        meter->setPivot ({ (layout::vuPivotX - c->x) / c->w, (layout::vuPivotY - c->y) / c->h }, layout::vuNeedleLen / c->w);
        meter->onClick = [this] { proc.vuSource.store ((proc.vuSource.load() + 1) % 3); };
        vu = registerControl (std::move (meter), { c->x, c->y + layout::topBarH, c->w, c->h });
    }

    // ---- knobs
    for (auto& c : layout::controls)
    {
        if (juce::String (c.type) != "knob") continue;
        auto knob = std::make_unique<gui::FilmKnob> (c.id, c.sprite, fillFor (c.sprite));
        auto* k = registerControl (std::move (knob), squareAt (c, c.r * 1.18f));
        if (auto* param = apvts.getParameter (c.id))
            k->setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
        sliderAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, c.id, *k));
        if (juce::String (c.id) == pid::delayTime) delayKnob = k;
    }

    // ---- lever switches
    struct LeverDef { const char* id; std::vector<float> angles; bool wrap; };
    const std::vector<LeverDef> leverDefs = {
        { pid::mode, { std::begin (layout::modeAngles), std::end (layout::modeAngles) }, false },
        { pid::sync, { std::begin (layout::syncAngles), std::end (layout::syncAngles) }, true },
        { pid::stereoMode, { std::begin (layout::stereoModeAngles), std::end (layout::stereoModeAngles) }, false },
        { pid::sendMode, { std::begin (layout::sendModeAngles), std::end (layout::sendModeAngles) }, true },
    };
    for (auto& d : leverDefs)
    {
        auto* c = findCtl (d.id);
        auto lever = std::make_unique<gui::LeverSwitch> (d.id, d.angles, layout::leverArmToBase, d.wrap);
        lever->attach (*apvts.getParameter (d.id));
        const bool chicken = juce::String (d.id) == pid::mode;
        if (chicken) lever->useChickenhead();
        const float baseW = 2.0f * c->r / 0.98f;
        levers.push_back (registerControl (std::move (lever), squareAt (*c, chicken ? 82.0f : baseW * 0.8f)));
    }

    // ---- power toggle + lamp
    {
        auto* c = findCtl ("power");
        auto t = std::make_unique<gui::BatToggle> (pid::power);
        t->attach (*apvts.getParameter (pid::power));
        powerToggle = registerControl (std::move (t), squareAt (*c, c->r / 0.6f));

        auto* lc = findCtl ("powerLamp");
        auto lamp = std::make_unique<gui::Lamp>();
        lamp->setInterceptsMouseClicks (false, false);
        powerLamp = registerControl (std::move (lamp), squareAt (*lc, lc->r * 1.6f));
        lamps.push_back (powerLamp);
    }

    // ---- head indicator lamps
    for (int i = 0; i < 4; ++i)
    {
        auto* c = findCtl (("headLamp" + juce::String (i)).toRawUTF8());
        auto lamp = std::make_unique<gui::Lamp>();
        lamp->setInterceptsMouseClicks (false, false);
        headLamps[(size_t) i] = registerControl (std::move (lamp), squareAt (*c, c->r * 1.6f));
        lamps.push_back (headLamps[(size_t) i]);
    }

    // ---- lamp push buttons
    for (auto [id, momentary] : { std::pair { pid::kick, true }, std::pair { pid::throwBtn, true }, std::pair { pid::hold, false } })
    {
        auto* c = findCtl (id);
        auto b = std::make_unique<gui::LampButton> (id, momentary);
        b->attach (*apvts.getParameter (id));
        lamps.push_back (registerControl (std::move (b), squareAt (*c, c->r * 1.6f)));
    }

    // ---- MIC / LINE jacks
    {
        auto* m = findCtl ("jackMic");
        auto* l = findCtl ("jackLine");
        const juce::Rectangle<float> area (m->x - 42, m->y + layout::topBarH - 42, (l->x - m->x) + 84, 84);
        auto j = std::make_unique<gui::JackSelector> (pid::inputMode);
        j->setJackCentres ({ 42.0f / area.getWidth(), 0.5f }, { (42.0f + l->x - m->x) / area.getWidth(), 0.5f }, m->r / area.getWidth());
        j->attach (*apvts.getParameter (pid::inputMode));
        registerControl (std::move (j), area);
    }

    // ---- readouts
    for (auto* id : { "tapeReadout", "noteReadout" })
    {
        auto* c = findCtl (id);
        auto* r = registerControl (std::make_unique<gui::Readout>(), { c->x - c->w / 2, c->y + layout::topBarH - c->h / 2, c->w, c->h });
        (juce::String (id) == "tapeReadout" ? tapeReadout : noteReadout) = r;
    }

    // ---- overlays
    addChildComponent (bubble);
    learnOverlay.setControls (paramControls);
    learnOverlay.describe = [this] (const juce::String& id) { return proc.midiLearn.describe (proc.midiLearn.indexOf (id)); };
    learnOverlay.isArmed = [this] (const juce::String& id) { return proc.midiLearn.getArmed() == proc.midiLearn.indexOf (id) && proc.midiLearn.getArmed() >= 0; };
    learnOverlay.onPick = [this] (const juce::String& id)
    {
        const int idx = proc.midiLearn.indexOf (id);
        proc.midiLearn.arm (proc.midiLearn.getArmed() == idx ? -1 : idx);
    };
    addAndMakeVisible (learnOverlay);
    help = std::make_unique<HelpOverlay>();
    addChildComponent (*help);

    topBar.onLearnToggled = [this] (bool on)
    {
        learnOverlay.setActive (on);
        if (! on) proc.midiLearn.arm (-1);
    };
    topBar.onScaleChosen = [this] (float s) { setScale (s); };
    topBar.onHelp = [this] { help->setVisible (! help->isVisible()); help->toFront (false); };

    proc.midiLearn.addChangeListener (this);

    // ---- size
    setResizable (true, true);
    const double aspect = (double) layout::designW / (double) layout::designH;
    getConstrainer()->setFixedAspectRatio (aspect);
    setResizeLimits (juce::roundToInt (layout::designW * 0.4), juce::roundToInt (layout::designH * 0.4),
                     juce::roundToInt (layout::designW * 1.3), juce::roundToInt (layout::designH * 1.3));
    sizeInitialised = true;
    setSize (juce::roundToInt (layout::designW * initialScale), juce::roundToInt (layout::designH * initialScale));

    vblank = std::make_unique<juce::VBlankAttachment> (this, [this] (double ts) { tick (ts); });
}

DubascoEditor::~DubascoEditor()
{
    proc.midiLearn.removeChangeListener (this);
    proc.midiLearn.arm (-1);
}

void DubascoEditor::setScale (float s)
{
    setSize (juce::roundToInt (layout::designW * s), juce::roundToInt (layout::designH * s));
}

void DubascoEditor::paint (juce::Graphics& g)
{
    const float s = (float) getWidth() / (float) layout::designW;
    const float top = layout::topBarH * s;
    const auto area = juce::Rectangle<float> (0, top, (float) getWidth(), (float) getHeight() - top);
    const float phys = g.getInternalContext().getPhysicalPixelScaleFactor();
    const auto& img = background.get (juce::roundToInt (area.getWidth() * phys), juce::roundToInt (area.getHeight() * phys));
    g.drawImage (img, area, juce::RectanglePlacement::stretchToFit);
}

void DubascoEditor::resized()
{
    const float s = (float) getWidth() / (float) layout::designW;
    if (sizeInitialised && getWidth() > 0) proc.editorScale.store (s);
    topBar.setBounds (0, 0, getWidth(), juce::roundToInt (layout::topBarH * s));
    for (auto& [comp, rect] : placements)
        comp->setBounds ((rect * s).getSmallestIntegerContainer());
    learnOverlay.setBounds (getLocalBounds().withTrimmedTop (topBar.getHeight()));
    help->setBounds (getLocalBounds());
    if (bubbleOwner != nullptr) showValue (*bubbleOwner, true);
}

void DubascoEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    learnOverlay.repaint();
}

void DubascoEditor::showValue (gui::ParamControl& pc, bool show)
{
    if (! show || learnOverlay.isActive())
    {
        if (bubbleOwner == &pc || ! show) { bubble.setVisible (false); bubbleOwner = nullptr; }
        return;
    }
    if (auto* param = proc.apvts.getParameter (pc.paramId))
    {
        bubbleOwner = &pc;
        bubble.show (pc.component(), param->getName (40).toUpperCase(), param->getCurrentValueAsText());
        bubble.toFront (false);
    }
}

void DubascoEditor::showParamMenu (gui::ParamControl& pc)
{
    auto* param = proc.apvts.getParameter (pc.paramId);
    if (param == nullptr) return;
    auto& ml = proc.midiLearn;
    const int idx = ml.indexOf (pc.paramId);
    const auto mapping = ml.describe (idx);

    juce::PopupMenu m;
    m.addSectionHeader (param->getName (40));
    m.addItem (1, ml.getArmed() == idx && idx >= 0 ? "Cancel MIDI Learn" : "MIDI Learn", idx >= 0);
    m.addItem (2, "Clear MIDI mapping" + (mapping.isNotEmpty() ? "  (" + mapping + ")" : juce::String()), mapping.isNotEmpty());
    m.addSeparator();
    m.addItem (3, "Reset to default");

    std::unique_ptr<juce::HostProvidedContextMenu> hostMenu;
    if (auto* ctx = getHostContext())
        hostMenu = ctx->getContextMenuForParameter (param);
    if (hostMenu != nullptr)
    {
        m.addSeparator();
        m.addSubMenu ("Host", hostMenu->getEquivalentPopupMenu());
    }

    auto safe = juce::Component::SafePointer<DubascoEditor> (this);
    std::shared_ptr<juce::HostProvidedContextMenu> hm (std::move (hostMenu));
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&pc.component()), [safe, idx, param, hm] (int r)
    {
        if (safe == nullptr) return;
        auto& ml2 = safe->proc.midiLearn;
        if (r == 1) ml2.arm (ml2.getArmed() == idx ? -1 : idx);
        else if (r == 2) ml2.clear (idx);
        else if (r == 3) { param->beginChangeGesture(); param->setValueNotifyingHost (param->getDefaultValue()); param->endChangeGesture(); }
        safe->learnOverlay.repaint();
    });
}

void DubascoEditor::tick (double ts)
{
    const double dt = lastTick > 0 ? juce::jlimit (0.0, 0.1, ts - lastTick) : 1.0 / 60.0;
    lastTick = ts;
    auto& eng = proc.engine;

    // reels: the GUI runs its own copy of the motor model (so the reels also turn when the
    // host is not processing audio), a little visual flutter from the wow/flutter knob
    {
        auto raw = [this] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };
        const bool on = raw (pid::power) > 0.5f;
        double delaySec = raw (pid::delayTime) * 0.001;
        if (raw (pid::sync) > 0.5f)
            delaySec = params::syncNoteBeats (juce::roundToInt (raw (pid::syncNote))) * 60.0 / juce::jlimit (30.0, 300.0, proc.hostBpm.load());
        const double targetLog = std::log (0.25 / juce::jlimit (0.025, 2.0, delaySec));
        const double tau = 0.02 * std::pow (100.0, (double) raw (pid::inertia) / 10.0);
        if (motorLog == 0.0) motorLog = targetLog;
        motorLog += (targetLog - motorLog) * (1.0 - std::exp (-dt / tau));
        motorRamp = juce::jlimit (0.0, 1.0, motorRamp + (on ? dt / 0.45 : -dt / 1.1));
    }
    const float speed = (float) (std::exp (motorLog) * motorRamp);
    const float wf = proc.apvts.getRawParameterValue (pid::wowFlutter)->load() / 10.0f;
    flutterVis = 0.06f * wf * std::sin ((float) ts * 4.1f) + 0.03f * wf * std::sin ((float) ts * 13.7f);
    reelL->tick (dt, speed, flutterVis);
    reelR->tick (dt, speed, -flutterVis * 0.5f);
    tapeRun->tick (dt, speed * (1.0f + flutterVis));

    const int src = proc.vuSource.load();
    vu->sourceLabel = src == 0 ? "IN" : (src == 1 ? "ECHO" : "OUT");
    vu->tick (dt, src == 0 ? eng.meterIn.load() : (src == 1 ? eng.meterEcho.load() : eng.meterOut.load()));

    // lamps
    const bool powerOn = proc.apvts.getRawParameterValue (pid::power)->load() > 0.5f;
    powerLamp->setTarget (powerOn ? 1.0f : 0.0f);
    bool h[3], rev;
    dub::TapeEchoEngine::modeHeads (juce::roundToInt (proc.apvts.getRawParameterValue (pid::mode)->load()) + 1, h[0], h[1], h[2], rev);
    const float echoAct = juce::jlimit (0.0f, 1.0f, eng.meterEcho.load() * 6.0f);
    for (int i = 0; i < 3; ++i)
        headLamps[(size_t) i]->setTarget (h[i] && powerOn ? 0.45f + 0.55f * echoAct : 0.0f);
    headLamps[3]->setTarget (rev && powerOn ? 0.85f : 0.0f);
    for (auto* l : lamps) l->tick (dt);
    for (auto* l : levers) l->tick (dt);
    powerToggle->tick (dt);
    learnOverlay.tick (dt);

    const bool sync = proc.apvts.getRawParameterValue (pid::sync)->load() > 0.5f;
    delayKnob->setDimmed (sync);

    readoutTimer += dt;
    if (readoutTimer > 0.08)
    {
        readoutTimer = 0;
        const float ms = speed > 0.0001f ? 250.0f / speed : 0.0f;
        tapeReadout->setText (speed < 0.01f ? juce::String ("MOTOR STOPPED")
                                            : juce::String (speed, 2) + "x  |  " + juce::String (juce::roundToInt (ms)) + " ms");
        const int note = juce::roundToInt (proc.apvts.getRawParameterValue (pid::syncNote)->load());
        noteReadout->setText (sync ? params::syncNoteNames()[note] : juce::String ("FREE"));
        if (bubbleOwner != nullptr && bubble.isVisible()) showValue (*bubbleOwner, true);
    }
}
