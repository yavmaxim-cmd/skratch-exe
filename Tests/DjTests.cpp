#include "PluginEditor.h"
#include <iostream>
#include <stdexcept>

namespace
{
void check(bool condition, const char *label)
{
    if (!condition)
        throw std::runtime_error(label);
    std::cout << "PASS " << label << '\n';
}

float frame(dj::Rack &rack, float value, double beat)
{
    float values[]{value, -value};
    rack.frame(values, 2, beat);
    if (!std::isfinite(values[0]) || !std::isfinite(values[1]))
        throw std::runtime_error("DJ FX produces finite stereo");
    return values[0];
}

double amplitude(dj::Values settings, double frequency, int rate)
{
    dj::Rack rack;
    rack.prepare(rate);
    rack.configure(settings, 120);
    double energy = 0;
    for (int i = 0; i < rate / 12; ++i)
    {
        const auto sample =
            frame(rack, .22f * std::sin(i * 2 * skratch::pi * frequency / rate), i * 2.0 / rate);
        if (i >= rate / 24)
            energy += sample * sample;
    }
    return std::sqrt(energy / (rate / 24));
}

juce::Component *component(juce::Component &parent, const char *id)
{
    if (parent.getComponentID() == id)
        return &parent;
    for (auto *child : parent.getChildren())
        if (auto *found = component(*child, id))
            return found;
    return nullptr;
}

void rackTests()
{
    for (const int rate : {44100, 48000, 96000})
    {
        auto controls = dj::defaults();
        controls[dj::enabled] = 1;
        const auto cleanLow = amplitude(controls, 60, rate);
        const auto cleanHigh = amplitude(controls, 6000, rate);
        controls[dj::filter] = -.85f;
        controls[dj::slope] = 1;
        check(amplitude(controls, 6000, rate) < cleanHigh * .14, "DJ filter low-pass and 24 dB slope");
        controls[dj::filter] = .8f;
        check(amplitude(controls, 60, rate) < cleanLow * .15, "DJ filter high-pass");
        controls[dj::filter] = 0;
        controls[dj::lowKill] = 1;
        check(amplitude(controls, 60, rate) < cleanLow * .3, "Low EQ kill");
        controls[dj::lowKill] = 0;
        controls[dj::midKill] = 1;
        check(amplitude(controls, 860, rate) < .11, "Mid EQ kill");
        controls[dj::midKill] = 0;
        controls[dj::highKill] = 1;
        check(amplitude(controls, 10000, rate) < cleanHigh * .4, "High EQ kill");
    }
    constexpr int rate = 48000;
    dj::Rack rack;
    rack.prepare(rate);
    auto controls = dj::defaults();
    controls[dj::enabled] = controls[dj::echoOn] = controls[dj::echoMix] = 1;
    controls[dj::echoSync] = 0;
    controls[dj::echoMs] = 10;
    controls[dj::feedback] = .65f;
    rack.configure(controls, 120);
    for (int i = 0; i < 1200; ++i)
        frame(rack, 0, i / 24000.0);
    frame(rack, .5f, 0);
    controls[dj::echoOut] = 1;
    rack.configure(controls, 120);
    float first = 0, second = 0;
    for (int i = 1; i <= 960; ++i)
    {
        const float y = frame(rack, 0, i / 24000.0);
        if (i == 480)
            first = y;
        if (i == 960)
            second = y;
    }
    check(first > .48f && second > .30f && second < first, "Echo Out leaves timed feedback tail");
    controls[dj::freeze] = 1;
    controls[dj::echoOut] = 1;
    rack.configure(controls, 120);
    float frozen = 0;
    for (int i = 1; i < 4800; ++i)
        frozen = std::max(frozen, std::abs(frame(rack, 0, i / 24000.0)));
    check(frozen > .15f && frozen < 1.5f, "Echo Freeze recirculates bounded samples");
    rack.prepare(rate);
    controls[dj::freeze] = 0;
    controls[dj::echoOut] = 0;
    controls[dj::echoSync] = 1;
    controls[dj::echoDivision] = 7;
    rack.configure(controls, 120);
    for (int i = 0; i < 1200; ++i)
        frame(rack, 0, 0);
    frame(rack, .6f, 0);
    float beforeDelay = 0;
    for (int i = 1; i < 16000; ++i)
        beforeDelay = std::max(beforeDelay, std::abs(frame(rack, 0, i / 24000.0)));
    check(beforeDelay == 0 && frame(rack, 0, 16000 / 24000.0) > .59f,
          "Echo 1/4 triplet follows BPM at the expected sample");

    rack.prepare(rate);
    controls = dj::defaults();
    controls[dj::enabled] = controls[dj::crushOn] = controls[dj::crushMix] = 1;
    controls[dj::bits] = 3;
    controls[dj::downsample] = 12;
    rack.configure(controls, 120);
    for (int i = 0; i < 1600; ++i)
        frame(rack, .31f, i / 24000.0);
    check(std::abs(frame(rack, .31f, 0) - .25f) < .001f, "Crusher quantizes 3-bit levels");
    const auto held = frame(rack, .79f, 0);
    check(std::abs(frame(rack, -.79f, 0) - held) < .001f, "Crusher holds output across source samples");
    controls[dj::enabled] = 0;
    rack.configure(controls, 120);
    for (int i = 0; i < 1300; ++i)
        frame(rack, .313f, 0);
    check(std::abs(frame(rack, .313f, 0) - .313f) < 1e-5f, "Rack bypass returns to dry input");

    controls = dj::defaults();
    controls[dj::enabled] = controls[dj::driveOn] = controls[dj::driveMix] = 1;
    controls[dj::driveGain] = 0;
    std::array<float, 3> drives{};
    for (int mode = 0; mode < 3; ++mode)
    {
        rack.prepare(rate);
        controls[dj::driveMode] = static_cast<float>(mode);
        rack.configure(controls, 120);
        for (int i = 0; i < 1600; ++i)
            drives[mode] = frame(rack, .16f, 0);
    }
    check(drives[0] > drives[1] && drives[1] > drives[2],
          "Soft Clip, Hard Clip and Saturation have distinct curves");

    rack.prepare(rate);
    controls = dj::defaults();
    controls[dj::enabled] = controls[dj::gateOn] = controls[dj::gateMode] = 1;
    rack.configure(controls, 120);
    check(frame(rack, .4f, 0) == .4f, "Manual Stutter waits for trigger");
    controls[dj::gateFire] = 1;
    rack.configure(controls, 120);
    float open = 0, shut = 0;
    for (int i = 0; i < 9000; ++i)
    {
        const auto value = frame(rack, .4f, i / 24000.0);
        if (i == 1800)
            open = value;
        if (i == 4900)
            shut = value;
    }
    check(open > .39f && shut < .01f, "Synced Stutter produces rhythmic open and closed segments");
    controls[dj::limiter] = 1;
    controls[dj::enabled] = 0;
    rack.configure(controls, 120);
    for (int i = 0; i < 1600; ++i)
        frame(rack, 2, 0);
    check(frame(rack, 2, 0) < 1, "Limiter contains peaks independently from rack bypass");
}
} // namespace

