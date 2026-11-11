#include "DjPanel.h"
#include "PluginEditor.h"

juce::String DjPanel::valueText(dj::Param parameter, double value)
{
    if (std::abs(value) < .00005)
        value = 0;
    const int index = static_cast<int>(std::round(value));
    if (parameter == dj::echoDivision)
    {
        constexpr const char *names[]{"1/1",  "1/2",  "1/4",  "1/8",  "1/16",
                                      "1/1T", "1/2T", "1/4T", "1/8T", "1/16T"};
        return names[juce::jlimit(0, 9, index)];
    }
    if (parameter == dj::gateDivision)
    {
        constexpr const char *names[]{"1/4", "1/8", "1/16", "1/32", "1/4T", "1/8T", "1/16T", "1/32T"};
        return names[juce::jlimit(0, 7, index)];
    }
    if (parameter == dj::driveMode)
    {
        constexpr const char *names[]{"SOFT", "HARD", "SAT"};
        return names[juce::jlimit(0, 2, index)];
    }
    if (parameter == dj::cueMode)
    {
        constexpr const char *names[]{"TRIGGER", "GATE", "TOGGLE"};
        return names[juce::jlimit(0, 2, index)];
    }
    if (parameter == dj::slope)
        return index == 0 ? "12 dB" : "24 dB";
    if (parameter == dj::gateMode)
        return index == 0 ? "AUTO" : "MANUAL";
    if (parameter == dj::padBank)
        return index == 0 ? "MAIN" : "FX";
    if (parameter == dj::morphA || parameter == dj::morphB)
        return skratch::patternName(juce::jlimit(0, 15, index));
    if ((parameter == dj::inputHp && index < 15) || (parameter == dj::inputLp && index >= 19999))
        return "OFF";
    if (parameter == dj::morphAmount || parameter == dj::mix)
        return juce::String(juce::roundToInt(value * 100)) + "%";
    const auto &definition = dj::definitions[parameter];
    if (definition.maximum == 1 && definition.minimum == 0 && definition.step == 1)
        return index == 0 ? "OFF" : "ON";
    return definition.step == 1 ? juce::String(index) : juce::String(value, 2);
}

DjPanel::DjPanel(NeedleProcessor &p) : processor(p)
{
    addAndMakeVisible(setCue);
    setCue.setTooltip("Set or replace Hot Cue at the current position. Shift-click a slot to clear it.");
    for (size_t i = 0; i < cues.size(); ++i)
    {
        auto &button = cues[i];
        button.setButtonText(juce::String(static_cast<int>(i + 1)));
        addAndMakeVisible(button);
        button.onStateChange = [this, i, lastDown = false, playing = false]() mutable
        {
            auto &button = cues[i];
            const bool down = button.isDown();
            if (down == lastDown)
                return;
            lastDown = down;
            if (down)
            {
                if (juce::ModifierKeys::getCurrentModifiersRealtime().isShiftDown())
                    processor.hotCues[i].store(-1);
                else if (setCue.getToggleState() || processor.hotCues[i].load() < 0)
                    processor.storeHotCue(static_cast<int>(i));
                else
                {
                    processor.requestHotCue(static_cast<int>(i), true);
                    playing = true;
                }
            }
            else if (playing)
            {
                processor.requestHotCue(static_cast<int>(i), false);
                playing = false;
            }
        };
    }
    constexpr const char *names[]{"ECHO OUT", "BRAKE", "SPINBACK", "STUTTER"};
    constexpr dj::Action actionIds[]{dj::echoAction, dj::brakeAction, dj::spinAction, dj::stutterAction};
    for (size_t i = 0; i < actions.size(); ++i)
    {
        actions[i].setButtonText(names[i]);
        addAndMakeVisible(actions[i]);
        actions[i].onClick = [this, action = actionIds[i]]
        {
            if (action == dj::echoAction || action == dj::stutterAction)
            {
                const auto parameter = action == dj::echoAction ? dj::echoOut : dj::gateFire;
                processor.setDjParam(parameter, processor.djValue(parameter) > .5f ? 0 : 1);
            }
            else
                processor.requestDjAction(action);
        };
    }
    for (size_t i = 0; i < fields.size(); ++i)
    {
        auto &f = fields[i];
        const auto &d = dj::definitions[i];
        f.title.setText(juce::String(d.name).toUpperCase(), juce::dontSendNotification);
        f.title.setFont(SkratchSkin::font(SkratchSkin::FontRole::label, 14));
        f.title.setColour(juce::Label::textColourId, NeedleTheme::xpInk);
        f.slider.setTitle(d.name);
        f.slider.setComponentID(d.id);
        f.slider.setSliderStyle(juce::Slider::LinearHorizontal);
        f.slider.setTextBoxStyle(juce::Slider::TextBoxRight, true, 60, 23);
        f.slider.setDoubleClickReturnValue(true, d.initial);
        addAndMakeVisible(f.title);
        addAndMakeVisible(f.slider);
        addAndMakeVisible(f.learn);
        f.learn.setButtonText("LEARN");
        f.learn.setTooltip(
            "Learn the next CC on the selected MIDI channel. Shift-click clears the assignment.");
        f.learn.onClick = [this, i]
        {
            if (juce::ModifierKeys::getCurrentModifiersRealtime().isShiftDown())
                processor.djCC[i].store(-1);
            else
            {
                const int target = dj::learnBase + static_cast<int>(i);
                processor.learnTarget.store(processor.learnTarget.load() == target ? -1 : target);
            }
        };
        f.attachment =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.state, d.id, f.slider);
        f.slider.textFromValueFunction = [parameter = static_cast<dj::Param>(i)](double value)
        { return valueText(parameter, value); };
        f.slider.onDragStart = [this, parameter = static_cast<dj::Param>(i)]
        { processor.clearDjOverride(parameter); };
        f.slider.updateText();
    }
    startTimerHz(10);
}

