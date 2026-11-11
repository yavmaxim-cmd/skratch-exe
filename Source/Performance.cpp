#include "PluginProcessor.h"

namespace
{
juce::var takeJson(const skratch::Take &t)
{
    auto *o = new juce::DynamicObject;
    o->setProperty("format", "SKRATCH.TAKE.2");
    o->setProperty("bpm", t.bpm);
    o->setProperty("length", t.length);
    o->setProperty("patternPriority", t.patternPriority);
    for (int lane = 0; lane < 2; ++lane)
    {
        juce::Array<juce::var> a;
        for (const auto &p : lane == 0 ? t.platter : t.fader)
        {
            juce::Array<juce::var> pair;
            pair.add(p.beat);
            pair.add(p.value);
            a.add(juce::var(pair));
        }
        o->setProperty(lane == 0 ? "platter" : "fader", a);
    }
    juce::Array<juce::var> events;
    for (const auto &e : t.events)
    {
        juce::Array<juce::var> a;
        a.add(e.beat);
        a.add(e.type);
        a.add(e.value);
        a.add(e.duration);
        a.add(e.velocity);
        events.add(juce::var(a));
    }
    o->setProperty("events", events);
    return o;
}
bool readTake(const juce::var &json, skratch::Take &t)
{
    if (json["format"].toString() != "SKRATCH.TAKE.2")
        return false;
    t.clear();
    t.reserve();
    t.bpm = static_cast<double>(json["bpm"]);
    t.length = static_cast<double>(json["length"]);
    t.patternPriority = static_cast<bool>(json["patternPriority"]);
    if (!std::isfinite(t.length) || t.length <= 0 || t.length > 100000 || !std::isfinite(t.bpm) ||
        t.bpm < 20 || t.bpm > 400)
        return false;
    for (int lane = 0; lane < 2; ++lane)
    {
        const auto *a = json[lane == 0 ? "platter" : "fader"].getArray();
        if (!a || a->size() > skratch::maxPoints)
            return false;
        auto &points = lane == 0 ? t.platter : t.fader;
        for (const auto &item : *a)
        {
            auto *pair = item.getArray();
            if (!pair || pair->size() != 2)
                return false;
            const double beat = static_cast<double>((*pair)[0]);
            const float value = static_cast<float>((*pair)[1]);
            if (!std::isfinite(beat) || beat < 0 || beat > t.length + 0.001 || !std::isfinite(value) ||
                (lane == 0 ? (value < -12 || value > 12) : (value < 0 || value > 1)))
                return false;
            points.push_back({beat, value});
        }
        std::stable_sort(points.begin(), points.end(), [](auto a, auto b) { return a.beat < b.beat; });
    }
    if (auto *events = json["events"].getArray())
    {
        if (events->size() > skratch::maxEvents)
            return false;
        for (const auto &item : *events)
        {
            auto *a = item.getArray();
            if (!a || a->size() != 5)
                return false;
            skratch::Event e{static_cast<double>((*a)[0]), static_cast<int>((*a)[1]),
                             static_cast<double>((*a)[2]), static_cast<double>((*a)[3]),
                             static_cast<float>((*a)[4])};
            if (!std::isfinite(e.beat) || !std::isfinite(e.value) || !std::isfinite(e.duration) ||
                !std::isfinite(e.velocity) || e.beat < 0 || e.beat > t.length + 0.001 || e.type < 0 ||
                e.type > 5 || e.duration < 0 || e.velocity < 0 || e.velocity > 1)
                return false;
            if ((e.type == skratch::PatternNote && (e.value < 0 || e.value > 15)) ||
                ((e.type == skratch::Seek || e.type == skratch::Cue) && (e.value < 0 || e.value > 1)) ||
                (e.type == skratch::Tempo && (e.value < 20 || e.value > 400)))
                return false;
            t.events.push_back(e);
        }
        std::stable_sort(t.events.begin(), t.events.end(), [](auto a, auto b) { return a.beat < b.beat; });
    }
    return true;
}
juce::var padJson(const skratch::PadConfig &c)
{
    auto *o = new juce::DynamicObject;
    o->setProperty("device", c.device);
    o->setProperty("platterAxis", c.platterAxis);
    o->setProperty("faderAxis", c.faderAxis);
    o->setProperty("slowAxis", c.slowAxis);
    o->setProperty("fastAxis", c.fastAxis);
    o->setProperty("deadzone", c.deadzone);
    o->setProperty("sensitivity", c.sensitivity);
    o->setProperty("curve", c.curve);
    o->setProperty("maxSpeed", c.maxSpeed);
    o->setProperty("cutIn", c.cutIn);
    o->setProperty("invertPlatter", c.invertPlatter);
    o->setProperty("invertFader", c.invertFader);
    o->setProperty("binary", c.binary);
    o->setProperty("enabled", c.enabled);
    juce::Array<juce::var> calibration, buttons;
    for (const auto &a : c.calibration)
    {
        juce::Array<juce::var> x;
        x.add(a.minimum);
        x.add(a.center);
        x.add(a.maximum);
        calibration.add(x);
    }
    for (auto b : c.buttons)
        buttons.add(b);
    o->setProperty("calibration", calibration);
    o->setProperty("buttons", buttons);
    return o;
}
skratch::PadConfig readPad(const juce::var &v)
{
    skratch::PadConfig c;
    if (!v.isObject())
        return c;
    const auto bounded = [&](const char *key, double lo, double hi, double fallback)
    {
        const auto a = v.getProperty(key, fallback);
        const double x = static_cast<double>(a);
        return std::isfinite(x) ? juce::jlimit(lo, hi, x) : fallback;
    };
    c.device = static_cast<int>(bounded("device", -1, 115, -1));
    c.platterAxis = static_cast<int>(bounded("platterAxis", 0, 7, 1));
    c.faderAxis = static_cast<int>(bounded("faderAxis", 0, 7, 2));
    c.slowAxis = static_cast<int>(bounded("slowAxis", -1, 7, 4));
    c.fastAxis = static_cast<int>(bounded("fastAxis", -1, 7, 5));
    c.deadzone = static_cast<float>(bounded("deadzone", 0, 0.95, 0.12));
    c.sensitivity = static_cast<float>(bounded("sensitivity", 0.1, 3, 1));
    c.curve = static_cast<float>(bounded("curve", 0.2, 4, 1.5));
    c.maxSpeed = static_cast<float>(bounded("maxSpeed", 0.25, 12, 4));
    c.cutIn = static_cast<float>(bounded("cutIn", 0, 1, 0.04));
    c.invertPlatter = static_cast<bool>(v.getProperty("invertPlatter", true));
    c.invertFader = static_cast<bool>(v["invertFader"]);
    c.binary = static_cast<bool>(v["binary"]);
    c.enabled = static_cast<bool>(v["enabled"]);
    if (auto *a = v["calibration"].getArray())
        for (int i = 0; i < std::min(8, a->size()); ++i)
        {
            auto *p = (*a)[i].getArray();
            if (!p || p->size() != 3)
                continue;
            const float lo = static_cast<float>((*p)[0]), center = static_cast<float>((*p)[1]),
                        hi = static_cast<float>((*p)[2]);
            if (std::isfinite(lo) && std::isfinite(center) && std::isfinite(hi) && lo < center &&
                center < hi && lo >= -1.1f && hi <= 1.1f)
                c.calibration[i] = {lo, center, hi};
        }
    if (auto *a = v["buttons"].getArray())
        for (int i = 0; i < std::min(8, a->size()); ++i)
            c.buttons[i] = juce::jlimit(0, 31, static_cast<int>((*a)[i]));
    return c;
}
} // namespace

