#include "PluginProcessor.h"
#include <iostream>
#include <stdexcept>

namespace
{
void verify(bool condition, const char *name)
{
    if (!condition)
        throw std::runtime_error(name);
    std::cout << "PASS " << name << '\n';
}

double response(dj::Values values, int rate, double hz)
{
    dj::InputStrip strip;
    strip.prepare(rate);
    strip.configure(values);
    double energy = 0;
    for (int i = 0; i < rate / 4; ++i)
    {
        float data[]{.2f * static_cast<float>(std::sin(i * 2 * skratch::pi * hz / rate))};
        strip.frame(data, 1);
        if (i >= rate / 8)
            energy += data[0] * data[0];
    }
    return std::sqrt(energy / (rate / 8));
}
} // namespace

void runMixerTests()
{
    auto parameters = dj::defaults();
    dj::InputStrip strip;
    strip.prepare(48000);
    strip.configure(parameters);
    verify(!strip.active(), "neutral source strip is an exact bypass");
    const auto baseline = response(parameters, 48000, 500);
    parameters[dj::inputGain] = 6;
    verify(std::abs(response(parameters, 48000, 500) / baseline - 1.995) < .02,
           "input gain is independent of the post-mix master");
    parameters[dj::inputGain] = 0;
    for (int rate : {44100, 48000, 96000})
    {
        parameters[dj::inputHp] = 1000;
        parameters[dj::inputLp] = 20000;
        verify(response(parameters, rate, 60) < .025, "source high-pass removes low frequency");
        parameters[dj::inputHp] = 0;
        parameters[dj::inputLp] = 750;
        verify(response(parameters, rate, 6500) < .032, "source low-pass removes high frequency");
    }
    parameters = dj::defaults();
    parameters[dj::noiseOn] = 1;
    parameters[dj::noiseThreshold] = -30;
    parameters[dj::noiseRelease] = 15;
    strip.configure(parameters);
    float quiet = 1, linked = 0;
    for (int i = 0; i < 9000; ++i)
    {
        float x[]{.005f, -.005f};
        strip.frame(x, 2);
        quiet = x[0];
    }
    for (int i = 0; i < 1500; ++i)
    {
        float x[]{.3f, -.005f};
        strip.frame(x, 2);
        linked = x[1];
    }
    verify(quiet < .00003f && linked < -.0049f,
           "noise gate suppresses quiet signal and links stereo channels");

    dj::Morph morph;
    morph.prepare(48000);
    parameters[dj::morphOn] = 1;
    parameters[dj::morphAmount] = .75f;
    std::pair<float, float> lanes;
    for (int i = 0; i < 1500; ++i)
        lanes = morph.frame(parameters, 0, 0, -2, 0, 2, 1);
    verify(std::abs(lanes.first - 1) < .001f && std::abs(lanes.second - .75f) < .001f,
           "Pattern Morph interpolates platter and fader independently");
    parameters[dj::morphB] = 7;
    const auto changed = morph.frame(parameters, 0, 0, -2, 0, -4, 0);
    verify(std::abs(changed.first - lanes.first) < .002f && std::abs(changed.second - lanes.second) < .002f,
           "live Morph slot change is continuous");

    dj::ManualCoast coast;
    coast.prepare(48000);
    coast.configure(true, .22f);
    verify(coast.apply(-2, -.9, true, true) == -2, "optional inertia does not change direct scratch");
    const double first = coast.apply(0, 0, false, true);
    double later = 0;
    for (int i = 0; i < 24000; ++i)
        later = coast.apply(0, 0, false, true);
    verify(first < -.89 && later < 0 && std::abs(later) < .5 && coast.isMoving(),
           "manual coast decays signed speed");
    coast.cancel();
    verify(coast.apply(0, 0, false, true) == 0, "Stop cancels manual coast");

    NeedleProcessor synth(true);
    synth.prepareToPlay(48000, 256);
    synth.setDjParam(dj::morphOn, 1);
    synth.setDjParam(dj::morphA, 10);
    synth.setDjParam(dj::morphAmount, 0);
    juce::AudioBuffer<float> audio(2, 256);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 48, 1.0f), 91);
    synth.processBlock(audio, midi);
    verify(synth.actualSpeed.load() < -.4f && audio.getMagnitude(0, 91) == 0,
           "native MIDI pattern invokes morphed trajectories after its sample offset");
    synth.learnTarget.store(dj::learnBase + dj::inputHp);
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 73, 127), 47);
    synth.processBlock(audio, midi);
    verify(synth.djCC[dj::inputHp].load() == 73 && synth.djValue(dj::inputHp) == 1500,
           "new high index parameter supports sample-offset MIDI Learn");
    synth.setDjParam(dj::inputHp, 360);
    synth.processBlock(audio, midi);
    verify(synth.djValue(dj::inputHp) == 360, "host edit clears the second-word MIDI override");

    juce::MemoryBlock state;
    synth.getStateInformation(state);
    NeedleProcessor restored(true);
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    verify(restored.djValue(dj::inputHp) == 360 && restored.djValue(dj::morphA) == 10 &&
               restored.djCC[dj::inputHp].load() == 73,
           "new mixer controls and MIDI learn survive host state restore");
    const auto legacy = juce::File(SKRATCH_FIXTURES_DIR).getChildFile("2.2-demo.skratch");
    verify(restored.loadPreset(legacy).wasOk() && restored.getPattern(12).platter[20] == -3.25f &&
               restored.djValue(dj::inputHp) == 0 && restored.djValue(dj::morphOn) == 0 &&
               restored.djCC[dj::inputHp].load() == -1,
           "actual 2.2 preset retains old data with neutral 2.3 defaults");
    const auto prior = juce::File(SKRATCH_FIXTURES_DIR).getChildFile("2.3-demo.skratch");
    verify(restored.loadPreset(prior).wasOk() && restored.getPattern(12).platter[20] == -3.25f &&
               !restored.takeLoop.load() && restored.speedCC.load() == 74 && restored.noteMap[0].load() == 72,
           "0.9.0-beta loads the actual 2.3 preset with pattern, take and MIDI mappings");

    NeedleProcessor effect;
    effect.prepareToPlay(48000, 256);
    effect.setParam("source", 1);
    effect.setParam("gain", 0);
    effect.setDjParam(dj::inputGain, -12);
    for (int block = 0; block < 24; ++block)
    {
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < 256; ++i)
                audio.setSample(channel, i, .2f);
        effect.processBlock(audio, midi);
    }
    verify(std::abs(audio.getSample(0, 255) - .2f * std::pow(10.0f, -.6f)) < .0002f,
           "automated input gain reaches the FX Live Input route without changing master");
}
