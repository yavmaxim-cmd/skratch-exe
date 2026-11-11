#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
const needle::SincReader &sincTable()
{
    static const needle::SincReader table;
    return table;
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout NeedleProcessor::parameters(bool instrument)
{
    juce::AudioProcessorValueTreeState::ParameterLayout p;
    p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"fader", 1}, "Crossfader", 0.0f, 1.0f,
                                                      1.0f));
    p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"gain", 1}, "Output dB", -36.0f, 6.0f,
                                                      -6.0f));
    p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"pitch", 1}, "Pitch %", -50.0f, 50.0f,
                                                      0.0f));
    p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"sensitivity", 1},
                                                      "Scratch sensitivity", 0.25f, 3.0f, 1.0f));
    p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"scratch", 1}, "Scratch velocity",
                                                      -6.0f, 6.0f, 0.0f));
    p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"play", 1}, "Play", false));
    p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"loop", 1}, "Loop", true));
    p.add(
        std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"reverse", 1}, "Hamster fader", false));
    p.add(
        std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"cutMode", 1}, "Cut fader mode", true));
    p.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"mode", 1}, "Scratch engine",
        juce::StringArray{"Classic Vinyl", "Direct Scratch", "Tape Scratch", "Digital Scratch"}, 0));
    p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"cutIn", 1}, "Cut-in distance",
                                                      0.001f, 0.4f, 0.04f));
    p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"curve", 1}, "Fader curve", 0.15f,
                                                      4.0f, 1.0f));
    p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"response", 1}, "Fader response ms",
                                                      0.1f, 15.0f, 1.5f));
    p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"bpm", 1}, "Internal BPM", 40.0f,
                                                      240.0f, 120.0f));
    p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"swing", 1}, "Pattern swing", 0.0f,
                                                      0.65f, 0.0f));
    p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"quantize", 1},
                                                       "Trigger quantization",
                                                       juce::StringArray{"Off", "1/16", "1/8", "1/4"}, 0));
    p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"rpm", 1}, "RPM",
                                                       juce::StringArray{"33", "45"}, 0));
    p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"sync", 1}, "Host tempo sync", true));
    p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"deckReverse", 1}, "Record reverse",
                                                     false));
    p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"length", 1}, "Pattern length beats",
                                                      0.0625f, 16.0f, 1.0f));
    p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"performance", 1}, "Low latency",
                                                     instrument));
    p.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"midiMode", 1}, "MIDI performance mode",
        juce::StringArray{"Scratch Patterns", "One Shot", "Live MIDI"}, 0));
    p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"source", 1}, "FX scratch source",
                                                       juce::StringArray{"File Deck", "Live Input"}, 0));
    p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"embedSample", 1}, "Embed sample",
                                                     true));
    for (const auto &d : dj::definitions)
        p.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{d.id, 2}, d.name, juce::NormalisableRange<float>(d.minimum, d.maximum, d.step),
            d.initial));
    return p;
}

NeedleProcessor::NeedleProcessor(bool instrument)
    : AudioProcessor(instrument
                         ? BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)
                         : BusesProperties()
                               .withInput("Input", juce::AudioChannelSet::stereo(), true)
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      juce::Thread("SKRATCH controller service"),
      state(*this, nullptr, "NeedleState", parameters(instrument)), instrumentMode(instrument),
      sinc(sincTable())
{
    const char *ids[] = {"fader",       "gain",    "pitch",       "sensitivity", "scratch", "play",
                         "loop",        "reverse", "cutMode",     "mode",        "cutIn",   "curve",
                         "response",    "bpm",     "swing",       "quantize",    "rpm",     "sync",
                         "deckReverse", "length",  "performance", "midiMode",    "source",  "embedSample"};
    for (size_t i = 0; i < parameterCache.size(); ++i)
        parameterCache[i] = {ids[i], state.getRawParameterValue(ids[i])};
    for (size_t i = 0; i < liveCC.size(); ++i)
    {
        liveCC[i].store(-1);
        liveRanges[i] = state.getParameter(liveIds[i])->getNormalisableRange();
        effectiveLive[i].store(param(liveIds[i]));
    }
    djValues = djPreviousHost = dj::defaults();
    for (size_t i = 0; i < dj::count; ++i)
    {
        djHost[i] = state.getRawParameterValue(dj::definitions[i].id);
        djCC[i].store(-1);
        djEffective[i].store(std::numeric_limits<float>::quiet_NaN());
    }
    for (auto &position : hotCues)
        position.store(-1.0);
    take.reserve();
    undo.reserve();
    redo.reserve();
    for (int i = 0; i < skratch::patternCount; ++i)
    {
        patterns[i] = skratch::makePattern(i);
        noteMap[i].store(48 + i);
    }
    renderPatterns = patterns;
    loadDemo();
    startThread();
}

NeedleProcessor::~NeedleProcessor()
{
    stopThread(2000);
    cancelPendingUpdate();
    ++generation;
    loader.removeAllJobs(true, -1);
    delete pendingClip.exchange(nullptr);
    delete retiredClip.exchange(nullptr);
}

void NeedleProcessor::setParam(const juce::String &id, float value)
{
    if (auto *p = state.getParameter(id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(value));
        p->endChangeGesture();
    }
}

void NeedleProcessor::prepareToPlay(double rate, int)
{
    engine.prepare(rate);
    djRack.prepare(rate);
    djMotor.prepare(rate);
    inputStrip.prepare(rate);
    fileDeckStrip.prepare(rate);
    coast.prepare(rate);
    morph.prepare(rate);
    oldBrake = oldSpin = false;
    cueTransport = activeHotCue = -1;
    if (!instrumentMode)
    {
        inputHistory.setSize(2, static_cast<int>(std::clamp(rate * 4.0, 1024.0, 1536000.0)));
        inputHistory.clear();
    }
    historyWrite = historyCount = 0;
    inputScratching = false;
    voiceActive = false;
    voiceNote = -1;
    heldNotes.fill(0);
    liveOverride.fill(false);
    mouseFramesLeft = 0;
    actualSpeed.store(0.0f);
    meter.store(0.0f);
    midiBack = midiForward = midiCut = false;
    engineActive.store(true);
    activePattern = -1;
    midiSpeedActive = midiFaderActive = false;
}

bool NeedleProcessor::isBusesLayoutSupported(const BusesLayout &l) const
{
    const auto out = l.getMainOutputChannelSet();
    if (instrumentMode)
        return l.inputBuses.isEmpty() && out == juce::AudioChannelSet::stereo();
    return (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono()) &&
           (l.getMainInputChannelSet() == out || l.getMainInputChannelSet().isDisabled());
}

