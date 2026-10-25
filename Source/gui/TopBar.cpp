#include "TopBar.h"
#include "Controls.h"
#include "../PluginProcessor.h"

namespace gui
{
// ============================================================================= look and feel
TopBarLookAndFeel::TopBarLookAndFeel()
{
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1b1814));
    setColour (juce::PopupMenu::textColourId, colours::cream);
    setColour (juce::PopupMenu::headerTextColourId, colours::amber);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff8a1a13));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::AlertWindow::backgroundColourId, juce::Colour (0xff1b1814));
    setColour (juce::AlertWindow::textColourId, colours::cream);
    setColour (juce::AlertWindow::outlineColourId, colours::cream.withAlpha (0.4f));
    setColour (juce::TextEditor::backgroundColourId, colours::lcdBg);
    setColour (juce::TextEditor::textColourId, colours::amber);
    setColour (juce::TextEditor::outlineColourId, colours::cream.withAlpha (0.3f));
    setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a251f));
    setColour (juce::TextButton::textColourOffId, colours::cream);
}

void TopBarLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    juce::Colour base = on ? juce::Colour (0xff8a1a13) : juce::Colour (0xff2a251f);
    if (b.getProperties().contains ("learn") && on) base = juce::Colour (0xff1f6b35);
    if (over) base = base.brighter (0.15f);
    if (down) base = base.darker (0.2f);
    juce::ColourGradient grad (base.brighter (0.12f), r.getX(), r.getY(), base.darker (0.25f), r.getX(), r.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    g.setColour (colours::cream.withAlpha (0.12f));
    g.drawLine (r.getX() + 4, r.getY() + 1.0f, r.getRight() - 4, r.getY() + 1.0f, 1.0f);
}

void TopBarLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (Assets::font (b.getHeight() * 0.5f));
    g.setColour (b.getToggleState() ? juce::Colours::white : colours::cream);
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (6, 0), juce::Justification::centred, 1);
    if (b.getProperties().contains ("midiLed"))
    {
        // small activity LED: lights up whenever MIDI arrives (from the host or directly from a device)
        const float led = (float) b.getProperties()["midiLed"];
        const float d = b.getHeight() * 0.22f;
        const juce::Rectangle<float> r (b.getHeight() * 0.3f, (b.getHeight() - d) * 0.5f, d, d);
        g.setColour (juce::Colour (0xff12331c).interpolatedWith (colours::learn, led));
        g.fillEllipse (r);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawEllipse (r, 1.0f);
    }
}

juce::Font TopBarLookAndFeel::getPopupMenuFont() { return Assets::font (17.0f, false); }

void TopBarLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (findColour (juce::PopupMenu::backgroundColourId));
    g.setColour (colours::cream.withAlpha (0.25f));
    g.drawRect (0, 0, w, h);
}

// ============================================================================= TopBar
TopBar::TopBar (DubascoProcessor& p) : proc (p)
{
    setLookAndFeel (&lnf);
    for (auto* b : { &prev, &next, &presetName, &save, &ab, &copyAB, &learn, &midiMenu, &settings, &help })
    {
        b->setLookAndFeel (&lnf);
        addAndMakeVisible (*b);
    }
    learn.getProperties().set ("learn", true);
    learn.setClickingTogglesState (true);

    prev.setTooltip ("Previous preset");
    next.setTooltip ("Next preset");
    presetName.setTooltip ("Choose a preset");
    save.setTooltip ("Save the current sound as a user preset");
    ab.setTooltip ("A/B compare: switch between two settings");
    copyAB.setTooltip ("Copy the current setting to the other A/B slot");
    learn.setTooltip ("MIDI learn: click a control, then move a controller");

    prev.onClick = [this] { proc.presets.previous(); };
    next.onClick = [this] { proc.presets.next(); };
    presetName.onClick = [this] { showPresetMenu(); };
    save.onClick = [this] { showSaveDialog(); };
    ab.onClick = [this] { proc.presets.toggleAB(); };
    copyAB.onClick = [this] { proc.presets.copyCurrentToOther(); };
    learn.onClick = [this] { if (onLearnToggled) onLearnToggled (learn.getToggleState()); };
    midiMenu.onClick = [this] { showMidiMenu(); };
    settings.onClick = [this] { showSettingsMenu(); };
    help.onClick = [this] { if (onHelp) onHelp(); };

    proc.presets.addChangeListener (this);
    startTimerHz (30);
    refreshLabels();
}