void DjPanel::resized()
{
    setCue.setBounds(12, 10, 92, 25);
    for (size_t i = 0; i < cues.size(); ++i)
        cues[i].setBounds(112 + static_cast<int>(i) * 49, 10, 43, 25);
    for (size_t i = 0; i < actions.size(); ++i)
        actions[i].setBounds(12 + static_cast<int>(i) * 113, 43, 103, 25);
    const auto column = (getWidth() - 30) / 4;
    for (size_t i = 0; i < fields.size(); ++i)
    {
        const int x = 12 + static_cast<int>(i % 4) * column;
        const int y = 80 + static_cast<int>(i / 4) * 48;
        fields[i].title.setBounds(x, y, column - 10, 17);
        fields[i].slider.setBounds(x, y + 17, column - 61, 23);
        fields[i].learn.setBounds(x + column - 59, y + 17, 48, 23);
    }
}

void DjPanel::paint(juce::Graphics &g)
{
    g.fillAll(NeedleTheme::xpPanel);
    const auto state = valueText(dj::cueMode, processor.djValue(dj::cueMode));
    SkratchSkin::text(g, SkratchSkin::FontRole::data, "HOT CUE / " + state, {535, 12, 240, 22}, 13,
                      NeedleTheme::xpBlue);
}

void DjPanel::timerCallback()
{
    if (!isShowing())
        return;
    const auto armed = processor.learnTarget.load();
    for (size_t i = 0; i < fields.size(); ++i)
    {
        auto &field = fields[i];
        const int cc = processor.djCC[i].load();
        field.learn.setButtonText(armed == dj::learnBase + static_cast<int>(i)
                                      ? "ARMED"
                                      : (cc < 0 ? "LEARN" : "CC " + juce::String(cc)));
        const auto value = processor.djValue(static_cast<dj::Param>(i));
        if (!field.slider.isMouseButtonDown() && std::abs(field.slider.getValue() - value) > .0001)
            field.slider.setValue(value, juce::dontSendNotification);
    }
    for (size_t i = 0; i < cues.size(); ++i)
        cues[i].setToggleState(processor.hotCues[i].load() >= 0, juce::dontSendNotification);
    actions[0].setToggleState(processor.djValue(dj::echoOut) > .5f, juce::dontSendNotification);
    actions[3].setToggleState(processor.djValue(dj::gateFire) > .5f, juce::dontSendNotification);
    repaint(535, 12, 240, 22);
}