void NeedleProcessor::applyMidi(const juce::MidiMessage &m, bool recordAccess)
{
    if (m.isNoteOn())
        lastMidiNote.store(m.getNoteNumber());
    if (m.isController())
        lastMidiCC.store(m.getControllerNumber());
    if (midiChannel.load() != 0 && m.getChannel() != midiChannel.load())
        return;
    const int armed = learnTarget.load();
    if (m.isNoteOn() && armed >= 3 && armed < liveLearnBase)
    {
        const int slot = learnTarget.exchange(-1) - 3;
        if (slot >= 0 && slot < 16)
            noteMap[slot].store(m.getNoteNumber());
    }
    if (m.isController())
    {
        const int number = m.getControllerNumber(), value = m.getControllerValue();
        const int learn = armed >= 0 && (armed < 3 || armed >= liveLearnBase) ? learnTarget.exchange(-1) : -1;
        if (learn == 0)
            speedCC.store(number);
        if (learn == 1)
            faderCC.store(number);
        if (learn == 2)
            positionCC.store(number);
        if (learn >= liveLearnBase && learn < liveLearnBase + static_cast<int>(liveCC.size()))
            liveCC[static_cast<size_t>(learn - liveLearnBase)].store(number);
        djChangedByMidi = applyDjController(number, value, learn);
        for (size_t i = 0; i < liveCC.size(); ++i)
            if (number == liveCC[i].load())
            {
                liveValues[i] = liveRanges[i].convertFrom0to1(value / 127.0f);
                liveOverride[i] = true;
            }
        if (number == speedCC.load())
        {
            midiSpeedValue = (value - 64) / static_cast<float>(value < 64 ? 64 : 63) * 6;
            midiSpeedActive = true;
        }
        if (number == faderCC.load())
        {
            faderMSB = value;
            faderLSB = 0;
            midiFaderValue = value / 127.0f;
            midiFaderActive = true;
        }
        if (faderCC.load() < 32 && number == faderCC.load() + 32)
        {
            faderLSB = value;
            midiFaderValue = (faderMSB * 128 + faderLSB) / 16383.0f;
            midiFaderActive = true;
        }
        if (number == positionCC.load())
        {
            positionMSB = value;
            positionLSB = 0;
            seek.store(value / 127.0);
        }
        if (positionCC.load() < 32 && number == positionCC.load() + 32)
        {
            positionLSB = value;
            seek.store((positionMSB * 128 + positionLSB) / 16383.0);
        }
        if (number == 110)
            midiTransport = value > 63 ? 1 : 0;
    }
    if (m.isPitchWheel())
    {
        midiSpeedValue = (m.getPitchWheelValue() - 8192) /
                         static_cast<float>(m.getPitchWheelValue() < 8192 ? 8192 : 8191) * 6;
        midiSpeedActive = true;
    }
    const bool on = m.isNoteOn();
    if (on || m.isNoteOff())
    {
        const int note = m.getNoteNumber();
        const int hotCue = note - static_cast<int>(djValues[dj::cueBase]);
        if (hotCue >= 0 && hotCue < 8 && hotCues[static_cast<size_t>(hotCue)].load() >= 0)
        {
            applyHotCue(hotCue, on, m.getChannel());
            return;
        }
        const int channel = m.getChannel();
        auto &depth = heldNotes[static_cast<size_t>((channel - 1) * 128 + note)];
        if (on && depth < 255)
            ++depth;
        else if (!on && depth > 0)
            --depth;
        if (midiMode == 1)
        {
            if (on)
            {
                djMotor.resume();
                voiceActive = true;
                voiceReleasing = false;
                voiceNote = note;
                voiceChannel = channel;
                voiceVelocity = m.getFloatVelocity();
                voiceEnvelope = 0;
                seek.store(cue.load());
            }
            else if (voiceNote == note && voiceChannel == channel && depth == 0)
                voiceReleasing = true;
            return;
        }
        int mappedSlot = -1;
        for (int slot = 0; midiMode == 0 && slot < 16; ++slot)
            if (noteMap[slot].load() == note)
            {
                mappedSlot = slot;
                break;
            }
        if (mappedSlot >= 0)
        {
            const int slot = mappedSlot;
            if (on)
            {
                activePattern = slot;
                activeNote = note;
                activeNoteChannel = channel;
                patternPhase = 0;
                patternLength = param("length");
                patternVelocity = m.getFloatVelocity();
                selectedPattern.store(slot);
                const int q = static_cast<int>(param("quantize"));
                const double grid = q == 1 ? 0.25 : (q == 2 ? 0.5 : 1.0);
                patternPendingBeat = q == 0 ? absoluteBeat : std::ceil((absoluteBeat - 1e-9) / grid) * grid;
                if (q > 0 && static_cast<int>(std::round(patternPendingBeat / grid)) % 2)
                    patternPendingBeat += param("swing") * grid * 0.5;
                recordedNoteEvent = static_cast<size_t>(-1);
                if (recordAccess && recording.load())
                {
                    recordedNoteEvent = take.events.size();
                    addRecordedEvent(skratch::PatternNote, slot, patternLength, patternVelocity);
                }
            }
            else if (activeNote == note && activeNoteChannel == channel && depth == 0)
            {
                activePattern = -1;
                activeNote = -1;
                if (recordAccess && recording.load() && recordedNoteEvent < take.events.size())
                {
                    auto &entry = take.events[recordedNoteEvent];
                    if (entry.type == skratch::PatternNote && static_cast<int>(entry.value) == slot)
                        entry.duration = std::max(0.0625, recordBeat - entry.beat);
                }
                recordedNoteEvent = static_cast<size_t>(-1);
            }
        }
        if (mappedSlot < 0)
            switch (note)
            {
            case 36:
                midiBack = on;
                break;
            case 37:
                midiForward = on;
                break;
            case 38:
                midiCut = on;
                break;
            case 39:
                if (on)
                    seek.store(cue.load());
                break;
            default:
                break;
            }
    }
    if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        midiBack = midiForward = midiCut = false;
        midiSpeedActive = midiFaderActive = false;
        activePattern = activeNote = -1;
        heldNotes.fill(0);
        voiceReleasing = true;
        cueTransport = activeHotCue = -1;
    }
}

