#include "PluginEditor.h"
using namespace NeedleTheme;

MotionEditor::MotionEditor(NeedleProcessor &p, bool isPattern) : processor(p), pattern(isPattern)
{
    setWantsKeyboardFocus(true);
    setTitle(pattern ? "Pattern trajectories" : "Recorded platter and crossfader tracks");
    setDescription("Two editable lanes. Click/drag adds points; arrows select time and change values, Tab "
                   "uses normal navigation.");
}
void MotionEditor::paint(juce::Graphics &g)
{
    g.fillAll(groove);
    const auto t = pattern ? skratch::Take{} : processor.getTake(1000);
    const auto p = pattern ? processor.getPattern(processor.selectedPattern.load()) : skratch::Pattern{};
    const double length = pattern ? 1 : std::max(4.0, t.length);
    for (int lane = 0; lane < 2; ++lane)
    {
        auto r = juce::Rectangle<int>(0, lane * getHeight() / 2, getWidth(), getHeight() / 2).reduced(4);
        text(g,
             lane == 0 ? "TRACK 1 / PLATTER MOTION  [-6x .. +6x]" : "TRACK 2 / CROSSFADER  [CLOSED .. OPEN]",
             r.withHeight(17), 10, muted);
        auto plot = r.withTrimmedTop(20);
        g.setColour(panel);
        g.drawRect(plot);
        for (int i = 1; i < 16; ++i)
            g.drawVerticalLine(plot.getX() + i * plot.getWidth() / 16, static_cast<float>(plot.getY()),
                               static_cast<float>(plot.getBottom()));
        juce::Path path;
        size_t cursor = 0;
        for (int x = 0; x < plot.getWidth(); ++x)
        {
            const double phase = x / static_cast<double>(std::max(1, plot.getWidth() - 1));
            const float v = pattern ? p.sample(lane == 0 ? p.platter : p.fader, phase)
                                    : skratch::curve(lane == 0 ? t.platter : t.fader, phase * length, cursor,
                                                     lane == 0 ? 0 : 1);
            const float normalized = lane == 0 ? 0.5f - v / 12 : 1 - v;
            const float y = plot.getY() + normalized * plot.getHeight();
            if (x == 0)
                path.startNewSubPath(static_cast<float>(plot.getX() + x), y);
            else
                path.lineTo(static_cast<float>(plot.getX() + x), y);
        }
        g.setColour(lane == 0 ? teal : chrome);
        g.strokePath(path, juce::PathStrokeType(1.5f));
        if (processor.takePlaying.load() && !pattern)
        {
            g.setColour(accent);
            g.drawVerticalLine(plot.getX() +
                                   static_cast<int>(processor.takePosition.load() / length * plot.getWidth()),
                               static_cast<float>(plot.getY()), static_cast<float>(plot.getBottom()));
        }
    }
    if (hasKeyboardFocus(true))
    {
        g.setColour(teal);
        g.drawRect(getLocalBounds());
    }
}
void MotionEditor::edit(juce::Point<float> pos)
{
    lane = pos.y >= getHeight() / 2 ? 1 : 0;
    auto r = juce::Rectangle<int>(0, lane * getHeight() / 2, getWidth(), getHeight() / 2)
                 .reduced(4)
                 .withTrimmedTop(20);
    const float x = juce::jlimit(0.0f, 1.0f, (pos.x - r.getX()) / std::max(1, r.getWidth())),
                y = juce::jlimit(0.0f, 1.0f, (pos.y - r.getY()) / std::max(1, r.getHeight()));
    const float value = lane == 0 ? (0.5f - y) * 12 : 1 - y;
    node = juce::jlimit(0, 64, static_cast<int>(std::round(x * 64)));
    if (pattern)
        processor.editPattern(processor.selectedPattern.load(), lane, node, value);
    else
    {
        const auto t = processor.getTake();
        processor.editTakePoint(lane, x * std::max(4.0, t.length), value);
    }
    repaint();
}
void MotionEditor::mouseDown(const juce::MouseEvent &e)
{
    grabKeyboardFocus();
    edit(e.position);
}
void MotionEditor::mouseDrag(const juce::MouseEvent &e)
{
    edit(e.position);
}
bool MotionEditor::keyPressed(const juce::KeyPress &k)
{
    if (k.getKeyCode() == juce::KeyPress::leftKey)
    {
        node = std::max(0, node - 1);
        return true;
    }
    if (k.getKeyCode() == juce::KeyPress::rightKey)
    {
        node = std::min(64, node + 1);
        return true;
    }
    if (k.getKeyCode() == '1' || k.getKeyCode() == '2')
    {
        lane = k.getKeyCode() - '1';
        return true;
    }
    if (k.getKeyCode() == juce::KeyPress::upKey || k.getKeyCode() == juce::KeyPress::downKey)
    {
        const float delta = k.getKeyCode() == juce::KeyPress::upKey ? 0.1f : -0.1f;
        if (pattern)
        {
            auto p = processor.getPattern(processor.selectedPattern.load());
            processor.editPattern(processor.selectedPattern.load(), lane, node,
                                  (lane == 0 ? p.platter : p.fader)[node] + delta);
        }
        else
        {
            auto t = processor.getTake();
            size_t cursor = 0;
            double beat = node / 64.0 * std::max(4.0, t.length);
            float v = skratch::curve(lane == 0 ? t.platter : t.fader, beat, cursor, lane == 0 ? 0 : 1);
            processor.editTakePoint(lane, beat, v + delta);
        }
        repaint();
        return true;
    }
    return false;
}
PianoRoll::PianoRoll(NeedleProcessor &p) : processor(p)
{
    setWantsKeyboardFocus(true);
    setTitle("Piano roll scratch patterns");
    setDescription("Click adds/removes a pattern note; drag right sets duration, Shift-drag vertically sets "
                   "velocity. Arrows select cell; Enter toggles it.");
}
void PianoRoll::paint(juce::Graphics &g)
{
    g.fillAll(groove);
    const int key = 133;
    const float row = getHeight() / 12.0f, step = (getWidth() - key) / 16.0f;
    for (int i = 0; i < 12; ++i)
    {
        const int y = static_cast<int>((11 - i) * row);
        g.setColour(i % 2 ? panel.withAlpha(0.5f) : face);
        g.fillRect(0, y, key, static_cast<int>(row));
        text(g, juce::String(48 + i) + " / " + skratch::patternName(i),
             {6, y, key - 9, static_cast<int>(row)}, 9, muted);
        g.setColour(panel);
        g.drawHorizontalLine(y, static_cast<float>(key), static_cast<float>(getWidth()));
    }
    for (int i = 0; i <= 16; ++i)
    {
        g.setColour(i % 4 == 0 ? chrome.withAlpha(0.45f) : panel);
        g.drawVerticalLine(static_cast<int>(key + i * step), 0, static_cast<float>(getHeight()));
    }
    const auto t = processor.getTake(1);
    for (const auto e : t.events)
        if (e.type == skratch::PatternNote && e.value >= 0 && e.value < 12)
        {
            const float x = key + static_cast<float>(e.beat / 4) * (getWidth() - key),
                        y = (11 - static_cast<int>(e.value)) * row;
            auto r = juce::Rectangle<float>(
                x + 1, y + 1, static_cast<float>(e.duration / 4) * (getWidth() - key) - 2, row - 2);
            g.setColour(teal.withAlpha(0.45f + e.velocity * 0.55f));
            g.fillRect(r);
            text(g, skratch::patternName(static_cast<int>(e.value)), r.toNearestInt().reduced(2, 0), 9,
                 groove);
        }
    if (hasKeyboardFocus(true))
    {
        g.setColour(ink);
        g.drawRect(juce::Rectangle<float>(key + selectedStep * step, (11 - selectedPitch) * row, step, row),
                   1);
    }
}
void PianoRoll::toggle(int step, int pitch, bool remove)
{
    auto t = processor.getTake();
    t.patternPriority = true;
    t.length = std::max(4.0, t.length);
    const double beat = step * 0.25;
    dragEvent = -1;
    for (size_t i = 0; i < t.events.size(); ++i)
        if (t.events[i].type == skratch::PatternNote && static_cast<int>(t.events[i].value) == pitch &&
            std::abs(t.events[i].beat - beat) < 0.01)
        {
            t.events.erase(t.events.begin() + static_cast<std::ptrdiff_t>(i));
            processor.replaceTake(t);
            repaint();
            return;
        }
    if (!remove && t.events.size() < skratch::maxEvents)
    {
        t.events.push_back({beat, skratch::PatternNote, static_cast<double>(pitch), 0.25, 1});
        std::stable_sort(t.events.begin(), t.events.end(), [](auto a, auto b) { return a.beat < b.beat; });
        for (size_t i = 0; i < t.events.size(); ++i)
            if (t.events[i].type == skratch::PatternNote && t.events[i].value == pitch &&
                t.events[i].beat == beat)
                dragEvent = static_cast<int>(i);
    }
    processor.replaceTake(t);
    repaint();
}
void PianoRoll::mouseDown(const juce::MouseEvent &e)
{
    grabKeyboardFocus();
    selectedPitch =
        juce::jlimit(0, 11, 11 - static_cast<int>(e.position.y / std::max(1.0f, getHeight() / 12.0f)));
    if (e.x < 133)
    {
        processor.triggerPattern(selectedPitch);
        return;
    }
    selectedStep = juce::jlimit(0, 15, static_cast<int>((e.position.x - 133) / (getWidth() - 133) * 16));
    toggle(selectedStep, selectedPitch, e.mods.isRightButtonDown());
}
void PianoRoll::mouseDrag(const juce::MouseEvent &e)
{
    if (dragEvent < 0)
        return;
    auto t = processor.getTake();
    if (dragEvent >= static_cast<int>(t.events.size()))
        return;
    auto &note = t.events[dragEvent];
    if (e.mods.isShiftDown())
        note.velocity = juce::jlimit(0.05f, 1.0f, 1.0f - e.position.y / std::max(1, getHeight()));
    else
        note.duration = juce::jlimit(
            0.25, 4.0 - note.beat,
            std::round(((e.position.x - 133) / (getWidth() - 133) * 4 - note.beat) / 0.25) * 0.25);
    processor.replaceTake(t);
    repaint();
}
bool PianoRoll::keyPressed(const juce::KeyPress &k)
{
    if (k.getKeyCode() == juce::KeyPress::leftKey)
        selectedStep = std::max(0, selectedStep - 1);
    else if (k.getKeyCode() == juce::KeyPress::rightKey)
        selectedStep = std::min(15, selectedStep + 1);
    else if (k.getKeyCode() == juce::KeyPress::upKey)
        selectedPitch = std::min(11, selectedPitch + 1);
    else if (k.getKeyCode() == juce::KeyPress::downKey)
        selectedPitch = std::max(0, selectedPitch - 1);
    else if (k.getKeyCode() == juce::KeyPress::returnKey)
        toggle(selectedStep, selectedPitch, false);
    else
        return false;
    repaint();
    return true;
}

