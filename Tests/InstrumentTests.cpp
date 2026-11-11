#include "PluginProcessor.h"
#include <iostream>
#include <stdexcept>

namespace
{
void verify(bool condition, const char *description)
{
    if (!condition)
        throw std::runtime_error(description);
    std::cout << "PASS " << description << '\n';
}

int firstAudible(const juce::AudioBuffer<float> &buffer)
{
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        if (std::abs(buffer.getSample(0, i)) > 1e-8f)
            return i;
    return -1;
}
} // namespace

void runInstrumentTests(const juce::File &directory)
{
    const auto synthOwner = std::make_unique<NeedleProcessor>(true);
    auto &synth = *synthOwner;
    verify(synth.getBusCount(true) == 0 && synth.getTotalNumOutputChannels() == 2,
           "Instrument exposes no audio input and one stereo output");
    synth.prepareToPlay(48000, 512);
    synth.setParam("midiMode", 1);
    synth.setParam("gain", 0);
    synth.cue.store(0.25);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buffer(2, 512);
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 111);
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 151);
    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 171);
    synth.processBlock(buffer, midi);
    verify(firstAudible(buffer) >= 111 && firstAudible(buffer) < 116 && buffer.getMagnitude(380, 130) > 0.02f,
           "One Shot begins at MIDI sample offset and overlapping repeated note stays held");
    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 20);
    synth.processBlock(buffer, midi);
    verify(buffer.getMagnitude(350, 162) == 0, "One Shot Note Off releases within the documented envelope");

    synth.setParam("midiMode", 0);
    synth.prepareToPlay(48000, 512);
    midi.addEvent(juce::MidiMessage::noteOn(1, 53, 0.9f), 200);
    synth.processBlock(buffer, midi);
    verify(firstAudible(buffer) >= 200 && buffer.getMagnitude(200, 312) > 0.01f,
           "Pattern note starts after its sample offset in the same block");
    midi.addEvent(juce::MidiMessage::allSoundOff(1), 0);
    synth.processBlock(buffer, midi);
    synth.setParam("midiMode", 2);
    synth.learnTarget.store(NeedleProcessor::liveLearnBase + 1);
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 25, 127), 50);
    synth.processBlock(buffer, midi);
    verify(synth.liveCC[1].load() == 25 && synth.learnTarget.load() == -1,
           "Live MIDI gain CC learns at offset");
    juce::MemoryBlock state;
    synth.getStateInformation(state);
    const auto restoredOwner = std::make_unique<NeedleProcessor>(true);
    auto &restored = *restoredOwner;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    verify(restored.liveCC[1].load() == 25 && restored.param("midiMode") == 2,
           "Live MIDI mapping and MIDI mode survive state restore");

    const auto sample = directory.getChildFile("portable-sample.wav");
    {
        juce::WavAudioFormat wav;
        juce::AudioBuffer<float> audio(1, 2048);
        for (int i = 0; i < audio.getNumSamples(); ++i)
            audio.setSample(0, i, 0.5f * std::sin(i * 0.15f));
        sample.deleteFile();
        std::unique_ptr<juce::OutputStream> stream = sample.createOutputStream();
        auto writer = wav.createWriterFor(
            stream,
            juce::AudioFormatWriterOptions{}.withSampleRate(48000).withNumChannels(1).withBitsPerSample(16));
        verify(writer != nullptr && writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()),
               "write portable WAV fixture");
    }
    synth.loadFile(sample);
    for (int i = 0; i < 400 && synth.loading.load(); ++i)
        juce::Thread::sleep(2);
    synth.setParam("midiMode", 1);
    synth.cue.store(0);
    synth.getStateInformation(state);
    sample.deleteFile();
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    for (int i = 0; i < 400 && restored.loading.load(); ++i)
        juce::Thread::sleep(2);
    restored.prepareToPlay(48000, 512);
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 23);
    restored.processBlock(buffer, midi);
    verify(restored.view().name == sample.getFileName() && buffer.getMagnitude(0, 512) > 0.04f,
           "embedded project sample restores and plays after the original file is removed");

    const auto effectOwner = std::make_unique<NeedleProcessor>();
    auto &effect = *effectOwner;
    effect.prepareToPlay(48000, 512);
    effect.setParam("source", 1);
    effect.setParam("performance", 1);
    const auto feed = [&](int block)
    {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            for (int channel = 0; channel < 2; ++channel)
                buffer.setSample(channel, i, 0.4f * std::sin((block * 512 + i) * 0.037f));
        effect.processBlock(buffer, midi);
    };
    for (int i = 0; i < 12; ++i)
        feed(i);
    effect.keySpeed.store(-1);
    feed(12);
    verify(effect.actualSpeed.load() < -0.8f && buffer.getMagnitude(0, 512) > 0.04f &&
               effect.getLatencySamples() == 0,
           "FX reverses captured live input and reports zero PDC samples");
}