void NeedleProcessor::processBlock(juce::AudioBuffer<float> &b, juce::MidiBuffer &midi)
{
    juce::ScopedNoDenormals noDenormals;
    const bool measure = latencyMetrics.enabled.load(std::memory_order_relaxed);
    const auto blockStart = measure ? juce::Time::getHighResolutionTicks() : 0;
    const bool monitoring = monitor.enabled.load(std::memory_order_relaxed);
    const auto rms = [&b]
    {
        double power = 0;
        for (int c = 0; c < b.getNumChannels(); ++c)
        {
            const double level = b.getRMSLevel(c, 0, b.getNumSamples());
            power += level * level;
        }
        return static_cast<float>(std::sqrt(power / std::max(1, b.getNumChannels())));
    };
    for (int c = getTotalNumInputChannels(); c < b.getNumChannels(); ++c)
        b.clear(c, 0, b.getNumSamples());
    if (monitoring)
        monitor.inputRms.store(rms(), std::memory_order_relaxed);
    if (retiredClip.load(std::memory_order_acquire) == nullptr)
        if (auto *next = pendingClip.exchange(nullptr, std::memory_order_acq_rel))
        {
            retiredClip.store(activeClip.release(), std::memory_order_release);
            activeClip.reset(next);
            engine.reset();
            rotation = 0;
            voiceActive = false;
            cueTransport = activeHotCue = -1;
            double noSeek = -1;
            seek.compare_exchange_strong(noSeek, 0);
        }
    const juce::SpinLock::ScopedTryLockType lock(takeLock);
    const bool takeAccess = lock.isLocked();
    if (measure && !takeAccess)
        latencyMetrics.takeContention.fetch_add(1, std::memory_order_relaxed);
    if (takeAccess && patternsDirty)
    {
        renderPatterns = patterns;
        patternsDirty = false;
    }
    const auto recordEvent = [&](int type, double value, double duration = 0, float velocity = 1)
    {
        if (takeAccess)
            addRecordedEvent(type, value, duration, velocity);
    };
    const int length = activeClip ? activeClip->audio.getNumSamples() : 0;
    const double clipRate = activeClip ? activeClip->rate : engine.outputRate;
    const float *clipChannels[2]{};
    if (activeClip)
        for (int c = 0; c < 2; ++c)
            clipChannels[c] =
                activeClip->audio.getReadPointer(std::min(c, activeClip->audio.getNumChannels() - 1));
    const float hostFader = param("fader");
    if (previousHostFader != hostFader)
    {
        midiFaderActive = false;
        previousHostFader = hostFader;
    }
    if (resetMidi.exchange(false))
    {
        midiBack = midiForward = midiCut = false;
        midiSpeedActive = midiFaderActive = false;
        midiTransport = -1;
        activePattern = activeNote = -1;
        voiceActive = false;
        heldNotes.fill(0);
        liveOverride.fill(false);
        djOverridden.fill(false);
        for (auto &value : djEffective)
            value.store(std::numeric_limits<float>::quiet_NaN(), std::memory_order_relaxed);
        cueTransport = activeHotCue = -1;
    }
    readDjControls();
    const int nextMode = static_cast<int>(param("midiMode"));
    if (midiMode != nextMode)
    {
        midiMode = nextMode;
        activePattern = activeNote = -1;
        voiceActive = false;
        heldNotes.fill(0);
        midiSpeedActive = midiFaderActive = false;
    }
    std::array<float, 6> hostValues;
    for (size_t i = 0; i < hostValues.size(); ++i)
    {
        hostValues[i] = param(liveIds[i]);
        if (hostValues[i] != previousLiveHost[i])
            liveOverride[i] = false;
        previousLiveHost[i] = hostValues[i];
    }
    const auto liveValue = [&](size_t index)
    { return liveOverride[index] ? liveValues[index] : hostValues[index]; };
    const bool reverse = param("reverse") > 0.5f, cutMode = param("cutMode") > 0.5f;
    const bool play = param("play") > 0.5f, loop = param("loop") > 0.5f;
    const bool sync = param("sync") > 0.5f, performance = param("performance") > 0.5f;
    const bool liveInput = !instrumentMode && getTotalNumInputChannels() > 0 &&
                           inputHistory.getNumSamples() > 0 && param("source") > 0.5f;
    bool playTake = takeAccess && takePlaying.load();
    const bool holdTake = !takeAccess && takePlaying.load();
    if (play != previousPlay)
    {
        midiTransport = -1;
        cueTransport = activeHotCue = -1;
        previousPlay = play;
        recordEvent(skratch::Transport, play ? 1 : 0);
    }
    if (cue.load() != previousCue)
    {
        previousCue = cue.load();
        recordEvent(skratch::Cue, previousCue);
    }
    double bpm = playTake && !sync ? playbackTempo : param("bpm");
    if (sync)
        if (auto *head = getPlayHead())
            if (auto position = head->getPosition())
            {
                if (auto tempo = position->getBpm())
                    if (std::isfinite(*tempo) && *tempo >= 20 && *tempo <= 400)
                        bpm = *tempo;
                if (position->getIsPlaying())
                    if (auto ppq = position->getPpqPosition())
                        if (std::isfinite(*ppq))
                            absoluteBeat = *ppq;
            }
    bpmDisplay.store(bpm);
    if (lastRecordedTempo != bpm)
    {
        recordEvent(skratch::Tempo, bpm);
        lastRecordedTempo = bpm;
    }
    double beatStep = bpm / (60.0 * engine.outputRate);
    unsigned fxActions = djActions.exchange(0, std::memory_order_relaxed);
    const auto overrideDj = [&](dj::Param parameter, float value)
    {
        djValues[parameter] = value;
        djOverridden[parameter] = true;
        djEffective[parameter].store(value, std::memory_order_relaxed);
    };
    if ((fxActions & (1u << dj::echoAction)) != 0)
        overrideDj(dj::echoOut, djValues[dj::echoOut] > .5f ? 0 : 1);
    if ((fxActions & (1u << dj::stutterAction)) != 0)
        overrideDj(dj::gateFire, djValues[dj::gateFire] > .5f ? 0 : 1);
    if ((fxActions & (1u << dj::fxAction)) != 0)
        overrideDj(dj::enabled, djValues[dj::enabled] > .5f ? 0 : 1);
    if ((fxActions & (1u << dj::resetFilterAction)) != 0)
        overrideDj(dj::filter, 0);
    bool fxRunning = false, inputRunning = false, fileDeckStripRunning = false;
    const auto updateDj = [&]
    {
        const bool braking = djValues[dj::brake] > .5f, spinning = djValues[dj::spin] > .5f;
        if (braking && !oldBrake)
            fxActions |= 1u << dj::brakeAction;
        if (spinning && !oldSpin)
            fxActions |= 1u << dj::spinAction;
        oldBrake = braking;
        oldSpin = spinning;
        djMotor.configure(djValues);
        if ((fxActions & ((1u << dj::brakeAction) | (1u << dj::spinAction))) != 0)
            djMotor.action(fxActions, liveInput && !inputScratching ? 1.0 : engine.velocity);
        fxActions = 0;
        djRack.configure(djValues, bpm);
        fxRunning = djRack.active() || djRack.limiterActive();
        inputStrip.configure(djValues);
        inputRunning = inputStrip.active();
        if (!instrumentMode && !liveInput)
        {
            fileDeckStrip.configure(djValues);
            fileDeckStripRunning = fileDeckStrip.active();
        }
        coast.configure(djValues[dj::coastOn] > .5f, djValues[dj::friction]);
    };
    updateDj();
    const auto dispatchCues = [&](unsigned queuedCues, int source)
    {
        if (queuedCues == 0)
            return;
        for (int slot = 0; slot < 8; ++slot)
        {
            if ((queuedCues & (1u << slot)) != 0)
                applyHotCue(slot, true, source);
            if ((queuedCues & (1u << (slot + 8))) != 0)
                applyHotCue(slot, false, source);
        }
    };
    dispatchCues(cueRequests.exchange(0, std::memory_order_relaxed), 0);
    dispatchCues(padCueRequests.exchange(0, std::memory_order_relaxed), -1);
    const double rpmScale = param("rpm") > 0.5f ? 45.0 / 33.333333333 : 1.0;
    const double transportSpeed = rpmScale * (param("deckReverse") > 0.5f ? -1 : 1);
    const double rotationScale = 1.0 / (engine.outputRate * 1.8);
    const double clipStep = clipRate / engine.outputRate;
    const int algorithm = static_cast<int>(param("mode"));
    const double automatedScratch = param("scratch");
    double sensitivity = 1, baseSpeed = 1;
    float gain = 1;
    const auto updateControls = [&]
    {
        baseSpeed = (1 + liveValue(0) * 0.01) * transportSpeed;
        gain = juce::Decibels::decibelsToGain(liveValue(1));
        sensitivity = liveValue(2);
        engine.configure(algorithm, liveValue(3), liveValue(4), liveValue(5), performance);
    };
    updateControls();
    const bool held = mouseHeld.load(), pad = padActive.load();
    const auto serial = mouseSerial.load(std::memory_order_acquire);
    const float mouse = mouseSpeed.load(), key = keySpeed.load(), padVelocity = padSpeed.load(),
                padCrossfader = padFader.load();
    if (serial != mouseSeen)
    {
        mouseSeen = serial;
        mouseFramesLeft = static_cast<int>(engine.outputRate * (performance ? 0.024 : 0.080));
        if (measure && held)
            if (const auto stamp = latencyMetrics.mouseTick.load(std::memory_order_relaxed); stamp > 0)
                latencyMetrics.mouseToDsp.add(juce::Time::getHighResolutionTicks() - stamp);
    }
    if (measure)
        if (const auto stamp = latencyMetrics.padTick.load(std::memory_order_acquire);
            stamp > 0 && stamp != padSeen)
        {
            padSeen = stamp;
            latencyMetrics.padToDsp.add(juce::Time::getHighResolutionTicks() - stamp);
        }
    const bool cut = cutHeld.load();
    const int request = patternRequest.exchange(-1), released = patternRelease.exchange(-1);
    if (released == 99)
    {
        coast.cancel();
        voiceReleasing = true;
        heldNotes.fill(0);
        midiSpeedActive = false;
        midiBack = midiForward = false;
        midiTransport = -1;
        cueTransport = activeHotCue = -1;
    }
    if (released == 99 || released == activePattern)
    {
        activePattern = -1;
        activeNote = -1;
    }
    if (request >= 0)
    {
        activePattern = request;
        activeNote = -1;
        patternPhase = 0;
        patternLength = patternRequestLength.load();
        patternVelocity = patternRequestVelocity.load();
        const int q = static_cast<int>(param("quantize"));
        const double grid = q == 1 ? 0.25 : (q == 2 ? 0.5 : 1.0);
        patternPendingBeat = q == 0 ? absoluteBeat : std::ceil((absoluteBeat - 1e-9) / grid) * grid;
        if (q > 0 && static_cast<int>(std::round(patternPendingBeat / grid)) % 2)
            patternPendingBeat += param("swing") * grid * 0.5;
        recordEvent(skratch::PatternNote, request, patternLength, patternVelocity);
    }
    auto event = midi.cbegin();
    float peak = 0, lastFader = hostFader;
    const int seekFadeSamples =
        std::max(1, static_cast<int>(engine.outputRate * (performance ? 0.001 : 0.003)));
    const float attack = static_cast<float>(1.0 / std::max(1.0, engine.outputRate * 0.001));
    const float release = static_cast<float>(1.0 / std::max(1.0, engine.outputRate * 0.006));
    const int historyLength = inputHistory.getNumSamples();
    const bool captureTake = takeAccess && recording.load();
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        while (event != midi.cend() && (*event).samplePosition <= i)
        {
            const auto message = *event;
            if (message.numBytes <= 3)
            {
                const auto midiMessage = message.getMessage();
                djChangedByMidi = false;
                applyMidi(midiMessage, takeAccess);
                if (midiMessage.isController())
                    updateControls();
                if (djChangedByMidi)
                    updateDj();
            }
            ++event;
        }
        if (playTake)
        {
            while (eventCursor < take.events.size() && take.events[eventCursor].beat <= playBeat + 1e-9)
            {
                const auto &e = take.events[eventCursor++];
                if (e.type == skratch::Seek)
                    seek.store(e.value);
                if (e.type == skratch::Cue)
                    cue.store(e.value);
                if (e.type == skratch::Transport)
                    takeTransport = e.value > 0 ? 1 : 0;
                if (e.type == skratch::Cut)
                    takeCut = e.value > 0;
                if (e.type == skratch::Tempo && !sync)
                {
                    playbackTempo = juce::jlimit(20.0, 400.0, e.value);
                    beatStep = playbackTempo / (60 * engine.outputRate);
                }
                if (e.type == skratch::PatternNote)
                {
                    activePattern = juce::jlimit(0, 15, static_cast<int>(e.value));
                    activeNote = -1;
                    patternPhase = 0;
                    patternPendingBeat = absoluteBeat;
                    patternLength = std::max(0.0625, e.duration);
                    patternVelocity = e.velocity;
                }
            }
        }
        const double requested = seek.load(std::memory_order_relaxed) >= 0 ? seek.exchange(-1.0) : -1;
        if (requested >= 0 && std::isfinite(requested))
        {
            for (int c = 0; c < 2; ++c)
                seekFrom[c] = previousOutput[c];
            seekFade = seekFadeSamples;
            engine.position = std::clamp(requested, 0.0, 0.999999) * length;
            rotation = engine.position / (clipRate * 1.8);
            engine.velocity = 0;
            coast.cancel();
            recordEvent(skratch::Seek, requested);
        }
        const double midiSpeed = (midiForward ? 1.0 : 0.0) - (midiBack ? 1.0 : 0.0);
        const bool transport =
            playTake && takeTransport >= 0
                ? takeTransport != 0
                : (cueTransport >= 0 ? cueTransport != 0 : (midiTransport >= 0 ? midiTransport != 0 : play));
        double target =
            held ? (serial == 0 || mouseFramesLeft > 0 ? mouse * sensitivity : 0)
                 : (key != 0 ? key * sensitivity
                             : (pad ? padVelocity
                                    : ((midiBack || midiForward)
                                           ? midiSpeed * sensitivity
                                           : (midiSpeedActive ? midiSpeedValue
                                                              : (std::abs(automatedScratch) > 0.001
                                                                     ? automatedScratch
                                                                     : (transport ? baseSpeed : 0))))));
        float fader = pad ? padCrossfader : (midiFaderActive ? midiFaderValue : hostFader), patternGain = 1,
              patternSpeed = 0, patternFader = 1;
        bool patternApplied = false;
        if (mouseFramesLeft > 0)
            --mouseFramesLeft;
        if (voiceActive && midiMode == 1)
        {
            if (!held && key == 0 && !pad && !midiSpeedActive)
                target = baseSpeed;
            voiceEnvelope = std::clamp(voiceEnvelope + (voiceReleasing ? -release : attack), 0.0f, 1.0f);
            patternGain = voiceEnvelope * voiceVelocity;
            if ((voiceReleasing && voiceEnvelope <= 0) || engine.position >= length || engine.position < 0)
            {
                voiceActive = false;
                patternGain = 0;
            }
        }
        if (activePattern >= 0 && absoluteBeat + 1e-9 >= patternPendingBeat)
        {
            const auto &p = renderPatterns[activePattern];
            const double phase = std::clamp(patternPhase / patternLength, 0.0, 1.0);
            float patternTarget = p.sample(p.platter, phase);
            fader = p.sample(p.fader, phase);
            if (activePattern != 11 && morph.active(djValues))
            {
                const auto &a = renderPatterns[static_cast<size_t>(djValues[dj::morphA])];
                const auto &bPattern = renderPatterns[static_cast<size_t>(djValues[dj::morphB])];
                const auto blended = morph.frame(
                    djValues, patternTarget, fader, a.sample(a.platter, phase), a.sample(a.fader, phase),
                    bPattern.sample(bPattern.platter, phase), bPattern.sample(bPattern.fader, phase));
                patternTarget = blended.first;
                fader = blended.second;
            }
            target = patternTarget * (0.5 + patternVelocity * 0.5);
            patternGain = patternVelocity;
            patternSpeed = static_cast<float>(target);
            patternFader = fader;
            patternApplied = true;
            patternPhase += beatStep;
            if (patternPhase >= patternLength)
            {
                if (activeNote >= 0)
                    patternPhase = std::fmod(patternPhase, patternLength);
                else
                    activePattern = -1;
            }
        }
        if (playTake)
        {
            target = skratch::curve(take.platter, playBeat, speedCursor, static_cast<float>(target));
            fader = skratch::curve(take.fader, playBeat, faderCursor, fader);
            if (take.patternPriority && patternApplied)
            {
                target = patternSpeed;
                fader = patternFader;
            }
            lastTakeSpeed = static_cast<float>(target);
            lastTakeFader = fader;
        }
        else if (holdTake)
        {
            target = lastTakeSpeed;
            fader = lastTakeFader;
        }
        if (!std::isfinite(target))
            target = 0;
        if (djMotor.active())
        {
            const bool manual = held || key != 0 || pad || midiBack || midiForward || midiSpeedActive ||
                                std::abs(automatedScratch) > .001 || patternApplied || playTake || holdTake;
            target = djMotor.tick(target, manual);
        }
        if (coast.active())
        {
            const bool direct = held || key != 0 || (pad && std::abs(padVelocity) > .0001f);
            const bool free = !transport && !voiceActive && !patternApplied && !playTake && !holdTake &&
                              !midiSpeedActive && !midiBack && !midiForward &&
                              std::abs(automatedScratch) <= .001 && !djMotor.interrupting();
            target = coast.apply(target, engine.velocity, direct, free);
        }
        lastFader = fader;
        const bool gate = cut || midiCut.load(std::memory_order_relaxed) || (playTake && takeCut);
        if (gate != previousCut)
        {
            recordEvent(skratch::Cut, gate ? 1 : 0);
            previousCut = gate;
        }
        engine.tick(target, fader, reverse, cutMode, gate);
        if (midiMode == 1 && !voiceActive && !transport && !held && key == 0 && !pad && !midiSpeedActive &&
            !coast.isMoving())
            patternGain = 0;
        const bool wraps = loop && !(midiMode == 1 && voiceActive);
        const bool inside = engine.position >= 0 && engine.position < length;
        const bool audible = length > 0 && (wraps || inside) && std::abs(engine.velocity) > 0.000001;
        if (captureTake && recording.load(std::memory_order_relaxed))
        {
            if (recordCountdown-- <= 0)
            {
                if (take.platter.size() < skratch::maxPoints)
                {
                    take.platter.push_back({recordBeat, static_cast<float>(target)});
                    take.fader.push_back({recordBeat, fader});
                }
                else
                    recording.store(false);
                recordCountdown = std::max(1, static_cast<int>(engine.outputRate / 200.0)) - 1;
            }
            recordBeat += beatStep;
            take.length = recordBeat;
        }
        const double increment = engine.velocity * clipStep;
        const float motionGain =
            static_cast<float>(std::min(1.0, std::abs(engine.velocity) * 80)) * patternGain;
        const bool scratchInput =
            liveInput && (held || key != 0 || pad || midiBack || midiForward || midiSpeedActive ||
                          std::abs(automatedScratch) > 0.001 || patternApplied || playTake ||
                          djMotor.interrupting() || coast.isMoving());
        float inputFrame[2]{}, voiceFrame[2]{};
        const bool voiceStripRunning = instrumentMode ? inputRunning : fileDeckStripRunning;
        if (voiceStripRunning)
        {
            if (audible)
            {
                voiceFrame[0] = sinc.read(clipChannels[0], length, engine.position, increment, wraps);
                voiceFrame[1] = clipChannels[0] == clipChannels[1]
                    ? voiceFrame[0] : sinc.read(clipChannels[1], length, engine.position, increment, wraps);
            }
            auto &strip = instrumentMode ? inputStrip : fileDeckStrip;
            strip.frame(voiceFrame, b.getNumChannels());
        }
        if (inputRunning && !instrumentMode && getTotalNumInputChannels() > 0)
        {
            for (int c = 0; c < b.getNumChannels(); ++c)
                inputFrame[c] = b.getSample(c, i);
            inputStrip.frame(inputFrame, b.getNumChannels());
        }
        if (liveInput)
        {
            if (scratchInput && !inputScratching)
            {
                historyRead = std::max(0, historyCount - static_cast<int>(engine.outputRate * 0.055));
            }
            if (scratchInput != inputScratching)
            {
                std::copy_n(previousOutput, 2, seekFrom);
                seekFade = seekFadeSamples;
            }
            inputScratching = scratchInput;
            for (int c = 0; c < getTotalNumInputChannels(); ++c)
                inputHistory.setSample(c, historyWrite, inputRunning ? inputFrame[c] : b.getSample(c, i));
        }
        float leftWet = 0, frame[2]{};
        for (int c = 0; c < b.getNumChannels(); ++c)
        {
            const float dry = inputRunning && !instrumentMode ? inputFrame[c] : b.getSample(c, i);
            float wet = 0;
            if (!liveInput && audible)
            {
                wet =
                    voiceStripRunning ? voiceFrame[c] * motionGain
                    : c == 1 && clipChannels[0] == clipChannels[1]
                        ? leftWet
                        : sinc.read(clipChannels[std::min(c, 1)], length, engine.position, increment, wraps) *
                              motionGain;
                if (c == 0)
                    leftWet = wet;
            }
            if (liveInput)
            {
                wet = dry;
                if (scratchInput)
                {
                    const auto first = static_cast<int>(historyRead);
                    const int oldest = historyWrite - historyCount;
                    const auto at = [&](int index)
                    {
                        const int offset = oldest + std::clamp(index, 0, historyCount);
                        return inputHistory.getSample(c, offset < 0 ? offset + historyLength : offset);
                    };
                    const float fraction = static_cast<float>(historyRead - first);
                    const float a = at(first), next = at(first + 1);
                    wet = (a + (next - a) * fraction) * motionGain;
                }
            }
            const float dryGain = liveInput && cutMode ? 0.0f : engine.inputGain;
            float out = liveInput && !scratchInput ? dry * std::min(1.0f, dryGain + engine.deckGain)
                                                   : dry * dryGain + wet * engine.deckGain;
            if (seekFade > 0)
            {
                const float blend = 1.0f - seekFade / static_cast<float>(seekFadeSamples);
                out = seekFrom[std::min(c, 1)] * (1 - blend) + out * blend;
            }
            const float safe = std::isfinite(out) ? out : 0;
            if (fxRunning)
                frame[c] = safe;
            else
            {
                const float output = std::clamp(safe * gain, -1.0f, 1.0f);
                b.setSample(c, i, output);
                peak = std::max(peak, std::abs(output));
            }
            previousOutput[std::min(c, 1)] = safe;
        }
        if (fxRunning)
        {
            djRack.frame(frame, b.getNumChannels(), absoluteBeat, gain);
            for (int c = 0; c < b.getNumChannels(); ++c)
            {
                const auto output = std::isfinite(frame[c]) ? std::clamp(frame[c], -1.0f, 1.0f) : 0.0f;
                b.setSample(c, i, output);
                peak = std::max(peak, std::abs(output));
            }
        }
        if (seekFade > 0)
            --seekFade;
        if (liveInput)
        {
            const bool full = historyCount == historyLength - 1;
            if (++historyWrite == historyLength)
                historyWrite = 0;
            if (!full)
                ++historyCount;
            if (scratchInput)
                historyRead = std::clamp(historyRead + engine.velocity - (full ? 1.0 : 0.0), 0.0,
                                         static_cast<double>(historyCount));
        }
        if (length > 0)
        {
            engine.position += increment;
            if (wraps && (engine.position < 0 || engine.position >= length))
                engine.position = needle::wrap(engine.position, length);
            else if (!wraps)
                engine.position = std::clamp(engine.position, -1.0, static_cast<double>(length));
        }
        if (liveInput || wraps || inside)
            rotation += (liveInput && !scratchInput ? 1.0 : engine.velocity) * rotationScale;
        absoluteBeat += beatStep;
        if (playTake)
        {
            playBeat += beatStep;
            if (playBeat >= take.length)
            {
                if (takeLoop.load())
                {
                    playBeat = 0;
                    speedCursor = faderCursor = eventCursor = 0;
                    activePattern = -1;
                    engine.velocity = 0;
                }
                else
                {
                    takePlaying.store(false);
                    playTake = false;
                    activePattern = -1;
                    takeTransport = -1;
                }
            }
        }
    }
    effectiveFader.store(lastFader);
    if (takeAccess)
        takePosition.store(playBeat);
    progress.store(length > 0 ? std::clamp(engine.position / length, 0.0, 1.0) : 0);
    actualSpeed.store(liveInput && !inputScratching ? 1.0f : static_cast<float>(engine.velocity));
    rotation = std::remainder(rotation, 1.0);
    platterTurns.store(rotation);
    appliedMouseSerial.store(serial, std::memory_order_release);
    for (size_t i = 0; i < effectiveLive.size(); ++i)
        effectiveLive[i].store(liveOverride[i] ? liveValues[i] : std::numeric_limits<float>::quiet_NaN(),
                               std::memory_order_relaxed);
    meter.store(peak);
    if (monitoring)
    {
        monitor.rack.store(djRack.active(), std::memory_order_relaxed);
        monitor.activeModules.store(djRack.activeModules(), std::memory_order_relaxed);
        monitor.strip.store(inputRunning || fileDeckStripRunning, std::memory_order_relaxed);
        monitor.outputRms.store(rms(), std::memory_order_relaxed);
    }
    midi.clear();
    if (measure)
    {
        const auto ticks = juce::Time::getHighResolutionTicks() - blockStart;
        latencyMetrics.block.add(ticks);
        if (juce::Time::highResolutionTicksToSeconds(ticks) > b.getNumSamples() / engine.outputRate)
            latencyMetrics.lateBlocks.fetch_add(1, std::memory_order_relaxed);
    }
}