juce::TextButton &ToolPage::button(const juce::String &name, std::function<void()> action)
{
    auto b = std::make_unique<juce::TextButton>(name);
    b->onClick = std::move(action);
    addAndMakeVisible(*b);
    buttons.push_back(std::move(b));
    return *buttons.back();
}
ToolPage::ToolPage(NeedleProcessor &p, int which) : processor(p), page(which)
{
    if (page == 0)
    {
        browser.setModel(this);
        browser.setRowHeight(25);
        addAndMakeVisible(browser);
        button("ADD FILES",
               [this]
               {
                   fileAction("Add audio", "*.wav;*.mp3;*.flac;*.aif;*.aiff;*.ogg", false,
                              [this](const juce::File &f)
                              {
                                  processor.addSamples({f.getFullPathName()});
                                  return juce::Result::ok();
                              });
               });
        button("PREVIOUS", [this] { processor.nextSample(-1); });
        button("NEXT", [this] { processor.nextSample(1); });
        button("SYNTH DEMO", [this] { processor.loadDemo(); });
    }
    if (page == 1)
    {
        piano = std::make_unique<PianoRoll>(p);
        pianoView.setViewedComponent(piano.get(), false);
        pianoView.setScrollBarsShown(true, false);
        addAndMakeVisible(pianoView);
        button("PLAY SEQUENCE", [this] { processor.playRecording(true); });
        button("STOP", [this] { processor.playRecording(false); });
        button("QUANTIZE 1/16", [this] { processor.quantizeRecording(0.25); });
        button("UNDO", [this] { processor.undoTake(); });
        button("REDO", [this] { processor.undoTake(true); });
        feedback = "NOTES 48-59 / click: add/delete / drag: length / Shift-drag: velocity";
    }
    if (page == 2)
    {
        for (int i = 0; i < 16; ++i)
            selection.addItem(juce::String(48 + i) + " / " + skratch::patternName(i), i + 1);
        selection.setSelectedId(p.selectedPattern.load() + 1);
        selection.onChange = [this]
        {
            processor.selectedPattern.store(selection.getSelectedId() - 1);
            repaint();
        };
        addAndMakeVisible(selection);
        motion = std::make_unique<MotionEditor>(p, true);
        addAndMakeVisible(*motion);
        duration.setName("Pattern beats");
        duration.setTitle("Pattern length in beats");
        duration.setSliderStyle(juce::Slider::LinearHorizontal);
        duration.setTextBoxStyle(juce::Slider::TextBoxRight, false, 55, 23);
        addAndMakeVisible(duration);
        durationAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            p.state, "length", duration);
        button("PREVIEW", [this] { processor.triggerPattern(processor.selectedPattern.load()); });
        button("STOP", [this] { processor.releasePattern(99); });
        button("LEARN NOTE",
               [this]
               {
                   processor.learnTarget.store(3 + processor.selectedPattern.load());
                   feedback = "Play a MIDI note to assign this pattern";
               });
        button("TO CUSTOM 1",
               [this]
               {
                   const auto p = processor.getPattern(processor.selectedPattern.load());
                   for (int n = 0; n < 65; ++n)
                   {
                       processor.editPattern(12, 0, n, p.platter[n]);
                       processor.editPattern(12, 1, n, p.fader[n]);
                   }
                   selection.setSelectedId(13);
               });
        button("SAVE PRESET",
               [this]
               {
                   fileAction("Save preset", "*.skratch", true,
                              [this](auto f) { return processor.savePreset(f); });
               });
    }
    if (page == 3)
    {
        motion = std::make_unique<MotionEditor>(p, false);
        addAndMakeVisible(*motion);
        button("RECORD",
               [this]
               {
                   if (processor.recording.load())
                       processor.stopRecording();
                   else
                       processor.startRecording();
               });
        button("STOP",
               [this]
               {
                   processor.stopRecording();
                   processor.playRecording(false);
               });
        button("PLAY TAKE", [this] { processor.playRecording(true); });
        button("UNDO", [this] { processor.undoTake(); });
        button("REDO", [this] { processor.undoTake(true); });
        button("SNAP 1/16", [this] { processor.quantizeRecording(0.25); });
        button("EXPORT MIDI",
               [this]
               {
                   fileAction("Export MIDI", "*.mid", true,
                              [this](auto f) { return processor.exportTake(f, true); });
               });
        button("EXPORT TAKE",
               [this]
               {
                   fileAction("Export take", "*.json", true,
                              [this](auto f) { return processor.exportTake(f, false); });
               });
        button("IMPORT",
               [this]
               {
                   fileAction("Import MIDI or take", "*.mid;*.midi;*.json", false,
                              [this](auto f) { return processor.importTake(f); });
               });
        addAndMakeVisible(loop);
        loop.setToggleState(p.takeLoop.load(), juce::dontSendNotification);
        loop.onClick = [this] { processor.takeLoop.store(loop.getToggleState()); };
    }
    if (page == 4)
    {
        selection.addItemList(NeedleProcessor::presetNames(), 1);
        selection.setSelectedId(p.presetIndex.load() + 1);
        selection.onChange = [this] { processor.applyPreset(selection.getSelectedId() - 1); };
        addAndMakeVisible(selection);
        button("PREVIEW", [this] { processor.triggerPattern(processor.selectedPattern.load()); });
        button("SAVE PRESET",
               [this]
               {
                   fileAction("Save preset", "*.skratch", true,
                              [this](auto f) { return processor.savePreset(f); });
               });
        button("LOAD PRESET",
               [this]
               {
                   fileAction("Load preset", "*.skratch", false,
                              [this](auto f) { return processor.loadPreset(f); });
               });
        button("SAVE MAPPING",
               [this]
               {
                   fileAction("Save input mapping", "*.json", true,
                              [this](auto f) { return processor.exportMapping(f); });
               });
        button("LOAD MAPPING",
               [this]
               {
                   fileAction("Load input mapping", "*.json", false,
                              [this](auto f) { return processor.importMapping(f); });
               });
        feedback = "Original motion algorithms / no artist recordings / full presets include take, mappings "
                   "and file references";
    }
    if (page == 0)
        browser.setVisible(getNumRows() > 0);
}
void ToolPage::resized()
{
    auto area = getLocalBounds().reduced(8);
    auto bar = area.removeFromTop(29);
    area.removeFromTop(6);
    area.removeFromBottom(18);
    if (selection.isVisible())
    {
        selection.setBounds(bar.removeFromLeft(page == 4 ? std::min(360, getWidth() / 3) : 210));
        bar.removeFromLeft(7);
    }
    if (page == 2)
    {
        duration.setBounds(bar.removeFromLeft(135));
        bar.removeFromLeft(5);
    }
    if (page == 3)
    {
        loop.setBounds(bar.removeFromRight(104));
        bar.removeFromRight(4);
    }
    const int width = buttons.empty() ? 0 : bar.getWidth() / static_cast<int>(buttons.size());
    for (auto &b : buttons)
        b->setBounds(bar.removeFromLeft(width).reduced(2, 0));
    if (page == 0)
        browser.setBounds(area);
    if (motion)
        motion->setBounds(area);
    if (piano)
    {
        pianoView.setBounds(area);
        piano->setSize(std::max(780, area.getWidth() - 16), 264);
    }
}
void ToolPage::paint(juce::Graphics &g)
{
    g.fillAll(xpPanel);
    if (page == 0 && getNumRows() == 0)
        text(g, "NO SAMPLES / ADD FILES OR DROP AUDIO INTO THE WINDOW", getLocalBounds().reduced(22), 13,
             xpInk, juce::Justification::centred);
    if (page == 4)
    {
        text(g, "MOTION PRESETS / NU-METAL  +  HIP-HOP 2000  +  TURNTABLISM", {20, 55, getWidth() - 40, 30},
             18, xpBlue);
        text(g, "BREAKBEAT  /  INDUSTRIAL  /  EXPERIMENTAL", {20, 88, getWidth() - 40, 25}, 14, xpInk);
    }
    text(g, feedback, {10, getHeight() - 18, getWidth() - 20, 17}, 12, xpInk);
}
void ToolPage::refresh()
{
    if (page == 0)
    {
        browser.updateContent();
        browser.setVisible(getNumRows() > 0);
    }
    if (page == 2 && selection.getSelectedId() != processor.selectedPattern.load() + 1)
        selection.setSelectedId(processor.selectedPattern.load() + 1, juce::dontSendNotification);
    if (page == 4 && selection.getSelectedId() != processor.presetIndex.load() + 1)
        selection.setSelectedId(processor.presetIndex.load() + 1, juce::dontSendNotification);
    if (page == 3 && !buttons.empty())
    {
        loop.setToggleState(processor.takeLoop.load(), juce::dontSendNotification);
        buttons[0]->setButtonText(processor.recording.load() ? "REC / STOP" : "RECORD");
        buttons[0]->setToggleState(processor.recording.load(), juce::dontSendNotification);
    }
    repaint();
}
int ToolPage::getNumRows()
{
    return processor.sampleFiles().size();
}
void ToolPage::paintListBoxItem(int row, juce::Graphics &g, int w, int h, bool selected)
{
    auto files = processor.sampleFiles();
    if (row >= files.size())
        return;
    if (selected)
    {
        g.setColour(xpBlue);
        g.fillAll();
    }
    SkratchSkin::text(g, SkratchSkin::FontRole::data,
                      juce::String(row + 1).paddedLeft('0', 2) + "  " + juce::File(files[row]).getFileName(),
                      {9, 0, w - 18, h}, 13, selected ? juce::Colours::white : xpInk);
}
void ToolPage::listBoxItemClicked(int row, const juce::MouseEvent &)
{
    auto files = processor.sampleFiles();
    if (row < files.size())
        processor.addSamples({files[row]});
}
void ToolPage::fileAction(const juce::String &title, const juce::String &filter, bool save,
                          std::function<juce::Result(const juce::File &)> action)
{
    chooser = std::make_unique<juce::FileChooser>(title, juce::File(), filter);
    juce::Component::SafePointer<ToolPage> safe(this);
    chooser->launchAsync(
        (save ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting
              : juce::FileBrowserComponent::openMode) |
            juce::FileBrowserComponent::canSelectFiles,
        [safe, save, action, filter](const juce::FileChooser &fc)
        {
            if (!safe || fc.getResult() == juce::File())
                return;
            auto file = fc.getResult();
            if (save && file.getFileExtension().isEmpty())
                file = file.withFileExtension(filter.fromFirstOccurrenceOf(".", false, false));
            const auto result = action(file);
            safe->feedback =
                result.wasOk() ? "OK / " + file.getFileName() : "ERROR / " + result.getErrorMessage();
            safe->refresh();
        });
}