void NeedleProcessor::stopDeck()
{
    setParam("play", 0);
    keySpeed.store(0);
    mouseHeld.store(false);
    patternRelease.store(99);
}
void NeedleProcessor::nextCue()
{
    cue.store(std::fmod(cue.load() + 0.125, 1.0));
    returnToCue();
}
void NeedleProcessor::triggerPattern(int slot, float velocity, double beats)
{
    patternRequestVelocity.store(juce::jlimit(0.0f, 1.0f, velocity));
    patternRequestLength.store(
        juce::jlimit(0.0625, 16.0, beats <= 0 ? static_cast<double>(param("length")) : beats));
    patternRequest.store(juce::jlimit(0, 15, slot));
    selectedPattern.store(juce::jlimit(0, 15, slot));
}
void NeedleProcessor::releasePattern(int slot)
{
    patternRelease.store(slot);
}
skratch::Pattern NeedleProcessor::getPattern(int slot) const
{
    const juce::SpinLock::ScopedLockType lock(takeLock);
    return patterns[juce::jlimit(0, 15, slot)];
}
void NeedleProcessor::editPattern(int slot, int lane, int node, float value)
{
    const juce::SpinLock::ScopedLockType lock(takeLock);
    auto &p = patterns[juce::jlimit(0, 15, slot)];
    (lane == 0 ? p.platter : p.fader)[juce::jlimit(0, skratch::nodes - 1, node)] =
        juce::jlimit(lane == 0 ? -6.0f : 0.0f, lane == 0 ? 6.0f : 1.0f, value);
    patternsDirty = true;
}
juce::StringArray NeedleProcessor::presetNames()
{
    return {
        "NU-METAL / Hybrid Cut",          "NU-METAL / Steel Impact",      "HIP-HOP 2000 / Oldschool Cut",
        "HIP-HOP 2000 / Concrete Break",  "TURNTABLISM / DJ Battle",      "TURNTABLISM / Orbit Lab",
        "BREAKBEAT / Breakbeat 2003",     "BREAKBEAT / Chopped Ride",     "INDUSTRIAL / Mechanical Scratch",
        "INDUSTRIAL / Vinyl Destruction", "EXPERIMENTAL / Digital Chaos", "EXPERIMENTAL / Tape Collapse"};
}
void NeedleProcessor::applyPreset(int i)
{
    i = juce::jlimit(0, 11, i);
    presetIndex.store(i);
    static const int slots[] = {3, 5, 0, 7, 6, 8, 4, 1, 7, 2, 8, 9};
    selectedPattern.store(slots[i]);
    setParam("mode", i >= 10 ? 3.0f : (i >= 8 ? 2.0f : 0.0f));
    setParam("cutMode", 1);
    setParam("cutIn", i >= 8 ? 0.015f : 0.04f);
    setParam("curve", i % 3 == 0 ? 0.4f : 1.0f);
    setParam("response", i >= 8 ? 0.6f : 1.5f);
}
void NeedleProcessor::rememberUndo()
{
    undo = take;
    canUndo = true;
    canRedo = false;
}
void NeedleProcessor::startRecording()
{
    const juce::SpinLock::ScopedLockType lock(takeLock);
    rememberUndo();
    take.clear();
    take.patternPriority = false;
    take.bpm = bpmDisplay.load();
    recordBeat = 0;
    recordCountdown = 0;
    takePlaying.store(false);
    recording.store(true);
    addRecordedEvent(skratch::Seek, progress.load());
    addRecordedEvent(skratch::Transport, param("play") > 0.5f ? 1 : 0);
    addRecordedEvent(skratch::Tempo, take.bpm);
    addRecordedEvent(skratch::Cue, cue.load());
    addRecordedEvent(skratch::Cut, cutHeld.load() || midiCut ? 1 : 0);
}
void NeedleProcessor::stopRecording()
{
    const juce::SpinLock::ScopedLockType lock(takeLock);
    recording.store(false);
    take.length = std::max(0.0625, recordBeat);
}
void NeedleProcessor::playRecording(bool play)
{
    const juce::SpinLock::ScopedLockType lock(takeLock);
    patternRelease.store(99);
    takeTransport = -1;
    takeCut = false;
    if (play)
    {
        recording.store(false);
        playBeat = 0;
        speedCursor = faderCursor = eventCursor = 0;
        playbackTempo = take.bpm;
    }
    takePlaying.store(play && (!take.platter.empty() || !take.events.empty()));
}
void NeedleProcessor::addRecordedEvent(int type, double value, double duration, float velocity)
{
    if (recording.load() && take.events.size() < skratch::maxEvents)
        take.events.push_back({recordBeat, type, value, duration, velocity});
}
skratch::Take NeedleProcessor::getTake(int displayPoints) const
{
    if (displayPoints <= 0)
    {
        const juce::SpinLock::ScopedLockType lock(takeLock);
        return take;
    }
    skratch::Take snapshot;
    snapshot.platter.reserve(displayPoints + 1);
    snapshot.fader.reserve(displayPoints + 1);
    snapshot.events.reserve(skratch::maxEvents);
    const juce::SpinLock::ScopedLockType lock(takeLock);
    snapshot.length = take.length;
    snapshot.bpm = take.bpm;
    snapshot.patternPriority = take.patternPriority;
    snapshot.events = take.events;
    for (int lane = 0; lane < 2; ++lane)
    {
        const auto &source = lane == 0 ? take.platter : take.fader;
        auto &target = lane == 0 ? snapshot.platter : snapshot.fader;
        const size_t step = std::max(size_t(1), (source.size() + displayPoints - 1) / displayPoints);
        for (size_t i = 0; i < source.size(); i += step)
            target.push_back(source[i]);
        if (!source.empty() && (target.empty() || target.back().beat != source.back().beat))
            target.push_back(source.back());
    }
    return snapshot;
}
void NeedleProcessor::replaceTake(const skratch::Take &t)
{
    const juce::SpinLock::ScopedLockType lock(takeLock);
    recording.store(false);
    takePlaying.store(false);
    rememberUndo();
    take = t;
    take.reserve();
    playBeat = 0;
}
bool NeedleProcessor::undoTake(bool forward)
{
    const juce::SpinLock::ScopedLockType lock(takeLock);
    if (recording.load() || (forward ? !canRedo : !canUndo))
        return false;
    takePlaying.store(false);
    if (forward)
    {
        undo = take;
        take = redo;
        canUndo = true;
        canRedo = false;
    }
    else
    {
        redo = take;
        take = undo;
        canUndo = false;
        canRedo = true;
    }
    take.reserve();
    return true;
}
void NeedleProcessor::quantizeRecording(double grid)
{
    if (grid <= 0 || !std::isfinite(grid))
        return;
    const juce::SpinLock::ScopedLockType lock(takeLock);
    if (recording.load())
        return;
    takePlaying.store(false);
    rememberUndo();
    const auto snap = [&](double beat) { return std::max(0.0, std::round(beat / grid) * grid); };
    for (auto *lane : {&take.platter, &take.fader})
    {
        for (auto &p : *lane)
            p.beat = snap(p.beat);
        std::stable_sort(lane->begin(), lane->end(), [](auto a, auto b) { return a.beat < b.beat; });
        // Last value at each snapped time wins; no zero-duration interpolation segments.
        size_t n = 0;
        for (const auto p : *lane)
        {
            if (n > 0 && std::abs((*lane)[n - 1].beat - p.beat) < 1e-9)
                (*lane)[n - 1] = p;
            else
                (*lane)[n++] = p;
        }
        lane->resize(n);
    }
    for (auto &e : take.events)
    {
        e.beat = snap(e.beat);
        if (e.type == skratch::PatternNote)
            e.duration = std::max(grid, snap(e.duration));
    }
    std::stable_sort(take.events.begin(), take.events.end(), [](auto a, auto b) { return a.beat < b.beat; });
    take.length = std::max(grid, snap(take.length));
}
void NeedleProcessor::editTakePoint(int lane, double beat, float value)
{
    const juce::SpinLock::ScopedLockType lock(takeLock);
    if (recording.load() || !std::isfinite(beat) || !std::isfinite(value))
        return;
    takePlaying.store(false);
    rememberUndo();
    auto &points = lane == 0 ? take.platter : take.fader;
    beat = std::max(0.0, beat);
    value = juce::jlimit(lane == 0 ? -6.0f : 0.0f, lane == 0 ? 6.0f : 1.0f, value);
    auto it =
        std::lower_bound(points.begin(), points.end(), beat, [](auto p, double b) { return p.beat < b; });
    if (it != points.end() && std::abs(it->beat - beat) < 0.03)
    {
        it->beat = beat;
        it->value = value;
    }
    else if (points.size() < skratch::maxPoints)
        points.insert(it, {beat, value});
    take.length = std::max({0.25, take.length, beat});
}