void NeedleProcessor::install(juce::AudioBuffer<float> &&audio, double rate, const juce::String &name,
                              const juce::String &path, int request, SampleBytes bytes, bool preserveHotCues)
{
    std::vector<float> preview(800, 0.0f);
    for (size_t i = 0; i < preview.size(); ++i)
    {
        const int start = static_cast<int>(i * audio.getNumSamples() / preview.size());
        const int end =
            std::min(audio.getNumSamples(),
                     std::max(start + 1, static_cast<int>((i + 1) * audio.getNumSamples() / preview.size())));
        float v = 0;
        for (int c = 0; c < audio.getNumChannels(); ++c)
            for (int n = start; n < end; ++n)
                v = std::max(v, std::abs(audio.getSample(c, n)));
        preview[i] = v;
    }
    if (generation.load() != request)
        return;
    auto next = std::make_unique<Clip>(Clip{std::move(audio), rate});
    Clip *replaced = nullptr;
    {
        const juce::ScopedLock lock(metadataMutex);
        if (generation.load() != request)
            return;
        if (!preserveHotCues)
            for (auto &position : hotCues)
                position.store(-1);
        clipSeconds = next->audio.getNumSamples() / rate;
        ++clipRevision;
        clipName = name;
        filePath = path;
        embeddedSample = std::move(bytes);
        peaks.swap(preview);
        status = juce::String::fromUTF8("\xd0\x93\xd0\xbe\xd1\x82\xd0\xbe\xd0\xb2\xd0\xbe");
        replaced = pendingClip.exchange(next.release(), std::memory_order_acq_rel);
        loading.store(false);
    }
    delete replaced;
    delete retiredClip.exchange(nullptr, std::memory_order_acq_rel);
}