TopBar::~TopBar()
{
    stopTimer();
    proc.presets.removeChangeListener (this);
    for (auto* b : { &prev, &next, &presetName, &save, &ab, &copyAB, &learn, &midiMenu, &settings, &help })
        b->setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void TopBar::timerCallback()
{
    const auto a = proc.midiLearn.getActivityCounter();
    const float before = midiLed;
    if (a != lastActivity) { lastActivity = a; midiLed = 1.0f; }
    else midiLed = juce::jmax (0.0f, midiLed - 0.12f);
    if (midiLed != before)
    {
        midiMenu.getProperties().set ("midiLed", midiLed);
        midiMenu.repaint();
    }
}

void TopBar::setLearnActive (bool a) { learn.setToggleState (a, juce::dontSendNotification); }

void TopBar::changeListenerCallback (juce::ChangeBroadcaster*) { refreshLabels(); }

void TopBar::refreshLabels()
{
    presetName.setButtonText (proc.presets.getCurrentName());
    ab.setButtonText (proc.presets.isOnB() ? "B" : "A");
    copyAB.setButtonText (proc.presets.isOnB() ? "B>A" : "A>B");
}

void TopBar::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    juce::ColourGradient grad (juce::Colour (0xff2b2620), 0, 0, juce::Colour (0xff12100d), 0, r.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRect (r);
    // rasta pin-stripe at the bottom
    const float sh = juce::jmax (1.0f, r.getHeight() * 0.04f);
    g.setColour (juce::Colour (0xff0a7a35)); g.fillRect (r.withTop (r.getBottom() - 3 * sh).withHeight (sh));
    g.setColour (juce::Colour (0xffe39a0a)); g.fillRect (r.withTop (r.getBottom() - 2 * sh).withHeight (sh));
    g.setColour (juce::Colour (0xffb5150f)); g.fillRect (r.withTop (r.getBottom() - sh).withHeight (sh));

    const float h = r.getHeight();
    g.setColour (colours::cream);
    g.setFont (Assets::font (h * 0.52f));
    auto logo = r.withWidth (r.getWidth() * 0.2f).withTrimmedLeft (h * 0.4f).withTrimmedBottom (3 * sh);
    g.drawText ("DUBASCO", logo, juce::Justification::centredLeft);
    g.setColour (colours::cream.withAlpha (0.6f));
    g.setFont (Assets::font (h * 0.3f, false));
    g.drawText ("TAPE ECHO  RE-201 STYLE", logo.withTrimmedLeft (h * 2.45f), juce::Justification::centredLeft);
}

void TopBar::resized()
{
    auto r = getLocalBounds().withTrimmedBottom (juce::roundToInt (getHeight() * 0.12f)).reduced (0, juce::roundToInt (getHeight() * 0.16f));
    const int h = r.getHeight();
    const int W = getWidth();
    auto row = r.withTrimmedLeft (juce::roundToInt (W * 0.25f));
    auto place = [&] (juce::Component& c, float wMul) { c.setBounds (row.removeFromLeft (juce::roundToInt (h * wMul))); row.removeFromLeft (h / 6); };
    place (prev, 1.0f);
    place (presetName, 7.2f);
    place (next, 1.0f);
    place (save, 1.9f);
    row.removeFromLeft (h / 2);
    place (ab, 1.1f);
    place (copyAB, 1.7f);

    auto right = r.withTrimmedRight (juce::roundToInt (h * 0.4f));
    auto placeR = [&] (juce::Component& c, float wMul) { c.setBounds (right.removeFromRight (juce::roundToInt (h * wMul))); right.removeFromRight (h / 6); };
    placeR (help, 1.0f);
    placeR (settings, 1.8f);
    placeR (midiMenu, 1.7f);
    placeR (learn, 3.4f);
}

void TopBar::showPresetMenu()
{
    auto& pm = proc.presets;
    pm.refresh();
    juce::PopupMenu m, user;
    m.setLookAndFeel (&lnf);
    m.addSectionHeader ("FACTORY");
    const auto& entries = pm.getEntries();
    for (int i = 0; i < entries.size(); ++i)
    {
        const auto& e = entries.getReference (i);
        if (e.factory) m.addItem (i + 1, e.name, true, i == pm.getCurrentIndex());
        else user.addItem (i + 1, e.name, true, i == pm.getCurrentIndex());
    }
    m.addSeparator();
    if (user.getNumItems() > 0) m.addSubMenu ("User presets", user);
    else m.addItem (-100, "(no user presets yet)", false);
    m.addSeparator();
    const bool canDelete = juce::isPositiveAndBelow (pm.getCurrentIndex(), entries.size()) && ! entries[pm.getCurrentIndex()].factory;
    m.addItem (-1, "Delete current user preset", canDelete);
    m.addItem (-2, "Open preset folder");

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetName), [this] (int r)
    {
        auto& p = proc.presets;
        if (r > 0) p.load (r - 1);
        else if (r == -1) p.removeUser (p.getCurrentIndex());
        else if (r == -2) { auto f = PresetManager::getUserFolder(); f.createDirectory(); f.startAsProcess(); }
    });
}