void runDjTests(NeedleEditor &editor, const juce::File &directory)
{
    rackTests();
    dj::Motor motor;
    motor.prepare(48000);
    auto settings = dj::defaults();
    settings[dj::motorOn] = 1;
    motor.configure(settings);
    check(motor.tick(1, false) < .01, "Motor applies finite start ramp");
    check(motor.tick(-2.5, true) == -2.5, "Manual motion bypasses motor inertia");
    motor.action(1u << dj::brakeAction, 1);
    const double first = motor.tick(1, false);
    for (int i = 0; i < 22000; ++i)
        motor.tick(1, false);
    check(first > .99 && motor.tick(1, false) == 0, "Vinyl Brake reaches full stop");
    motor.action(1u << dj::spinAction, 0);
    check(motor.tick(1, false) < -2.8, "Spinback starts in reverse");
    constexpr std::array<int, 7> binding{9, -1, 12, 15, 8, 14, 2};
    std::array<bool, 32> released{}, pressed{};
    pressed[9] = pressed[15] = true;
    const auto padDown = dj::mapFxButtons(pressed, released, binding);
    const auto padHeld = dj::mapFxButtons(pressed, pressed, binding);
    const auto padUp = dj::mapFxButtons(released, pressed, binding);
    check(padDown.actions == (1u << dj::echoAction) && padDown.cue == 1 && padHeld.actions == 0 &&
              padUp.cue == -1,
          "simulated remapped FX bank handles press, hold and Gate release");

    const auto owner = std::make_unique<NeedleProcessor>(true);
    auto &p = *owner;
    p.prepareToPlay(48000, 256);
    p.setNonRealtime(true);
    p.setDjParam(dj::cueMode, 1);
    p.hotCues[0].store(.35);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> audio(2, 256);
    midi.addEvent(juce::MidiMessage::noteOn(1, 72, 1.0f), 91);
    p.processBlock(audio, midi);
    check(audio.getMagnitude(0, 91) == 0 && audio.getMagnitude(92, 160) > .02f && p.progress.load() > .35,
          "Hot Cue MIDI starts after the sample offset and seeks to stored position");
    midi.addEvent(juce::MidiMessage::noteOff(1, 72), 43);
    for (int i = 0; i < 4; ++i)
        p.processBlock(audio, midi);
    check(audio.getMagnitude(0, 256) < 1e-5f, "Gate Hot Cue releases playback");
    p.setDjParam(dj::cueMode, 2);
    p.requestHotCue(0, true);
    p.processBlock(audio, midi);
    check(audio.getMagnitude(0, 256) > .02f, "Toggle Hot Cue starts from GUI request");
    p.requestHotCue(0, true);
    for (int i = 0; i < 4; ++i)
        p.processBlock(audio, midi);
    check(audio.getMagnitude(0, 256) < 1e-5f, "Toggle Hot Cue second press stops playback");
    p.setDjParam(dj::cueMode, 0);
    midi.addEvent(juce::MidiMessage::noteOn(1, 72, 1.0f), 5);
    midi.addEvent(juce::MidiMessage::noteOff(1, 72), 32);
    p.processBlock(audio, midi);
    check(audio.getMagnitude(64, 192) > .02f, "Trigger Hot Cue keeps playing after Note Off");
    p.learnTarget.store(dj::learnBase + dj::filter);
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 24, 127), 120);
    p.processBlock(audio, midi);
    check(p.djCC[dj::filter].load() == 24 && p.djValue(dj::filter) == 1,
          "FX MIDI Learn routes CC to DSP values");
    p.setDjParam(dj::filter, -.4f);
    p.setDjParam(dj::enabled, 1);
    p.processBlock(audio, midi);
    juce::MemoryBlock blob;
    p.getStateInformation(blob);
    const auto restored = std::make_unique<NeedleProcessor>(true);
    restored->setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
    check(restored->djValue(dj::enabled) == 1 && std::abs(restored->djValue(dj::filter) + .4f) < .001f &&
              restored->djCC[dj::filter].load() == 24 && restored->hotCues[0].load() == .35,
          "FX automation, MIDI map and Hot Cue restore from project state");
    const auto xml = juce::AudioProcessor::getXmlFromBinary(blob.getData(), static_cast<int>(blob.getSize()));
    auto legacy = juce::ValueTree::fromXml(*xml);
    legacy.setProperty("skratchVersion", 3, nullptr);
    legacy.removeProperty("extra", nullptr);
    for (int i = legacy.getNumChildren() - 1; i >= 0; --i)
        if (legacy.getChild(i).getProperty("id").toString().startsWith("dj."))
            legacy.removeChild(i, nullptr);
    const auto legacyXml = legacy.createXml();
    juce::AudioProcessor::copyXmlToBinary(*legacyXml, blob);
    restored->setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
    check(restored->djValue(dj::enabled) == 0 && restored->hotCues[0].load() < 0,
          "2.1 state migrates to bypassed FX and empty Hot Cues");
    p.loadFile(directory.getChildFile("nonexistent-hotcue-sample.wav"));
    for (int i = 0; i < 200 && p.loading.load(); ++i)
        juce::Thread::sleep(2);
    check(!p.loading.load() && p.hotCues[0].load() == .35, "failed file load preserves active Hot Cues");
    for (const auto &parameter : dj::definitions)
        if (p.state.getParameter(parameter.id) == nullptr)
            throw std::runtime_error("DJ parameter missing from host automation");
    check(true, "Every new DJ parameter is exposed in APVTS");

    auto *bank = dynamic_cast<juce::TextButton *>(component(editor, "fx.bank"));
    auto *echo = component(editor, "dj.echoDivision");
    auto *echoMilliseconds = component(editor, "dj.echoMs");
    auto *main = component(editor, "bpm");
    check(bank && echo && echoMilliseconds && main && main->isVisible() && !echo->isVisible(),
          "MAIN bank preserves existing rotary controls");
    bank->onClick();
    check(echo->isVisible() && !echoMilliseconds->isVisible() && !main->isVisible(),
          "FX bank selects the synced Echo division control");
    auto &editorProcessor = static_cast<NeedleProcessor &>(*editor.getAudioProcessor());
    editorProcessor.getStateInformation(blob);
    restored->setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
    check(restored->mixerFxBank.load(), "FX bank survives host state restore");
    std::unique_ptr<juce::AudioProcessorEditor> restoredEditor(restored->createEditor());
    auto *restoredBank = dynamic_cast<juce::TextButton *>(component(*restoredEditor, "fx.bank"));
    check(restoredBank && restoredBank->getButtonText() == "BANK / FX",
          "reopened editor restores the FX bank");
    editorProcessor.setDjParam(dj::echoSync, 0);
    bank->onClick();
    bank->onClick();
    check(!echo->isVisible() && echoMilliseconds->isVisible(),
          "FX bank selects Echo milliseconds when sync is off");
    editorProcessor.setDjParam(dj::echoSync, 1);
    bank->onClick();
    bank->onClick();
    for (auto id :
         {"dj.inputGain", "dj.inputHp",   "dj.inputLp",  "dj.noiseThreshold", "dj.torque", "dj.startMs",
          "dj.brakeMs",   "dj.spinSpeed", "dj.friction", "dj.morphAmount",    "dj.mix",    "dj.duty",
          "dj.resonance", "dj.low",       "dj.mid",      "dj.high",           "dj.filter", "dj.feedback",
          "dj.drive",     "dj.bits"})
        if (dynamic_cast<CustomRotaryKnob *>(component(editor, id)) == nullptr)
            throw std::runtime_error("missing physical mixer parameter control");
    check(true, "all 21 retained mixer sockets have attached rotary controls");
    for (const auto *id : {"dj.inputGain", "dj.inputHp", "dj.inputLp", "dj.noiseThreshold",
                          "bpm", "cutIn", "curve", "swing", "dj.low", "dj.mid", "dj.high", "dj.filter",
                          "dj.echoMs", "dj.echoDivision", "dj.feedback", "dj.drive", "dj.bits",
                          "dj.mix", "dj.duty", "dj.resonance", "dj.torque", "dj.startMs", "dj.brakeMs",
                          "dj.spinSpeed", "dj.friction", "dj.morphAmount", "gain", "pitch", "sensitivity",
                          "response", "fader"})
    {
        auto *slider = dynamic_cast<juce::Slider *>(component(editor, id));
        auto *parameter = editorProcessor.state.getParameter(id);
        if (slider == nullptr || parameter == nullptr)
            throw std::runtime_error(std::string("missing mixer binding: ") + id);
        const auto original = parameter->getValue();
        const auto changed = parameter->convertFrom0to1(original > .5f ? .16f : .84f);
        slider->setValue(changed, juce::sendNotificationSync);
        const auto difference = std::abs(editorProcessor.param(juce::String(id)) - changed);
        check(difference <= .0005f, (juce::String("physical mixer binding writes APVTS: ") + id).toRawUTF8());
        editorProcessor.setParam(id, parameter->convertFrom0to1(original));
    }
    editor.selectPanel(7);
    const auto preview = editor.createComponentSnapshot(editor.getLocalBounds(), true, 1.5f);
    const auto output = directory.getChildFile("fx-bank.png");
    output.deleteFile();
    juce::FileOutputStream stream(output);
    juce::PNGImageFormat png;
    check(stream.openedOk() && png.writeImageToStream(preview, stream),
          "native DJ FX bank and panel render to PNG");
    editor.selectPanel(0);
    bank->onClick();
}