void NeedleProcessor::loadDemo(bool preserveHotCues)
{
    int request;
    {
        const juce::ScopedLock lock(metadataMutex);
        request = ++generation;
        loading.store(true);
    }
    juce::AudioBuffer<float> demo(1, 192000);
    juce::Random random(42);
    for (int i = 0; i < demo.getNumSamples(); ++i)
    {
        const double t = i / 48000.0;
        const double envelope = std::min(1.0, t * 35.0) * std::min(1.0, (4.0 - t) * 35.0);
        const double voiced = 0.30 * std::sin(t * 2 * juce::MathConstants<double>::pi * 140) +
                              0.18 * std::sin(t * 2 * juce::MathConstants<double>::pi * 700) +
                              0.12 * std::sin(t * 2 * juce::MathConstants<double>::pi * 1120);
        demo.setSample(0, i, static_cast<float>(envelope * (voiced + 0.08 * (random.nextFloat() * 2 - 1))));
    }
    cue.store(0.0);
    install(std::move(demo), 48000.0, "Demo / Scratch tone", {}, request, {}, preserveHotCues);
}

void NeedleProcessor::loadFile(const juce::File &file)
{
    loadSample(file);
}

void NeedleProcessor::loadSample(const juce::File &file, SampleBytes bytes, bool restoreNow,
                                 bool preserveHotCues)
{
    {
        const juce::ScopedLock lock(listMutex);
        sampleIndex = sampleList.indexOf(file.getFullPathName());
    }
    int request;
    {
        const juce::ScopedLock lock(metadataMutex);
        request = ++generation;
        loading.store(true);
        status = juce::String::fromUTF8(
                     "\xd0\x97\xd0\xb0\xd0\xb3\xd1\x80\xd1\x83\xd0\xb7\xd0\xba\xd0\xb0\x3a\x20") +
                 file.getFileName();
    }
    auto decode = [this, file, request, preserveHotCues, bytes = std::move(bytes)]() mutable
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        constexpr int maxEmbeddedBytes = 16 * 1024 * 1024;
        const auto fileSize = file.getSize();
        if (!bytes && instrumentMode && fileSize > 0 && fileSize <= maxEmbeddedBytes)
        {
            auto data = std::make_shared<juce::MemoryBlock>();
            if (file.loadFileAsData(*data) && data->getSize() <= maxEmbeddedBytes)
                bytes = std::move(data);
        }
        std::unique_ptr<juce::AudioFormatReader> reader(
            bytes ? formats.createReaderFor(std::make_unique<juce::MemoryInputStream>(*bytes, false))
                  : formats.createReaderFor(file));
        juce::String error;
        if (!reader)
            error = juce::String::fromUTF8(
                "\xd0\x9d\xd0\xb5\x20\xd1\x83\xd0\xb4\xd0\xb0\xd0\xbb\xd0\xbe\xd1\x81\xd1\x8c\x20\xd0\xbe"
                "\xd1\x82\xd0\xba\xd1\x80\xd1\x8b\xd1\x82\xd1\x8c\x20\xd1\x84\xd0\xb0\xd0\xb9\xd0\xbb\x2e"
                "\x20\xd0\x92\xd1\x8b\xd0\xb1\xd0\xb5\xd1\x80\xd0\xb8\xd1\x82\xd0\xb5\x20\x57\x41\x56\x2c"
                "\x20\x4d\x50\x33\x2c\x20\x46\x4c\x41\x43\x2c\x20\x41\x49\x46\x46\x20\xd0\xb8\xd0\xbb\xd0"
                "\xb8\x20\x4f\x47\x47\x2e");
        else if (reader->lengthInSamples < 4 || reader->sampleRate <= 0 || reader->numChannels == 0)
            error = juce::String::fromUTF8("\xd0\x92\x20\xd1\x84\xd0\xb0\xd0\xb9\xd0\xbb\xd0\xb5\x20\xd0"
                                           "\xbd\xd0\xb5\xd1\x82\x20\xd0\xb0\xd1\x83\xd0\xb4\xd0\xb8\xd0"
                                           "\xbe\xd0\xb4\xd0\xb0\xd0\xbd\xd0\xbd\xd1\x8b\xd1\x85\x2e");
        else if (reader->lengthInSamples > 33554432)
            error = juce::String::fromUTF8(
                "\xd0\xa4\xd0\xb0\xd0\xb9\xd0\xbb\x20\xd1\x81\xd0\xbb\xd0\xb8\xd1\x88\xd0\xba\xd0\xbe\xd0"
                "\xbc\x20\xd0\xb4\xd0\xbb\xd0\xb8\xd0\xbd\xd0\xbd\xd1\x8b\xd0\xb9\x2e\x20\xd0\x9c\xd0\xb0"
                "\xd0\xba\xd1\x81\xd0\xb8\xd0\xbc\xd1\x83\xd0\xbc\x3a\x20\x33\x33\x20\x35\x35\x34\x20\x34"
                "\x33\x32\x20\xd0\xbe\xd1\x82\xd1\x81\xd1\x87\xd1\x91\xd1\x82\xd0\xb0\x20\x28\xd0\xbe\xd0"
                "\xba\xd0\xbe\xd0\xbb\xd0\xbe\x20\x31\x31\x20\xd0\xbc\xd0\xb8\xd0\xbd\x20\xd0\xbf\xd1\x80"
                "\xd0\xb8\x20\x34\x38\x20\xd0\xba\xd0\x93\xd1\x86\x29\x2e");
        else
        {
            juce::AudioBuffer<float> audio(static_cast<int>(std::min(2u, reader->numChannels)),
                                           static_cast<int>(reader->lengthInSamples));
            if (reader->read(&audio, 0, audio.getNumSamples(), 0, true, true))
            {
                if (generation.load() == request)
                    install(std::move(audio), reader->sampleRate, file.getFileName(), file.getFullPathName(),
                            request, std::move(bytes), preserveHotCues);
                return;
            }
            error = juce::String::fromUTF8(
                "\xd0\x9e\xd1\x88\xd0\xb8\xd0\xb1\xd0\xba\xd0\xb0\x20\xd1\x87\xd1\x82\xd0\xb5\xd0\xbd\xd0"
                "\xb8\xd1\x8f\x20\xd0\xb0\xd1\x83\xd0\xb4\xd0\xb8\xd0\xbe\x2e\x20\xd0\x9f\xd0\xbe\xd0\xbf"
                "\xd1\x80\xd0\xbe\xd0\xb1\xd1\x83\xd0\xb9\xd1\x82\xd0\xb5\x20\xd0\xb4\xd1\x80\xd1\x83\xd0"
                "\xb3\xd0\xbe\xd0\xb9\x20\xd1\x84\xd0\xb0\xd0\xb9\xd0\xbb\x2e");
        }
        if (generation.load() == request)
        {
            const juce::ScopedLock lock(metadataMutex);
            if (generation.load() == request)
            {
                status = error;
                loading.store(false);
            }
        }
    };
    if (restoreNow)
        decode();
    else
        loader.addJob(std::move(decode));
}

