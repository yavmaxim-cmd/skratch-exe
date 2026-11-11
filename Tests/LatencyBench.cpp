#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <chrono>
#include <iostream>
#include <iomanip>
#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using Clock = std::chrono::steady_clock;
using Micros = std::chrono::duration<double, std::micro>;

static double elapsed(Clock::time_point start)
{
    return Micros(Clock::now() - start).count();
}

#if JUCE_WINDOWS
static double threadCpuMicros()
{
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetThreadTimes(GetCurrentThread(), &creation, &exit, &kernel, &user))
        return 0;
    const auto ticks = [](const FILETIME &time)
    { return (static_cast<uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime; };
    return (ticks(kernel) + ticks(user)) / 10.0;
}
#endif

static void summary(const char *name, int rate, int size, std::vector<double> values)
{
    std::sort(values.begin(), values.end());
    double sum = 0;
    for (const auto value : values)
        sum += value;
    const double deadline = rate == 0 ? 0 : 1e6 * size / rate;
    const auto misses =
        deadline == 0 ? 0 : values.end() - std::upper_bound(values.begin(), values.end(), deadline);
    std::cout << name << ',' << rate << ',' << size << ',' << values.size() << ',' << sum / values.size()
              << ',' << values[values.size() * 95 / 100] << ',' << values[values.size() * 99 / 100] << ','
              << values.back() << ',' << misses << ','
              << (deadline == 0 ? 0 : 100 * sum / values.size() / deadline) << '\n';
}