juce::Result NeedleProcessor::exportTake(const juce::File &file, bool midi)
{
    const auto t = getTake();
    if (t.length <= 0)
        return juce::Result::fail("No take to export");
    if (!midi)
        return file.replaceWithText(juce::JSON::toString(takeJson(t)))
                   ? juce::Result::ok()
                   : juce::Result::fail("Cannot write take");
    juce::MidiFile smf;
    smf.setTicksPerQuarterNote(960);
    juce::MidiMessageSequence tempo, platterTrack, faderTrack, events;
    const auto add = [](juce::MidiMessageSequence &seq, juce::MidiMessage m, double beat)
    {
        m.setTimeStamp(beat * 960);
        seq.addEvent(m);
    };
    add(tempo, juce::MidiMessage::tempoMetaEvent(static_cast<int>(60000000 / t.bpm)), 0);
    add(tempo, juce::MidiMessage::timeSignatureMetaEvent(4, 4), 0);
    add(tempo, juce::MidiMessage::textMetaEvent(1, "SKRATCH-TAKE:" + juce::JSON::toString(takeJson(t), true)),
        0);
    add(platterTrack, juce::MidiMessage::textMetaEvent(3, "PLATTER MOTION / pitch bend +/-6x"), 0);
    add(faderTrack, juce::MidiMessage::textMetaEvent(3, "CROSSFADER / CC1+33 14-bit"), 0);
    for (const auto p : t.platter)
        add(platterTrack,
            juce::MidiMessage::pitchWheel(
                1, juce::jlimit(
                       0, 16383,
                       static_cast<int>(std::round(8192 + p.value / 6.0f * (p.value < 0 ? 8192 : 8191))))),
            p.beat);
    for (const auto p : t.fader)
    {
        const int v = juce::jlimit(0, 16383, static_cast<int>(std::round(p.value * 16383)));
        add(faderTrack, juce::MidiMessage::controllerEvent(1, 1, v >> 7), p.beat);
        add(faderTrack, juce::MidiMessage::controllerEvent(1, 33, v & 127), p.beat);
    }
    for (const auto e : t.events)
    {
        if (e.type == skratch::PatternNote)
        {
            const int note = noteMap[juce::jlimit(0, 15, static_cast<int>(e.value))].load();
            add(events,
                juce::MidiMessage::noteOn(
                    1, note,
                    static_cast<juce::uint8>(juce::jlimit(1, 127, static_cast<int>(e.velocity * 127)))),
                e.beat);
            add(events, juce::MidiMessage::noteOff(1, note), e.beat + std::max(0.0625, e.duration));
        }
        else if (e.type == skratch::Tempo)
            add(tempo, juce::MidiMessage::tempoMetaEvent(static_cast<int>(60000000 / e.value)), e.beat);
        else if (e.type == skratch::Seek || e.type == skratch::Cue)
        {
            const int v = juce::jlimit(0, 16383, static_cast<int>(e.value * 16383));
            add(events, juce::MidiMessage::controllerEvent(1, 20, v >> 7), e.beat);
            add(events, juce::MidiMessage::controllerEvent(1, 52, v & 127), e.beat);
        }
        else if (e.type == skratch::Transport)
            add(events, juce::MidiMessage::controllerEvent(1, 110, e.value > 0 ? 127 : 0), e.beat);
        else if (e.type == skratch::Cut)
            add(events,
                e.value > 0 ? juce::MidiMessage::noteOn(1, 38, static_cast<juce::uint8>(127))
                            : juce::MidiMessage::noteOff(1, 38),
                e.beat);
    }
    for (auto *seq : {&tempo, &platterTrack, &faderTrack, &events})
    {
        add(*seq, juce::MidiMessage::endOfTrack(), t.length);
        smf.addTrack(*seq);
    }
    juce::TemporaryFile temporary(file);
    {
        juce::FileOutputStream out(temporary.getFile());
        if (!out.openedOk() || !smf.writeTo(out))
            return juce::Result::fail("Cannot write MIDI");
        out.flush();
        if (out.getStatus().failed())
            return out.getStatus();
    }
    return temporary.overwriteTargetFileWithTemporary() ? juce::Result::ok()
                                                        : juce::Result::fail("Cannot replace MIDI file");
}
juce::Result NeedleProcessor::importTake(const juce::File &file)
{
    if (file.getSize() > 32 * 1024 * 1024)
        return juce::Result::fail("Import limit: 32 MB");
    skratch::Take t;
    if (file.hasFileExtension("mid;midi"))
    {
        juce::FileInputStream in(file);
        juce::MidiFile smf;
        if (!in.openedOk() || !smf.readFrom(in) || smf.getTimeFormat() <= 0)
            return juce::Result::fail("Expected PPQ Standard MIDI File");
        bool exact = false;
        for (int track = 0; track < smf.getNumTracks(); ++track)
            for (const auto *e : *smf.getTrack(track))
                if (e->message.isTextMetaEvent() &&
                    e->message.getTextFromTextMetaEvent().startsWith("SKRATCH-TAKE:"))
                    exact =
                        readTake(juce::JSON::parse(e->message.getTextFromTextMetaEvent().substring(13)), t);
        if (!exact)
        {
            t.reserve();
            t.length = std::max(0.25, smf.getLastTimestamp() / smf.getTimeFormat());
            int msb = 127, lsb = 127, seekMSB = 0, seekLSB = 0;
            for (int track = 0; track < smf.getNumTracks(); ++track)
            {
                const auto *seq = smf.getTrack(track);
                for (int i = 0; i < seq->getNumEvents(); ++i)
                {
                    const auto *h = seq->getEventPointer(i);
                    const auto &m = h->message;
                    const double beat = m.getTimeStamp() / smf.getTimeFormat();
                    if (m.isPitchWheel() && t.platter.size() < skratch::maxPoints)
                    {
                        const int v = m.getPitchWheelValue() - 8192;
                        t.platter.push_back({beat, v / static_cast<float>(v < 0 ? 8192 : 8191) * 6});
                    }
                    if (m.isController() && (m.getControllerNumber() == 1 || m.getControllerNumber() == 33) &&
                        t.fader.size() < skratch::maxPoints)
                    {
                        if (m.getControllerNumber() == 1)
                            msb = m.getControllerValue();
                        else
                            lsb = m.getControllerValue();
                        t.fader.push_back({beat, (msb * 128 + lsb) / 16383.0f});
                    }
                    int mapped = -1;
                    if (m.isNoteOn())
                        for (int slot = 0; slot < 16; ++slot)
                            if (noteMap[slot].load() == m.getNoteNumber())
                            {
                                mapped = slot;
                                break;
                            }
                    if (mapped >= 0 && t.events.size() < skratch::maxEvents)
                    {
                        const double duration =
                            h->noteOffObject
                                ? std::max(0.0625,
                                           (h->noteOffObject->message.getTimeStamp() - m.getTimeStamp()) /
                                               smf.getTimeFormat())
                                : 1;
                        t.events.push_back({beat, skratch::PatternNote, static_cast<double>(mapped), duration,
                                            m.getFloatVelocity()});
                    }
                    if (m.isTempoMetaEvent() && t.events.size() < skratch::maxEvents)
                    {
                        const double bpm = juce::jlimit(20.0, 400.0, 60 / m.getTempoSecondsPerQuarterNote());
                        if (beat < 1e-9)
                            t.bpm = bpm;
                        t.events.push_back({beat, skratch::Tempo, bpm});
                    }
                    if (m.isController() && m.getControllerNumber() == 110 &&
                        t.events.size() < skratch::maxEvents)
                        t.events.push_back(
                            {beat, skratch::Transport, m.getControllerValue() > 63 ? 1.0 : 0.0});
                    if (m.isController() &&
                        (m.getControllerNumber() == 20 || m.getControllerNumber() == 52) &&
                        t.events.size() < skratch::maxEvents)
                    {
                        if (m.getControllerNumber() == 20)
                        {
                            seekMSB = m.getControllerValue();
                            seekLSB = 0;
                        }
                        else
                            seekLSB = m.getControllerValue();
                        t.events.push_back({beat, skratch::Seek, (seekMSB * 128 + seekLSB) / 16383.0});
                    }
                    if ((m.isNoteOn() || m.isNoteOff()) && m.getNoteNumber() == 38 &&
                        t.events.size() < skratch::maxEvents)
                        t.events.push_back({beat, skratch::Cut, m.isNoteOn() ? 1.0 : 0.0});
                }
            }
            std::stable_sort(t.platter.begin(), t.platter.end(),
                             [](auto a, auto b) { return a.beat < b.beat; });
            std::stable_sort(t.fader.begin(), t.fader.end(), [](auto a, auto b) { return a.beat < b.beat; });
            std::stable_sort(t.events.begin(), t.events.end(),
                             [](auto a, auto b) { return a.beat < b.beat; });
        }
    }
    else if (!readTake(juce::JSON::parse(file.loadFileAsString()), t))
        return juce::Result::fail("Invalid SKRATCH take");
    replaceTake(t);
    return juce::Result::ok();
}