NeedleProcessor::View NeedleProcessor::view() const
{
    return view(~0u);
}

NeedleProcessor::View NeedleProcessor::view(unsigned knownRevision) const
{
    const juce::ScopedLock lock(metadataMutex);
    return {clipName, status, knownRevision == clipRevision ? std::vector<float>{} : peaks, clipSeconds,
            clipRevision};
}

void NeedleProcessor::getStateInformation(juce::MemoryBlock &dest)
{
    auto tree = state.copyState();
    tree.setProperty("skratchVersion", 5, nullptr);
    tree.setProperty("mixerFxBank", mixerFxBank.load(std::memory_order_relaxed), nullptr);
    tree.setProperty("extra", juce::JSON::toString(extraState(), true), nullptr);
    tree.removeProperty("sampleBytes", nullptr);
    SampleBytes bytes;
    {
        const juce::ScopedLock lock(metadataMutex);
        tree.setProperty("file", filePath, nullptr);
        bytes = embeddedSample;
    }
    if (instrumentMode && param("embedSample") > 0.5f && bytes)
        tree.setProperty("sampleBytes", bytes->toBase64Encoding(), nullptr);
    tree.setProperty("cue", cue.load(), nullptr);
    if (auto xml = tree.createXml())
        copyXmlToBinary(*xml, dest);
}

