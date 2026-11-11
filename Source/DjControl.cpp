#include "PluginProcessor.h"
#include <limits>

namespace
{
constexpr float noOverride = std::numeric_limits<float>::quiet_NaN();
} // namespace

float NeedleProcessor::djValue(dj::Param parameter) const noexcept
{
    const float value = djEffective[parameter].load(std::memory_order_relaxed);
    return std::isfinite(value) ? value : djHost[parameter]->load(std::memory_order_relaxed);
}

void NeedleProcessor::setDjParam(dj::Param parameter, float value)
{
    setParam(dj::definitions[parameter].id, value);
    clearDjOverride(parameter);
}

void NeedleProcessor::clearDjOverride(dj::Param parameter) noexcept
{
    djEffective[parameter].store(noOverride, std::memory_order_relaxed);
    djClearOverride[parameter / 64].fetch_or(uint64_t{1} << (parameter % 64), std::memory_order_relaxed);
}

double NeedleProcessor::getTailLengthSeconds() const
{
    if (djValue(dj::enabled) > .5f && djValue(dj::echoOn) > .5f)
        return 60;
    return instrumentMode ? .01 : 0;
}

void NeedleProcessor::readDjControls()
{
    std::array<uint64_t, (dj::count + 63) / 64> clear{};
    for (size_t i = 0; i < clear.size(); ++i)
        if (djClearOverride[i].load(std::memory_order_relaxed) != 0)
            clear[i] = djClearOverride[i].exchange(0, std::memory_order_relaxed);
    for (size_t i = 0; i < dj::count; ++i)
    {
        const auto raw = djHost[i]->load(std::memory_order_relaxed);
        if (raw != djPreviousHost[i] || (clear[i / 64] & (uint64_t{1} << (i % 64))) != 0)
        {
            djPreviousHost[i] = raw;
            djOverridden[i] = false;
            djEffective[i].store(noOverride, std::memory_order_relaxed);
        }
        if (!djOverridden[i])
            djValues[i] = std::isfinite(raw) ? raw : dj::definitions[i].initial;
    }
}

bool NeedleProcessor::applyDjController(int number, int value, int learn)
{
    if (learn >= dj::learnBase && learn < dj::learnBase + static_cast<int>(dj::count))
        djCC[static_cast<size_t>(learn - dj::learnBase)].store(number, std::memory_order_relaxed);
    bool changed = false;
    for (size_t i = 0; i < dj::count; ++i)
        if (djCC[i].load(std::memory_order_relaxed) == number)
        {
            const auto &def = dj::definitions[i];
            float converted = def.minimum + (def.maximum - def.minimum) * value / 127.0f;
            if (def.step > 0)
                converted = def.minimum + def.step * std::round((converted - def.minimum) / def.step);
            changed = changed || djValues[i] != converted;
            djValues[i] = converted;
            djOverridden[i] = true;
            djEffective[i].store(converted, std::memory_order_relaxed);
        }
    return changed;
}

void NeedleProcessor::storeHotCue(int index) noexcept
{
    if (index >= 0 && index < 8 && !loading.load(std::memory_order_relaxed))
        hotCues[static_cast<size_t>(index)].store(progress.load(std::memory_order_relaxed));
}

void NeedleProcessor::requestHotCue(int index, bool down) noexcept
{
    if (index >= 0 && index < 8)
        cueRequests.fetch_or(1u << (index + (down ? 0 : 8)), std::memory_order_relaxed);
}

void NeedleProcessor::applyHotCue(int index, bool down, int midiSource)
{
    const int mode = static_cast<int>(djValues[dj::cueMode]);
    if (!down)
    {
        if (mode == 1 && index == activeHotCue && midiSource == activeHotCueSource)
        {
            cueTransport = 0;
            activeHotCue = -1;
        }
        return;
    }
    const auto position = hotCues[static_cast<size_t>(index)].load(std::memory_order_relaxed);
    if (position < 0 || !std::isfinite(position))
        return;
    if (mode == 2 && cueTransport == 1 && index == activeHotCue)
    {
        cueTransport = 0;
        activeHotCue = -1;
        return;
    }
    activeHotCue = index;
    activeHotCueSource = midiSource;
    cueTransport = 1;
    djMotor.resume();
    voiceActive = false;
    seek.store(position, std::memory_order_relaxed);
}

void NeedleProcessor::writeDjState(juce::DynamicObject &object, bool includeCues) const
{
    juce::Array<juce::var> assignments;
    for (const auto &cc : djCC)
        assignments.add(cc.load());
    object.setProperty("djCC", assignments);
    if (includeCues)
    {
        juce::Array<juce::var> positions;
        for (const auto &position : hotCues)
            positions.add(position.load());
        object.setProperty("hotCues", positions);
    }
}

void NeedleProcessor::restoreDjState(const juce::var &stateData, bool includeCues)
{
    const auto *assignments = stateData["djCC"].getArray();
    for (size_t i = 0; i < djCC.size(); ++i)
        djCC[i].store(assignments && static_cast<int>(i) < assignments->size()
                          ? juce::jlimit(-1, 127, static_cast<int>((*assignments)[static_cast<int>(i)]))
                          : -1);
    if (includeCues)
    {
        const auto *positions = stateData["hotCues"].getArray();
        for (size_t i = 0; i < hotCues.size(); ++i)
        {
            const double position = positions && static_cast<int>(i) < positions->size()
                                        ? static_cast<double>((*positions)[static_cast<int>(i)])
                                        : -1.0;
            hotCues[i].store(std::isfinite(position) ? std::clamp(position, -1.0, .999999) : -1.0);
        }
    }
}