void TopBar::showSaveDialog()
{
    saveWindow = std::make_unique<juce::AlertWindow> ("Save preset", "Name for the user preset:", juce::MessageBoxIconType::NoIcon, this);
    saveWindow->setLookAndFeel (&lnf);
    saveWindow->addTextEditor ("name", proc.presets.getCurrentName());
    saveWindow->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    saveWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    saveWindow->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result)
    {
        if (result == 1 && saveWindow != nullptr)
            proc.presets.saveUser (saveWindow->getTextEditorContents ("name"));
        saveWindow.reset();
    }), false);
}

void TopBar::showMidiMenu()
{
    juce::PopupMenu m;
    m.setLookAndFeel (&lnf);
    m.addItem (1, "Clear all MIDI mappings");
    m.addItem (2, "Save mappings as default for new instances");
    m.addItem (3, "Load default mappings");
    m.addSeparator();
    auto& mlx = proc.midiLearn;
    if (mlx.isDirectInputAvailable())
    {
        m.addItem (4, "Receive MIDI directly from controllers", true, mlx.isDirectInputEnabled());
        if (mlx.isDirectInputEnabled())
        {
            const auto names = mlx.getOpenDeviceNames();
            if (names.isEmpty()) m.addItem (-2, "     (no MIDI device found)", false);
            for (auto& n : names) m.addItem (-2, "     " + n, false);
        }
    }
    const auto last = mlx.getLastMessageText();
    m.addItem (-3, "Last MIDI: " + (last.isNotEmpty() ? last : juce::String ("nothing received yet")), false);
    m.addSeparator();
    m.addSectionHeader ("Current mappings");
    auto& ml = proc.midiLearn;
    bool any = false;
    for (int i = 0; i < ml.getParamIds().size(); ++i)
    {
        const auto d = ml.describe (i);
        if (d.isEmpty()) continue;
        any = true;
        auto* param = proc.apvts.getParameter (ml.getParamIds()[i]);
        m.addItem (100 + i, (param ? param->getName (40) : ml.getParamIds()[i]) + "   <-   " + d + "   (click to clear)");
    }
    if (! any) m.addItem (-1, "(none - use MIDI LEARN or right-click a control)", false);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&midiMenu), [this] (int r)
    {
        auto& ml2 = proc.midiLearn;
        if (r == 1) ml2.clearAll();
        else if (r == 2) ml2.saveAsDefault();
        else if (r == 3) ml2.loadDefault();
        else if (r == 4) ml2.setDirectInputEnabled (! ml2.isDirectInputEnabled());
        else if (r >= 100) ml2.clear (r - 100);
    });
}

void TopBar::showSettingsMenu()
{
    juce::PopupMenu m;
    m.setLookAndFeel (&lnf);
    m.addSectionHeader ("WINDOW SIZE");
    const float cur = proc.editorScale.load();
    const float sizes[] = { 0.45f, 0.55f, 0.66f, 0.75f, 0.9f, 1.0f, 1.2f };
    for (int i = 0; i < (int) std::size (sizes); ++i)
        m.addItem (i + 1, juce::String (juce::roundToInt (sizes[i] * 100)) + " %", true, std::abs (cur - sizes[i]) < 0.01f);
    m.addSeparator();
    m.addSectionHeader ("VU METER SHOWS");
    const char* src[] = { "Input / record level", "Echo level", "Output level" };
    for (int i = 0; i < 3; ++i)
        m.addItem (50 + i, src[i], true, proc.vuSource.load() == i);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&settings), [this, sizes] (int r)
    {
        if (r >= 1 && r <= (int) std::size (sizes)) { if (onScaleChosen) onScaleChosen (sizes[r - 1]); }
        else if (r >= 50) proc.vuSource.store (r - 50);
    });
}
} // namespace gui