skratch::PadConfig NeedleProcessor::getPadConfig() const
{
    const juce::ScopedLock lock(padMutex);
    return padConfig;
}
void NeedleProcessor::setPadConfig(const skratch::PadConfig &c)
{
    const juce::ScopedLock lock(padMutex);
    padConfig = readPad(padJson(c));
}
skratch::PadState NeedleProcessor::getPadState() const
{
    const juce::ScopedLock lock(padMutex);
    return padState;
}
void NeedleProcessor::calibratePad(int stage)
{
    const juce::ScopedLock lock(padMutex);
    for (int i = 0; i < padState.axisCount; ++i)
    {
        auto &a = padConfig.calibration[i];
        const float v = padState.axes[i];
        if (stage == 0)
            a.center = juce::jlimit(-0.95f, 0.95f, v);
        if (stage == 1)
            a.minimum = std::min(v, a.center - 0.01f);
        if (stage == 2)
            a.maximum = std::max(v, a.center + 0.01f);
    }
}
void NeedleProcessor::publishPadAxes(bool active, float speed, float fader)
{
    const bool measure = latencyMetrics.enabled.load(std::memory_order_relaxed);
    const bool changed =
        measure && (active != padActive.load() || speed != padSpeed.load() || fader != padFader.load());
    padSpeed.store(speed);
    padFader.store(fader);
    padActive.store(active);
    if (changed)
        latencyMetrics.padTick.store(juce::Time::getHighResolutionTicks(), std::memory_order_release);
}
void NeedleProcessor::run()
{
    while (!threadShouldExit())
    {
        if (retiredClip.load(std::memory_order_acquire) != nullptr)
            delete retiredClip.exchange(nullptr, std::memory_order_acq_rel);
        const auto cfg = getPadConfig();
        const auto raw = gamepad.poll(cfg.device);
        const auto output = skratch::mapPad(raw, cfg);
        {
            const juce::ScopedLock lock(padMutex);
            padState = raw;
        }
        padConnected.store(raw.connected);
        publishPadAxes(output.active, output.speed, output.fader);
        const bool fxBank = cfg.enabled && djValue(dj::padBank) > .5f;
        constexpr dj::Param buttonParams[]{dj::padEcho,    dj::padBrake, dj::padSpin, dj::padHotCue,
                                           dj::padStutter, dj::padFx,    dj::padReset};
        const int padCueSlot = std::clamp(static_cast<int>(djValue(dj::padCue)) - 1, 0, 7);
        if (fxBank && raw.connected)
        {
            std::array<int, 7> indices;
            for (size_t action = 0; action < std::size(buttonParams); ++action)
                indices[action] = static_cast<int>(djValue(buttonParams[action]));
            const auto edges = dj::mapFxButtons(raw.buttons, previousPadButtons, indices);
            if (edges.actions != 0)
                djActions.fetch_or(edges.actions, std::memory_order_relaxed);
            if (edges.cue > 0)
            {
                heldPadCue = padCueSlot;
                padCueRequests.fetch_or(1u << heldPadCue, std::memory_order_relaxed);
            }
            if (heldPadCue >= 0 && (edges.cue < 0 || indices[dj::cueAction] < 0 ||
                                    indices[dj::cueAction] >= 32 || !raw.buttons[indices[dj::cueAction]]))
            {
                padCueRequests.fetch_or(1u << (heldPadCue + 8), std::memory_order_relaxed);
                heldPadCue = -1;
            }
        }
        else if (raw.connected && cfg.enabled)
        {
            int actions = 0;
            for (int a = 0; a < 8; ++a)
            {
                const int b = cfg.buttons[a];
                if (b >= 0 && b < 32 && raw.buttons[b] && !previousPadButtons[b])
                    actions |= 1 << a;
            }
            if (actions)
            {
                pendingActions.fetch_or(actions);
                triggerAsyncUpdate();
            }
        }
        if (heldPadCue >= 0 && (!fxBank || !raw.connected))
        {
            padCueRequests.fetch_or(1u << (heldPadCue + 8), std::memory_order_relaxed);
            heldPadCue = -1;
        }
        previousPadButtons = raw.connected ? raw.buttons : std::array<bool, 32>{};
        wait(4);
    }
    padActive.store(false);
}
void NeedleProcessor::handleAsyncUpdate()
{
    const int actions = pendingActions.exchange(0);
    if (actions & 1)
    {
        returnToCue();
        setParam("play", 1);
    }
    if (actions & 2)
        stopDeck();
    if (actions & 4)
        nextCue();
    if (actions & 8)
    {
        if (recording.load())
            stopRecording();
        else
            startRecording();
    }
    if (actions & 16)
        nextSample(-1);
    if (actions & 32)
        nextSample(1);
    if (actions & 64)
        applyPreset((presetIndex.load() + 11) % 12);
    if (actions & 128)
        applyPreset((presetIndex.load() + 1) % 12);
}
void NeedleProcessor::addSamples(const juce::StringArray &files)
{
    {
        const juce::ScopedLock lock(listMutex);
        for (const auto &f : files)
            if (juce::File(f).hasFileExtension("wav;mp3;flac;aif;aiff;ogg") && !sampleList.contains(f))
                sampleList.add(f);
    }
    if (files.size())
    {
        const juce::ScopedLock lock(listMutex);
        sampleIndex = sampleList.indexOf(files[0]);
    }
    if (files.size())
        loadFile(juce::File(files[0]));
}
juce::StringArray NeedleProcessor::sampleFiles() const
{
    const juce::ScopedLock lock(listMutex);
    return sampleList;
}
void NeedleProcessor::nextSample(int direction)
{
    juce::String path;
    {
        const juce::ScopedLock lock(listMutex);
        if (sampleList.isEmpty())
            return;
        sampleIndex = (sampleIndex + direction + sampleList.size()) % sampleList.size();
        path = sampleList[sampleIndex];
    }
    cue.store(0);
    loadFile(juce::File(path));
}
juce::var NeedleProcessor::extraState() const
{
    skratch::Take snapshot;
    snapshot.reserve();
    std::array<skratch::Pattern, 16> patternSnapshot;
    {
        const juce::SpinLock::ScopedLockType lock(takeLock);
        snapshot = take;
        patternSnapshot = patterns;
    }
    auto *o = new juce::DynamicObject;
    {
        o->setProperty("take", takeJson(snapshot));
        o->setProperty("takeLoop", takeLoop.load());
        o->setProperty("selectedPattern", selectedPattern.load());
        o->setProperty("presetIndex", presetIndex.load());
        juce::Array<juce::var> slots;
        for (const auto &p : patternSnapshot)
        {
            auto *x = new juce::DynamicObject;
            juce::Array<juce::var> a, b;
            for (auto v : p.platter)
                a.add(v);
            for (auto v : p.fader)
                b.add(v);
            x->setProperty("platter", a);
            x->setProperty("fader", b);
            slots.add(x);
        }
        o->setProperty("patterns", slots);
    }
    o->setProperty("pad", padJson(getPadConfig()));
    o->setProperty("speedCC", speedCC.load());
    o->setProperty("faderCC", faderCC.load());
    o->setProperty("positionCC", positionCC.load());
    juce::Array<juce::var> live;
    for (const auto &cc : liveCC)
        live.add(cc.load());
    o->setProperty("liveCC", live);
    o->setProperty("channel", midiChannel.load());
    juce::Array<juce::var> notes;
    for (const auto &n : noteMap)
        notes.add(n.load());
    o->setProperty("notes", notes);
    juce::Array<juce::var> files;
    for (const auto &f : sampleFiles())
        files.add(f);
    o->setProperty("samples", files);
    writeDjState(*o, true);
    return o;
}
void NeedleProcessor::restoreExtra(const juce::var &v)
{
    if (!v.isObject())
        return;
    takeLoop.store(static_cast<bool>(v.getProperty("takeLoop", true)));
    selectedPattern.store(juce::jlimit(0, 15, static_cast<int>(v["selectedPattern"])));
    presetIndex.store(juce::jlimit(0, 11, static_cast<int>(v["presetIndex"])));
    skratch::Take t;
    const bool valid = readTake(v["take"], t);
    {
        const juce::SpinLock::ScopedLockType lock(takeLock);
        recording.store(false);
        takePlaying.store(false);
        if (valid)
        {
            take = t;
            take.reserve();
        }
        if (auto *slots = v["patterns"].getArray())
        {
            for (int i = 0; i < std::min(16, slots->size()); ++i)
                for (int lane = 0; lane < 2; ++lane)
                    if (auto *a = (*slots)[i][lane == 0 ? "platter" : "fader"].getArray())
                        if (a->size() == skratch::nodes)
                            for (int j = 0; j < skratch::nodes; ++j)
                            {
                                const float x = static_cast<float>((*a)[j]);
                                if (std::isfinite(x))
                                    (lane == 0 ? patterns[i].platter : patterns[i].fader)[j] =
                                        juce::jlimit(lane == 0 ? -6.0f : 0.0f, lane == 0 ? 6.0f : 1.0f, x);
                            }
            patternsDirty = true;
        }
    }
    setPadConfig(readPad(v["pad"]));
    if (auto *a = v["notes"].getArray())
        for (int i = 0; i < std::min(16, a->size()); ++i)
            noteMap[i].store(juce::jlimit(0, 127, static_cast<int>((*a)[i])));
    speedCC.store(juce::jlimit(0, 127, static_cast<int>(v.getProperty("speedCC", 16))));
    faderCC.store(juce::jlimit(0, 127, static_cast<int>(v.getProperty("faderCC", 1))));
    positionCC.store(juce::jlimit(0, 127, static_cast<int>(v.getProperty("positionCC", 20))));
    const auto *savedLive = v["liveCC"].getArray();
    for (size_t i = 0; i < liveCC.size(); ++i)
        liveCC[i].store(savedLive && static_cast<int>(i) < savedLive->size()
                            ? juce::jlimit(-1, 127, static_cast<int>((*savedLive)[static_cast<int>(i)]))
                            : -1);
    resetMidi.store(true);
    midiChannel.store(juce::jlimit(0, 16, static_cast<int>(v["channel"])));
    if (auto *files = v["samples"].getArray())
    {
        const juce::ScopedLock lock(listMutex);
        sampleList.clear();
        for (const auto &f : *files)
            if (sampleList.size() < 512)
                sampleList.add(f.toString());
    }
}
juce::Result NeedleProcessor::exportMapping(const juce::File &f)
{
    auto *o = new juce::DynamicObject;
    o->setProperty("format", "SKRATCH.MAPPING.2");
    o->setProperty("pad", padJson(getPadConfig()));
    o->setProperty("speedCC", speedCC.load());
    o->setProperty("faderCC", faderCC.load());
    o->setProperty("positionCC", positionCC.load());
    juce::Array<juce::var> live;
    for (const auto &cc : liveCC)
        live.add(cc.load());
    o->setProperty("liveCC", live);
    o->setProperty("channel", midiChannel.load());
    juce::Array<juce::var> notes;
    for (const auto &n : noteMap)
        notes.add(n.load());
    o->setProperty("notes", notes);
    writeDjState(*o, false);
    juce::Array<juce::var> padBindings;
    for (size_t index = dj::padBank; index <= dj::padReset; ++index)
        padBindings.add(djValue(static_cast<dj::Param>(index)));
    o->setProperty("djPad", padBindings);
    return f.replaceWithText(juce::JSON::toString(o)) ? juce::Result::ok()
                                                      : juce::Result::fail("Cannot save mapping");
}
juce::Result NeedleProcessor::importMapping(const juce::File &f)
{
    if (f.getSize() > 1024 * 1024)
        return juce::Result::fail("Mapping too large");
    const auto v = juce::JSON::parse(f.loadFileAsString());
    if (v["format"].toString() != "SKRATCH.MAPPING.2")
        return juce::Result::fail("Invalid mapping");
    if (!v["pad"].isObject())
        return juce::Result::fail("Mapping has no controller configuration");
    for (const auto *key : {"speedCC", "faderCC", "positionCC", "channel"})
    {
        const auto a = v[key];
        if (!(a.isInt() || a.isInt64()) || static_cast<int>(a) < 0 ||
            static_cast<int>(a) > (juce::String(key) == "channel" ? 16 : 127))
            return juce::Result::fail("Invalid MIDI mapping value");
    }
    // Mapping-only import must not replace the user's current take or browser.
    if (auto *a = v["notes"].getArray())
        for (int i = 0; i < std::min(16, a->size()); ++i)
            noteMap[i].store(juce::jlimit(0, 127, static_cast<int>((*a)[i])));
    setPadConfig(readPad(v["pad"]));
    speedCC.store(juce::jlimit(0, 127, static_cast<int>(v["speedCC"])));
    faderCC.store(juce::jlimit(0, 127, static_cast<int>(v["faderCC"])));
    positionCC.store(juce::jlimit(0, 127, static_cast<int>(v["positionCC"])));
    midiChannel.store(juce::jlimit(0, 16, static_cast<int>(v["channel"])));
    if (const auto *live = v["liveCC"].getArray())
        for (int i = 0; i < std::min(static_cast<int>(liveCC.size()), live->size()); ++i)
            liveCC[static_cast<size_t>(i)].store(juce::jlimit(-1, 127, static_cast<int>((*live)[i])));
    if (v["djCC"].isArray())
        restoreDjState(v, false);
    if (const auto *bindings = v["djPad"].getArray())
        for (int i = 0; i < std::min(static_cast<int>(dj::padReset - dj::padBank + 1), bindings->size()); ++i)
        {
            const auto &definition = dj::definitions[dj::padBank + static_cast<size_t>(i)];
            const float value = static_cast<float>((*bindings)[i]);
            if (std::isfinite(value))
                setParam(definition.id, std::clamp(value, definition.minimum, definition.maximum));
        }
    resetMidi.store(true);
    return juce::Result::ok();
}
juce::Result NeedleProcessor::savePreset(const juce::File &file)
{
    juce::MemoryBlock block;
    getStateInformation(block);
    return file.replaceWithData(block.getData(), block.getSize()) ? juce::Result::ok()
                                                                  : juce::Result::fail("Cannot save preset");
}
juce::Result NeedleProcessor::loadPreset(const juce::File &file)
{
    juce::MemoryBlock block;
    if (file.getSize() > 32 * 1024 * 1024 || !file.loadFileAsData(block))
        return juce::Result::fail("Invalid preset");
    const auto xml = getXmlFromBinary(block.getData(), static_cast<int>(block.getSize()));
    if (!xml || !xml->hasTagName(state.state.getType().toString()))
        return juce::Result::fail("Invalid preset type");
    setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    return juce::Result::ok();
}