void MappingPanel::number(const juce::String &name, double lo, double hi, double initial,
                          std::function<void(double)> action)
{
    Field f;
    f.label = std::make_unique<juce::Label>();
    f.label->setText(name, juce::dontSendNotification);
    f.label->setFont(font(11));
    f.slider = std::make_unique<juce::Slider>();
    f.slider->setName(name);
    f.slider->setTitle(name);
    f.slider->setSliderStyle(juce::Slider::LinearHorizontal);
    f.slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 55, 23);
    f.slider->setRange(lo, hi,
                       (name.contains("AXIS") || name.contains("BUTTON") || name.contains("CC") ||
                        name.contains("CHANNEL") || name.contains("NOTE"))
                           ? 1
                           : 0.01);
    f.slider->setValue(initial, juce::dontSendNotification);
    auto *s = f.slider.get();
    s->onValueChange = [s, action] { action(s->getValue()); };
    addAndMakeVisible(*f.label);
    addAndMakeVisible(*f.slider);
    fields.push_back(std::move(f));
}
MappingPanel::MappingPanel(NeedleProcessor &p) : processor(p)
{
    for (auto *c : std::initializer_list<juce::Component *>{&device, &rescan, &center, &minimum, &maximum,
                                                            &enabled, &invertP, &invertF, &binary,
                                                            &learnSpeed, &learnFader, &learnPosition})
        addAndMakeVisible(c);
    auto cfg = p.getPadConfig();
    enabled.setToggleState(cfg.enabled, juce::dontSendNotification);
    invertP.setToggleState(cfg.invertPlatter, juce::dontSendNotification);
    invertF.setToggleState(cfg.invertFader, juce::dontSendNotification);
    binary.setToggleState(cfg.binary, juce::dontSendNotification);
    addAndMakeVisible(panic);
    panic.onClick = [this] { processor.resetMidi.store(true); };
    for (auto *b : {&enabled, &invertP, &invertF, &binary})
        b->onClick = [this] { apply(); };
    rescan.onClick = [this] { refreshDevices(); };
    device.onChange = [this] { apply(); };
    center.onClick = [this] { processor.calibratePad(0); };
    minimum.onClick = [this] { processor.calibratePad(1); };
    maximum.onClick = [this] { processor.calibratePad(2); };
    const auto change = [this](std::function<void(skratch::PadConfig &, double)> fn)
    {
        return [this, fn](double v)
        {
            auto c = processor.getPadConfig();
            fn(c, v);
            processor.setPadConfig(c);
        };
    };
    number("PLATTER AXIS", 0, 7, cfg.platterAxis,
           change([](auto &c, double v) { c.platterAxis = static_cast<int>(v); }));
    number("FADER AXIS", 0, 7, cfg.faderAxis,
           change([](auto &c, double v) { c.faderAxis = static_cast<int>(v); }));
    number("DEADZONE", 0, 0.9, cfg.deadzone,
           change([](auto &c, double v) { c.deadzone = static_cast<float>(v); }));
    number("SENSITIVITY", 0.1, 3, cfg.sensitivity,
           change([](auto &c, double v) { c.sensitivity = static_cast<float>(v); }));
    number("VELOCITY CURVE", 0.2, 4, cfg.curve,
           change([](auto &c, double v) { c.curve = static_cast<float>(v); }));
    number("MAX SPEED", 0.25, 12, cfg.maxSpeed,
           change([](auto &c, double v) { c.maxSpeed = static_cast<float>(v); }));
    number("SLOW TRIGGER AXIS", -1, 7, cfg.slowAxis,
           change([](auto &c, double v) { c.slowAxis = static_cast<int>(v); }));
    number("FAST TRIGGER AXIS", -1, 7, cfg.fastAxis,
           change([](auto &c, double v) { c.fastAxis = static_cast<int>(v); }));
    number("BINARY CUT-IN", 0, 1, cfg.cutIn,
           change([](auto &c, double v) { c.cutIn = static_cast<float>(v); }));
    const char *actions[] = {"CUE BUTTON",         "STOP BUTTON",        "NEXT CUE BUTTON",
                             "RECORD BUTTON",      "PREV SAMPLE BUTTON", "NEXT SAMPLE BUTTON",
                             "PREV PRESET BUTTON", "NEXT PRESET BUTTON"};
    for (int i = 0; i < 8; ++i)
        number(actions[i], 0, 31, cfg.buttons[i],
               change([i](auto &c, double v) { c.buttons[i] = static_cast<int>(v); }));
    number("MIDI CHANNEL / 0 ALL", 0, 16, p.midiChannel.load(),
           [this](double v) { processor.midiChannel.store(static_cast<int>(v)); });
    number("SPEED CC", 0, 127, p.speedCC.load(),
           [this](double v) { processor.speedCC.store(static_cast<int>(v)); });
    number("FADER CC", 0, 127, p.faderCC.load(),
           [this](double v) { processor.faderCC.store(static_cast<int>(v)); });
    number("POSITION CC", 0, 127, p.positionCC.load(),
           [this](double v) { processor.positionCC.store(static_cast<int>(v)); });
    for (int slot = 0; slot < 16; ++slot)
        number("NOTE / " + juce::String(skratch::patternName(slot)), 0, 127, p.noteMap[slot].load(),
               [this, slot](double v) { processor.noteMap[slot].store(static_cast<int>(v)); });
    number("MIXER CURVE", 0.15, 4, p.param("curve"),
           [this](double v) { processor.setParam("curve", static_cast<float>(v)); });
    number("PATTERN SWING", 0, 0.65, p.param("swing"),
           [this](double v) { processor.setParam("swing", static_cast<float>(v)); });
    learnSpeed.onClick = [this] { processor.learnTarget.store(processor.learnTarget.load() == 0 ? -1 : 0); };
    learnFader.onClick = [this] { processor.learnTarget.store(processor.learnTarget.load() == 1 ? -1 : 1); };
    learnPosition.onClick = [this]
    { processor.learnTarget.store(processor.learnTarget.load() == 2 ? -1 : 2); };
    for (size_t i = 0; i < learnLive.size(); ++i)
    {
        auto &button = learnLive[i];
        button.setTooltip("Click to learn from the next CC. Click again to cancel.");
        addAndMakeVisible(button);
        button.onClick = [this, i]
        {
            const auto target = NeedleProcessor::liveLearnBase + static_cast<int>(i);
            processor.learnTarget.store(processor.learnTarget.load() == target ? -1 : target);
        };
    }
    refreshDevices();
    startTimerHz(25);
}
void MappingPanel::refreshDevices()
{
    devices = processor.padDevices();
    device.clear(juce::dontSendNotification);
    device.addItem("NO DEVICE / SELECT TO CALIBRATE", 1);
    int selected = 1;
    for (size_t i = 0; i < devices.size(); ++i)
    {
        device.addItem(devices[i].name, static_cast<int>(i) + 2);
        if (devices[i].id == processor.getPadConfig().device)
            selected = static_cast<int>(i) + 2;
    }
    device.setSelectedId(selected, juce::dontSendNotification);
    repaint();
}
void MappingPanel::apply()
{
    auto c = processor.getPadConfig();
    const int index = device.getSelectedId() - 2;
    c.device = index >= 0 && index < static_cast<int>(devices.size()) ? devices[index].id : -1;
    c.enabled = enabled.getToggleState();
    c.invertPlatter = invertP.getToggleState();
    c.invertFader = invertF.getToggleState();
    c.binary = binary.getToggleState();
    processor.setPadConfig(c);
}
void MappingPanel::resized()
{
    const int width = getWidth();
    device.setBounds(12, 10, width - 130, 28);
    rescan.setBounds(width - 110, 10, 98, 28);
    enabled.setBounds(12, 44, 170, 25);
    invertP.setBounds(188, 44, 170, 25);
    invertF.setBounds(365, 44, 170, 25);
    binary.setBounds(545, 44, 145, 25);
    center.setBounds(width - 318, 77, 98, 26);
    minimum.setBounds(width - 214, 77, 98, 26);
    maximum.setBounds(width - 110, 77, 98, 26);
    const int column = (width - 36) / 3;
    for (size_t i = 0; i < fields.size(); ++i)
    {
        int x = 12 + static_cast<int>(i % 3) * (column + 6), y = 171 + static_cast<int>(i / 3) * 48;
        fields[i].label->setBounds(x, y, column, 16);
        fields[i].slider->setBounds(x, y + 16, column, 26);
    }
    learnSpeed.setBounds(12, 804, 170, 29);
    learnFader.setBounds(192, 804, 170, 29);
    learnPosition.setBounds(372, 804, 190, 29);
    panic.setBounds(572, 804, 160, 29);
    for (size_t i = 0; i < learnLive.size(); ++i)
        learnLive[i].setBounds(12 + static_cast<int>(i % 3) * (column + 6),
                               902 + static_cast<int>(i / 3) * 34, column, 29);
}
void MappingPanel::paint(juce::Graphics &g)
{
    g.fillAll(xpPanel);
    const auto raw = processor.getPadState();
    text(g,
         raw.connected
             ? (raw.standard ? "XINPUT STANDARD / LIVE" : "HID FALLBACK / MANUAL CALIBRATION REQUIRED")
             : "NO CONTROLLER REPORT / CHECK USB, BLUETOOTH OR DRIVER MODE",
         {12, 78, getWidth() - 342, 24}, 11, raw.connected ? teal : muted);
    for (int i = 0; i < 8; ++i)
    {
        int x = 12 + i * (getWidth() - 24) / 8;
        auto r = juce::Rectangle<int>(x, 111, (getWidth() - 40) / 8 - 4, 14);
        g.setColour(groove);
        g.fillRect(r);
        g.setColour(teal);
        const int delta = static_cast<int>(raw.axes[i] * r.getWidth() * 0.5f);
        g.fillRect(std::min(r.getCentreX(), r.getCentreX() + delta), r.getY(), std::abs(delta),
                   r.getHeight());
        SkratchSkin::text(g, SkratchSkin::FontRole::data,
                          "A" + juce::String(i) + " " + juce::String(raw.axes[i], 2),
                          {x, 129, r.getWidth(), 17}, 10, xpInk);
    }
    juce::String buttons = "BUTTONS ";
    for (int i = 0; i < raw.buttonCount; ++i)
        if (raw.buttons[i])
            buttons += juce::String(i) + " ";
    SkratchSkin::text(g, SkratchSkin::FontRole::data, buttons, {12, 149, getWidth() - 24, 17}, 10, xpInk);
    SkratchSkin::text(g, SkratchSkin::FontRole::data,
                      "MIDI: NOTE " + juce::String(processor.lastMidiNote.load()) + " / CC " +
                          juce::String(processor.lastMidiCC.load()) + " / " +
                          (processor.learnTarget.load() >= 0 ? "LEARN ARMED" : "LEARN OFF"),
                      {12, 842, getWidth() - 24, 20}, 12, teal);
    text(g,
         "XInput: Xbox-compatible. PS / Mobapad: select reported HID device, calibrate axes and remap "
         "buttons. Hardware compatibility is not assumed.",
         {12, 870, getWidth() - 24, 17}, 10, xpInk);
}
void MappingPanel::timerCallback()
{
    if (!isShowing())
        return;
    const auto c = processor.getPadConfig();
    const double values[] = {static_cast<double>(c.platterAxis),
                             static_cast<double>(c.faderAxis),
                             c.deadzone,
                             c.sensitivity,
                             c.curve,
                             c.maxSpeed,
                             static_cast<double>(c.slowAxis),
                             static_cast<double>(c.fastAxis),
                             c.cutIn,
                             static_cast<double>(c.buttons[0]),
                             static_cast<double>(c.buttons[1]),
                             static_cast<double>(c.buttons[2]),
                             static_cast<double>(c.buttons[3]),
                             static_cast<double>(c.buttons[4]),
                             static_cast<double>(c.buttons[5]),
                             static_cast<double>(c.buttons[6]),
                             static_cast<double>(c.buttons[7]),
                             static_cast<double>(processor.midiChannel.load()),
                             static_cast<double>(processor.speedCC.load()),
                             static_cast<double>(processor.faderCC.load()),
                             static_cast<double>(processor.positionCC.load())};
    for (size_t i = 0; i < 21 && i < fields.size(); ++i)
        fields[i].slider->setValue(values[i], juce::dontSendNotification);
    for (int i = 0; i < 16; ++i)
        fields[21 + i].slider->setValue(processor.noteMap[i].load(), juce::dontSendNotification);
    fields[37].slider->setValue(processor.param("curve"), juce::dontSendNotification);
    fields[38].slider->setValue(processor.param("swing"), juce::dontSendNotification);
    const auto armed = processor.learnTarget.load();
    for (size_t i = 0; i < learnLive.size(); ++i)
    {
        const int cc = processor.liveCC[i].load();
        const auto prefix =
            armed == NeedleProcessor::liveLearnBase + static_cast<int>(i) ? "MOVE CC / " : "LEARN ";
        learnLive[i].setButtonText(prefix + juce::String(NeedleProcessor::liveIds[i]).toUpperCase() +
                                   (cc >= 0 ? " / CC " + juce::String(cc) : ""));
    }
    enabled.setToggleState(c.enabled, juce::dontSendNotification);
    invertP.setToggleState(c.invertPlatter, juce::dontSendNotification);
    invertF.setToggleState(c.invertFader, juce::dontSendNotification);
    binary.setToggleState(c.binary, juce::dontSendNotification);
    if (lastDevice != c.device)
    {
        lastDevice = c.device;
        refreshDevices();
    }
    repaint();
}