void NeedleProcessor::setStateInformation(const void *data, int size)
{
    if (auto xml = getXmlFromBinary(data, size))
    {
        auto tree = juce::ValueTree::fromXml(*xml);
        if (!tree.hasType(state.state.getType()))
            return;
        const auto payload = tree.getProperty("sampleBytes").toString();
        tree.removeProperty("sampleBytes", nullptr);
        const int version = static_cast<int>(tree.getProperty("skratchVersion", 0));
        if (version < 5)
            for (size_t index = version < 4 ? 0 : dj::inputGain; index < dj::count; ++index)
            {
                const auto &d = dj::definitions[index];
                juce::ValueTree parameter("PARAM");
                parameter.setProperty("id", d.id, nullptr);
                parameter.setProperty("value", d.initial, nullptr);
                tree.addChild(parameter, -1, nullptr);
            }
        mixerFxBank.store(static_cast<bool>(tree.getProperty("mixerFxBank", false)), std::memory_order_relaxed);
        state.replaceState(tree);
        const auto extra = juce::JSON::parse(tree.getProperty("extra").toString());
        if (tree.hasProperty("extra"))
            restoreExtra(extra);
        const auto path = tree.getProperty("file").toString();
        SampleBytes bytes;
        if (instrumentMode && payload.isNotEmpty() && payload.length() <= 22400000)
        {
            auto data = std::make_shared<juce::MemoryBlock>();
            if (data->fromBase64Encoding(payload) && data->getSize() <= 16u * 1024u * 1024u)
                bytes = std::move(data);
        }
        if (path.isNotEmpty())
            loadSample(juce::File(path), std::move(bytes), instrumentMode, true);
        else
            loadDemo(true);
        restoreDjState(extra, true);
        resetMidi.store(true);
        cue.store(juce::jlimit(0.0, 0.999999, static_cast<double>(tree.getProperty("cue", 0.0))));
    }
}

juce::AudioProcessorEditor *NeedleProcessor::createEditor()
{
    return new NeedleEditor(*this);
}
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter()
{
    return new NeedleProcessor(JucePlugin_IsSynth != 0);
}
