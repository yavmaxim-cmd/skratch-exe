#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <cstdlib>

static thread_local bool measureAllocations = false;
static thread_local size_t callbackAllocations = 0;
void *operator new(std::size_t n)
{
    if (measureAllocations)
        ++callbackAllocations;
    if (void *p = std::malloc(std::max(std::size_t(1), n)))
        return p;
    throw std::bad_alloc();
}
void *operator new[](std::size_t n)
{
    return ::operator new(n);
}
void operator delete(void *p) noexcept
{
    std::free(p);
}
void operator delete[](void *p) noexcept
{
    std::free(p);
}
void operator delete(void *p, std::size_t) noexcept
{
    std::free(p);
}
void operator delete[](void *p, std::size_t) noexcept
{
    std::free(p);
}

static void check(bool success, const char *message)
{
    if (!success)
        throw std::runtime_error(message);
    std::cout << "PASS " << message << std::endl;
}
static float blocks(NeedleProcessor &p, int count = 20, int channels = 2)
{
    juce::AudioBuffer<float> b(channels, 256);
    juce::MidiBuffer midi;
    float peak = 0;
    for (int n = 0; n < count; ++n)
    {
        b.clear();
        p.processBlock(b, midi);
        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < 256; ++i)
            {
                const float s = b.getSample(c, i);
                if (!std::isfinite(s) || std::abs(s) > 1)
                    throw std::runtime_error("invalid audio output");
                peak = std::max(peak, std::abs(s));
            }
    }
    return peak;
}
static void message(NeedleProcessor &p, juce::MidiMessage m)
{
    juce::AudioBuffer<float> b(2, 256);
    b.clear();
    juce::MidiBuffer midi;
    midi.addEvent(m, 32);
    p.processBlock(b, midi);
}
void runUpgradeTests(const juce::File &output)
{
    auto instance = std::make_unique<NeedleProcessor>();
    auto &p = *instance;
    p.prepareToPlay(48000, 256);
    p.setParam("sync", 0);
    // Device-independent fixtures cover the controller mapping math, not hardware certification.
    skratch::PadConfig cfg;
    cfg.enabled = true;
    cfg.slowAxis = cfg.fastAxis = -1;
    skratch::PadState pad;
    pad.connected = true;
    pad.axisCount = 6;
    pad.axes[1] = -0.5f;
    pad.axes[2] = 0.3f;
    const auto half = skratch::mapPad(pad, cfg);
    pad.axes[1] = -1;
    const auto full = skratch::mapPad(pad, cfg);
    check(half.active && half.speed > 0 && half.speed < full.speed && full.speed == cfg.maxSpeed,
          "Xbox-style proportional velocity");
    pad.axes[1] = 0.05f;
    check(skratch::mapPad(pad, cfg).speed == 0, "gamepad deadzone center is zero");
    pad.connected = false;
    check(!skratch::mapPad(pad, cfg).active, "gamepad disconnect releases control");
    pad.connected = true;
    check(skratch::mapPad(pad, cfg).active, "gamepad reconnect reactivates mapping");
    cfg.platterAxis = 3;
    cfg.faderAxis = 0;
    pad.axes[3] = -0.8f;
    pad.axes[0] = -1;
    check(skratch::mapPad(pad, cfg).speed > 0 && skratch::mapPad(pad, cfg).fader == 0,
          "PlayStation / Mobapad remapped-axis fixture");
    cfg.invertPlatter = false;
    check(skratch::mapPad(pad, cfg).speed < 0, "axis inversion");
    cfg.calibration[3] = {-0.8f, 0.1f, 0.7f};
    pad.axes[3] = 0.1f;
    check(skratch::mapPad(pad, cfg).speed == 0, "manual center calibration");
    cfg.binary = true;
    cfg.cutIn = 0.5f;
    pad.axes[0] = 0.2f;
    check(skratch::mapPad(pad, cfg).fader == 1, "binary fader cut-in");
    cfg.faderAxis = 7;
    check(!skratch::mapPad(pad, cfg).active,
          "unknown HID axes report unavailable instead of guessed compatibility");
    cfg.faderAxis = 0;
    cfg.buttons[0] = 11;
    p.setPadConfig(cfg);
    check(p.getPadConfig().buttons[0] == 11, "button remapping persists in config");
    needle::Engine e;
    e.prepare(48000);
    e.configure(0, 1.5f, 0.04f, 1);
    e.deckGain = 1;
    float largest = 0;
    for (int n = 0; n < 4800; ++n)
    {
        const float before = e.deckGain;
        e.tick(n % 2 ? 6 : -6, n % 20 < 10 ? 0 : 1, false, true, false);
        largest = std::max(largest, std::abs(e.deckGain - before));
    }
    check(largest < 0.02f && std::isfinite(e.velocity), "rapid cuts and direction changes remain smoothed");
    p.setParam("play", 1);
    for (int mode = 0; mode < 4; ++mode)
    {
        p.setParam("mode", static_cast<float>(mode));
        p.keySpeed.store(-2);
        blocks(p, 64);
        check(p.actualSpeed.load() < -1.8f, "scratch algorithm reverse response");
        p.keySpeed.store(0);
        p.mouseHeld.store(true);
        p.mouseSpeed.store(0);
        blocks(p, 300);
        check(blocks(p) < 1e-5f, "scratch algorithm zero-speed silence");
        p.mouseHeld.store(false);
    }
    p.setParam("mode", 0);
    for (double rate : {44100.0, 48000.0, 96000.0})
    {
        p.prepareToPlay(rate, 256);
        check(blocks(p) > 0.02f, "sample-rate independent resampling");
    }
    auto layout = p.getBusesLayout();
    layout.inputBuses.set(0, juce::AudioChannelSet::mono());
    layout.outputBuses.set(0, juce::AudioChannelSet::mono());
    check(p.setBusesLayout(layout), "mono host layout accepted");
    check(blocks(p, 30, 1) > 0.02f, "mono processing");
    layout.inputBuses.set(0, juce::AudioChannelSet::stereo());
    layout.outputBuses.set(0, juce::AudioChannelSet::stereo());
    p.setBusesLayout(layout);
    p.prepareToPlay(48000, 256);
    p.setParam("play", 0);
    message(p, juce::MidiMessage::controllerEvent(1, 16, 0));
    blocks(p);
    check(p.actualSpeed.load() < -5.9f, "MIDI CC controls reverse velocity");
    message(p, juce::MidiMessage::pitchWheel(1, 16383));
    blocks(p);
    check(p.actualSpeed.load() > 5.9f, "14-bit pitch bend controls forward velocity");
    p.resetMidi.store(true);
    blocks(p, 50);
    check(std::abs(p.actualSpeed.load()) < 0.001f, "MIDI release removes latched controller override");
    p.learnTarget.store(0);
    message(p, juce::MidiMessage::controllerEvent(1, 74, 64));
    check(p.speedCC.load() == 74 && p.learnTarget.load() == -1, "MIDI Learn speed CC");
    p.learnTarget.store(1);
    message(p, juce::MidiMessage::controllerEvent(1, 7, 0));
    check(p.faderCC.load() == 7, "MIDI Learn fader CC");
    blocks(p);
    check(blocks(p) < 1e-5f, "learned fader closes audio");
    message(p, juce::MidiMessage::controllerEvent(1, 7, 127));
    p.learnTarget.store(3);
    message(p, juce::MidiMessage::noteOn(1, 72, (juce::uint8)100));
    check(p.noteMap[0].load() == 72, "MIDI Learn pattern note");
    message(p, juce::MidiMessage::allNotesOff(1));
    p.midiChannel.store(2);
    p.lastMidiCC.store(-1);
    p.learnTarget.store(0);
    message(p, juce::MidiMessage::controllerEvent(1, 11, 100));
    check(p.learnTarget.load() == 0 && p.speedCC.load() == 74,
          "MIDI channel filter excludes learn from other channels");
    p.learnTarget.store(-1);
    p.midiChannel.store(0);
    for (int slot = 0; slot < 12; ++slot)
    {
        p.seek.store(0.5);
        p.triggerPattern(slot, 0.8f, 0.25);
        const float audio = blocks(p, 20);
        check(slot == 11 ? std::isfinite(audio) : audio > 0.001f, "algorithmic piano-roll pattern renders");
        p.releasePattern(99);
        blocks(p, 2);
    }
    p.editPattern(12, 0, 20, -3.25f);
    p.editPattern(12, 1, 20, 0.25f);
    check(p.getPattern(12).platter[20] == -3.25f && p.getPattern(12).fader[20] == 0.25f,
          "custom independent pattern trajectories");
    p.setParam("play", 1);
    p.setParam("fader", 1);
    p.setParam("bpm", 120);
    p.seek.store(0.5);
    blocks(p);
    p.startRecording();
    for (int n = 0; n < 80; ++n)
    {
        p.keySpeed.store(n < 40 ? 1.25f : -1.25f);
        p.setParam("fader", n % 16 < 8 ? 0.15f : 0.9f);
        p.cutHeld.store(n > 20 && n < 30);
        blocks(p, 1);
    }
    p.stopRecording();
    p.keySpeed.store(0);
    p.cutHeld.store(false);
    const auto take = p.getTake();
    check(take.platter.size() > 50 && take.platter.size() == take.fader.size() && take.length > 0.8,
          "sample-clock recording captures both lanes at 200 Hz");
    check(std::any_of(take.events.begin(), take.events.end(), [](auto e) { return e.type == skratch::Cut; }),
          "recording captures momentary cuts as events");
    p.playRecording(true);
    blocks(p, 20);
    check(p.takePosition.load() > 0 && p.takePlaying.load(), "recorded take playback advances");
    const double repeatedPosition = p.progress.load();
    p.playRecording(false);
    blocks(p, 8);
    p.playRecording(true);
    blocks(p, 20);
    check(std::abs(repeatedPosition - p.progress.load()) < 1e-9,
          "recorded take repeats the same platter trajectory");
    p.playRecording(false);
    output.createDirectory();
    auto json = output.getChildFile("test-take.json"), mid = output.getChildFile("test-take.mid"),
         mapping = output.getChildFile("test-mapping.json"),
         preset = output.getChildFile("test-preset.skratch");
    check(p.exportTake(json, false).wasOk(), "take JSON export");
    check(p.exportTake(mid, true).wasOk(), "Standard MIDI File export");
    juce::MidiFile mf;
    juce::FileInputStream midiStream(mid);
    check(mf.readFrom(midiStream) && mf.getNumTracks() == 4 && mf.getTimeFormat() == 960,
          "SMF contains conductor, platter, fader and event tracks");
    p.quantizeRecording(0.25);
    const auto quantized = p.getTake();
    check(quantized.platter.size() < take.platter.size(), "recording quantization collapses duplicate times");
    check(p.undoTake() && p.getTake().platter.size() == take.platter.size(), "take Undo");
    check(p.undoTake(true) && p.getTake().platter.size() == quantized.platter.size(), "take Redo");
    check(p.importTake(mid).wasOk(), "MIDI import");
    auto imported = p.getTake();
    check(imported.platter.size() == take.platter.size() &&
              std::abs(imported.platter[20].beat - take.platter[20].beat) < 1e-12 &&
              imported.fader[20].value == take.fader[20].value,
          "exact MIDI round-trip preserves internal precision");
    check(p.importTake(json).wasOk(), "JSON take import");
    p.editTakePoint(0, 0.25, -2.3f);
    p.editTakePoint(1, 0.25, 0.4f);
    auto edited = p.getTake();
    size_t cursor = 0;
    check(std::abs(skratch::curve(edited.platter, 0.25, cursor, 0) + 2.3f) < 0.001f,
          "recorded platter curve editing");
    cursor = 0;
    check(std::abs(skratch::curve(edited.fader, 0.25, cursor, 0) - 0.4f) < 0.001f,
          "recorded fader curve editing");
    check(p.exportMapping(mapping).wasOk(), "mapping export");
    p.speedCC.store(99);
    p.noteMap[0].store(88);
    check(p.importMapping(mapping).wasOk() && p.speedCC.load() == 74 && p.noteMap[0].load() == 72,
          "mapping import restores CC and learned notes");
    p.takeLoop.store(false);
    check(p.savePreset(preset).wasOk(), "full preset save");
    p.takeLoop.store(true);
    p.editPattern(12, 0, 20, 0);
    check(p.loadPreset(preset).wasOk() && p.getPattern(12).platter[20] == -3.25f && !p.takeLoop.load(),
          "full preset restores custom pattern and loop state");
    auto broken = output.getChildFile("broken.json");
    broken.replaceWithText("{\"format\":\"SKRATCH.TAKE.2\",\"length\":-1}");
    check(p.importTake(broken).failed(), "invalid import rejected without replacing take");
    p.setParam("play", 1);
    p.setParam("mode", 1);
    message(p, juce::MidiMessage::allNotesOff(1));
    blocks(p, 10);
    juce::AudioBuffer<float> audio(2, 256);
    juce::MidiBuffer midi;
    callbackAllocations = 0;
    measureAllocations = true;
    for (int n = 0; n < 100; ++n)
    {
        audio.clear();
        p.processBlock(audio, midi);
    }
    measureAllocations = false;
    check(callbackAllocations == 0, "audio callback performs no heap allocations");
    p.startRecording();
    callbackAllocations = 0;
    measureAllocations = true;
    for (int n = 0; n < 100; ++n)
    {
        audio.clear();
        p.processBlock(audio, midi);
    }
    measureAllocations = false;
    p.stopRecording();
    check(callbackAllocations == 0, "recording callback performs no heap allocations");
    for (const auto parameter : {dj::enabled, dj::echoOn, dj::crushOn, dj::driveOn, dj::gateOn, dj::limiter})
        p.setDjParam(parameter, 1);
    p.setDjParam(dj::filter, -.5f);
    p.setDjParam(dj::mid, 2);
    p.setDjParam(dj::inputHp, 200);
    p.setDjParam(dj::inputLp, 9000);
    p.setDjParam(dj::noiseOn, 1);
    p.setDjParam(dj::morphOn, 1);
    p.setDjParam(dj::morphAmount, .5f);
    p.setDjParam(dj::coastOn, 1);
    p.triggerPattern(3);
    callbackAllocations = 0;
    measureAllocations = true;
    for (int n = 0; n < 100; ++n)
    {
        audio.clear();
        p.processBlock(audio, midi);
    }
    measureAllocations = false;
    check(callbackAllocations == 0, "all DJ FX and new mixer DSP run without audio callback allocations");
    p.setDjParam(dj::enabled, 0);
    p.setDjParam(dj::limiter, 0);
    p.setDjParam(dj::inputHp, 0);
    p.setDjParam(dj::inputLp, 20000);
    p.setDjParam(dj::noiseOn, 0);
    p.setDjParam(dj::morphOn, 0);
    p.setDjParam(dj::coastOn, 0);
    p.releasePattern(99);
    auto start = std::chrono::steady_clock::now();
    blocks(p, 1000);
    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(),
                 seconds = 256000.0 / 48000;
    std::cout << "METRIC offline processing / rendered seconds: " << wall << " / " << seconds
              << "; realtime factor " << wall / seconds << std::endl;
    check(wall < seconds, "DSP throughput exceeds real time on this machine");
    for (int i = 0; i < 12; ++i)
        p.applyPreset(i);
    check(p.presetNames().size() == 12, "six categories contain original algorithmic presets");
    p.setParam("mode", 0);
    p.setParam("play", 0);
    p.mouseHeld.store(true);
    p.mouseSpeed.store(0);
    blocks(p, 100);
    p.mouseHeld.store(false);
    struct Head final : juce::AudioPlayHead
    {
        double bpm = 90, ppq = 0.1;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo p;
            p.setBpm(bpm);
            p.setPpqPosition(ppq);
            p.setIsPlaying(true);
            return p;
        }
    } head;
    p.setPlayHead(&head);
    p.setParam("sync", 1);
    p.setParam("quantize", 1);
    message(p, juce::MidiMessage::noteOn(1, 49, (juce::uint8)100));
    check(p.bpmDisplay.load() == 90 && std::abs(p.actualSpeed.load()) < 0.01f,
          "DAW tempo and PPQ quantization wait for grid");
    for (int i = 0; i < 40; ++i)
    {
        head.ppq += 256.0 / 48000 * head.bpm / 60;
        blocks(p, 1);
    }
    check(std::abs(p.actualSpeed.load()) > 0.2f, "quantized pattern starts on host beat grid");
    message(p, juce::MidiMessage::noteOff(1, 49));
    blocks(p, 40);
    check(std::abs(p.actualSpeed.load()) < 0.01f, "piano-roll Note Off releases pattern");
    p.setPlayHead(nullptr);
    p.setParam("sync", 0);
    p.setParam("quantize", 0);
    skratch::Take sequence;
    sequence.length = 0.25;
    sequence.bpm = 120;
    sequence.patternPriority = true;
    sequence.events.push_back({0, skratch::PatternNote, 1, 0.25, 1});
    p.replaceTake(sequence);
    p.takeLoop.store(false);
    p.playRecording(true);
    check(blocks(p, 12) > 0.01f, "internal piano-roll sequence renders pattern events");
    blocks(p, 30);
    check(!p.takePlaying.load(), "non-loop take stops at end");
    p.takeLoop.store(true);
    p.playRecording(true);
    blocks(p, 50);
    check(p.takePlaying.load() && p.takePosition.load() < 0.25, "take loop wraps playback clock");
    p.playRecording(false);
    const auto longFile = juce::File::getCurrentWorkingDirectory().getChildFile("work/fixtures/long.wav");
    if (longFile.existsAsFile())
    {
        p.loadFile(longFile);
        for (int i = 0; i < 1000 && p.loading.load(); ++i)
            juce::Thread::sleep(5);
        check(!p.loading.load() && std::abs(p.view().seconds - 300) < 0.01,
              "five-minute mono sample decodes without losing duration");
        p.setParam("play", 1);
        p.seek.store(0.9);
        check(blocks(p) > 0.01f, "long sample plays near end with sample-rate conversion");
    }
    p.setParam("gain", 6);
    p.keySpeed.store(12);
    const auto hotPeak = blocks(p);
    for (int c = 0; c < 2; ++c)
        juce::FloatVectorOperations::fill(audio.getWritePointer(c), 3.0f, 256);
    p.processBlock(audio, midi);
    check(hotPeak > 0 && std::isfinite(p.meter.load()) && audio.getMagnitude(0, 256) == 1,
          "high-speed and deliberately clipping input remain finite and bounded");
    p.keySpeed.store(0);
    p.setParam("gain", -6);
    juce::MidiFile ordinary;
    ordinary.setTicksPerQuarterNote(960);
    juce::MidiMessageSequence track;
    auto zero = juce::MidiMessage::pitchWheel(1, 8192);
    zero.setTimeStamp(0);
    track.addEvent(zero);
    auto end = juce::MidiMessage::endOfTrack();
    end.setTimeStamp(960);
    track.addEvent(end);
    ordinary.addTrack(track);
    auto rawFile = output.getChildFile("ordinary.mid");
    rawFile.deleteFile();
    {
        juce::FileOutputStream out(rawFile);
        check(ordinary.writeTo(out), "generic MIDI fixture writes");
    }
    check(p.importTake(rawFile).wasOk() && !p.getTake().platter.empty() &&
              p.getTake().platter.front().value == 0,
          "generic SMF center pitch bend imports as exact zero");
    std::cout << "DEVICE REPORT / native controller count: " << p.padDevices().size()
              << " (not hardware certification)" << std::endl;
    for (const auto &device : p.padDevices())
        std::cout << "DEVICE " << device.id << " / " << device.name << " / standard=" << device.standard
                  << std::endl;
}