int main(int argc, char **argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    NeedleProcessor processor;
    const auto argument = argc > 1 ? juce::String(argv[1]) : juce::String{};
    const bool fullMixer = argument == "fullmixer";
    const bool fullFx = argument == "fullfx" || fullMixer;
    const bool performance = argument == "performance" || fullFx;
    if (performance)
        processor.setParam("performance", 1);
    if (fullFx)
    {
        for (const auto parameter :
             {dj::enabled, dj::limiter, dj::echoOn, dj::crushOn, dj::driveOn, dj::gateOn, dj::slope})
            processor.setDjParam(parameter, 1);
        processor.setDjParam(dj::filter, -.36f);
        processor.setDjParam(dj::low, -3);
        processor.setDjParam(dj::mid, 2);
        processor.setDjParam(dj::high, -4);
    }
    if (fullMixer)
    {
        processor.setDjParam(dj::inputGain, 2);
        processor.setDjParam(dj::inputHp, 140);
        processor.setDjParam(dj::inputLp, 12000);
        processor.setDjParam(dj::noiseOn, 1);
        processor.setDjParam(dj::morphOn, 1);
        processor.setDjParam(dj::morphAmount, .5f);
        processor.setDjParam(dj::coastOn, 1);
    }
    std::cout << std::fixed << std::setprecision(3)
              << "case,rate,block,n,mean_us,p95_us,p99_us,max_us,over_budget,mean_budget_pct\n";
    for (const int rate : {44100, 48000, 96000})
        for (const int size : {64, 128, 256, 512})
        {
            processor.prepareToPlay(rate, size);
            processor.setParam("play", 1);
            processor.setParam("cutMode", 0);
            juce::AudioBuffer<float> buffer(2, size);
            juce::MidiBuffer midi;
            midi.ensureSize(256);
            std::vector<double> times;
            times.reserve(2100);
            for (int i = 0; i < 2100; ++i)
            {
                buffer.clear();
                processor.mouseHeld.store(true);
                processor.mouseSpeed.store(i % 24 < 12 ? 4.6f : -3.2f);
                processor.state.getRawParameterValue("fader")->store(static_cast<float>(i % 32) / 31);
                processor.state.getRawParameterValue("curve")->store(0.9f + static_cast<float>(i % 17) / 17);
                if (i % 17 == 0)
                    midi.addEvent(juce::MidiMessage::noteOn(1, 48 + i % 10, 0.8f), size / 2);
                if (i % 17 == 7)
                    midi.addEvent(juce::MidiMessage::allNotesOff(1), size / 3);
                const auto start = Clock::now();
                processor.processBlock(buffer, midi);
                const auto us = elapsed(start);
                if (i >= 100)
                    times.push_back(us);
            }
            summary("dsp", rate, size, std::move(times));
        }
    processor.mouseHeld.store(false);
    processor.prepareToPlay(48000, 128);
    processor.setParam("play", 0);
    std::cout << "reported_latency_samples," << processor.getLatencySamples() << '\n';
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    editor->setVisible(true);
    std::vector<double> frames, drags, rotaryDrags;
    juce::Component *platter = nullptr;
    juce::Slider *rotary = nullptr;
    for (const auto *child : editor->getChildren())
    {
        for (auto *nested : child->getChildren())
        {
            if (nested->getComponentID() == "vinyl")
                platter = nested;
            if (nested->getComponentID() == "cutIn")
                rotary = dynamic_cast<juce::Slider *>(nested);
        }
        if (child->getComponentID() == "vinyl")
            platter = const_cast<juce::Component *>(child);
    }
    const auto mouse = juce::Desktop::getInstance().getMainMouseSource();
#if JUCE_WINDOWS
    const auto guiCpuBefore = threadCpuMicros();
#endif
    const auto guiStart = Clock::now();
    for (int i = 0; i < 42; ++i)
    {
        processor.progress.store(i * 0.011);
        processor.setParam("play", 1);
        juce::AudioBuffer<float> frameAudio(2, 128);
        juce::MidiBuffer frameMidi;
        processor.processBlock(frameAudio, frameMidi);
        if (platter)
        {
            const juce::Point<float> pos(platter->getWidth() * 0.8f, platter->getHeight() * 0.5f);
            const juce::MouseEvent down(mouse, pos, juce::ModifierKeys::leftButtonModifier, 1, 0, 0, 0, 0,
                                        platter, platter, juce::Time::getCurrentTime(), pos,
                                        juce::Time::getCurrentTime(), 1, false);
            platter->mouseDown(down);
            const auto start = Clock::now();
            platter->mouseDrag(down.withNewPosition(pos.translated(0, 40)));
            const auto us = elapsed(start);
            platter->mouseUp(down);
            if (i >= 2)
                drags.push_back(us);
        }
        const auto start = Clock::now();
        auto frame = editor->createComponentSnapshot(editor->getLocalBounds());
        const auto us = elapsed(start);
        if (i >= 2)
            frames.push_back(us);
    }
    const auto guiWall = elapsed(guiStart);
#if JUCE_WINDOWS
    const auto guiCpu = threadCpuMicros() - guiCpuBefore;
    std::cout << "gui_snapshot_thread_cpu," << guiCpu << ',' << guiWall << ',' << (100 * guiCpu / guiWall)
              << ',' << (42e6 / guiWall) << '\n';
#endif
    if (rotary)
        for (int i = 0; i < 40; ++i)
        {
            processor.setParam("cutIn", 0.08f);
            const auto pos = rotary->getLocalBounds().toFloat().getCentre();
            const juce::MouseEvent down(mouse, pos, juce::ModifierKeys::leftButtonModifier, 1, 0, 0, 0, 0,
                                        rotary, rotary, juce::Time::getCurrentTime(), pos,
                                        juce::Time::getCurrentTime(), 1, false);
            rotary->mouseDown(down);
            const auto start = Clock::now();
            rotary->mouseDrag(down.withNewPosition(pos.translated(0, -30)));
            const auto us = elapsed(start);
            if (processor.param("cutIn") > 0.08f)
                rotaryDrags.push_back(us);
            rotary->mouseUp(down);
        }
    if (!rotaryDrags.empty())
        summary("rotary_handler_to_apvts", 0, 0, std::move(rotaryDrags));
    if (!drags.empty())
        summary("mouse_handler", 0, 0, std::move(drags));
    summary("full_editor_snapshot", 0, 0, std::move(frames));
#ifdef SKRATCH_BENCH_TELEMETRY
    if (platter)
    {
        processor.latencyMetrics.enabled.store(true);
        juce::AudioBuffer<float> audio(2, 128);
        juce::MidiBuffer midi;
        const auto pos = juce::Point<float>(platter->getWidth() * 0.8f, platter->getHeight() * 0.5f);
        for (int i = 0; i < 64; ++i)
        {
            const juce::MouseEvent down(mouse, pos, juce::ModifierKeys::leftButtonModifier, 1, 0, 0, 0, 0,
                                        platter, platter, juce::Time::getCurrentTime(), pos,
                                        juce::Time::getCurrentTime(), 1, false);
            platter->mouseDown(down);
            platter->mouseDrag(down.withNewPosition(pos.translated(0, i % 2 ? -32.0f : 32.0f)));
            processor.processBlock(audio, midi);
            auto frame = platter->createComponentSnapshot(platter->getLocalBounds());
            platter->mouseUp(down);
            processor.publishPadAxes(true, i % 2 ? 2.0f : -2.0f, 1);
            processor.processBlock(audio, midi);
        }
        const auto print = [](const char *name, const LatencySeries &series)
        {
            const auto n = series.samples.load();
            if (n > 0)
                std::cout << name << ',' << n << ','
                          << juce::Time::highResolutionTicksToSeconds(series.totalTicks.load()) * 1e6 / n
                          << ',' << juce::Time::highResolutionTicksToSeconds(series.maxTicks.load()) * 1e6
                          << '\n';
        };
        print("telemetry_mouse_handler", processor.latencyMetrics.mouseHandler);
        print("telemetry_mouse_to_software_paint", processor.latencyMetrics.mouseToPaint);
        print("telemetry_mouse_to_next_dsp", processor.latencyMetrics.mouseToDsp);
        print("telemetry_simulated_pad_to_next_dsp", processor.latencyMetrics.padToDsp);
        print("telemetry_platter_paint", processor.latencyMetrics.platterPaint);
        print("telemetry_dsp_block", processor.latencyMetrics.block);
        std::cout << "telemetry_dsp_late," << processor.latencyMetrics.lateBlocks.load() << '\n';
    }
#endif
}
