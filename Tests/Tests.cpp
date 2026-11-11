#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <iostream>
#include <stdexcept>
void runUpgradeTests(const juce::File &);
void runSkinTests(NeedleProcessor &, NeedleEditor &, const juce::File &);
void runInstrumentTests(const juce::File &);
void runDjTests(NeedleEditor &, const juce::File &);
void runMixerTests();

static void require(bool condition, const char *what)
{
    if (!condition)
        throw std::runtime_error(what);
    std::cout << "PASS " << what << std::endl;
}

static float render(NeedleProcessor &p, int blocks = 32, float input = 0.0f)
{
    juce::AudioBuffer<float> audio(2, 256);
    juce::MidiBuffer midi;
    float peak = 0;
    for (int n = 0; n < blocks; ++n)
    {
        for (int c = 0; c < 2; ++c)
            juce::FloatVectorOperations::fill(audio.getWritePointer(c), input, 256);
        p.processBlock(audio, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 256; ++i)
            {
                const float v = audio.getSample(c, i);
                if (!std::isfinite(v) || std::abs(v) > 1.0f)
                    throw std::runtime_error("non-finite or unclamped output");
                peak = std::max(peak, std::abs(v));
            }
    }
    return peak;
}

int main(int argc, char **argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    try
    {
        require(needle::wrap(-1, 100) == 99, "negative wrap");
        float data[] = {0, 1, 0, -1};
        require(std::abs(needle::readCubic(data, 4, -1) + 1) < 1e-6, "reverse interpolation");
        NeedleProcessor p;
        require(p.view().status[0] == 0x0413, "Russian status is decoded as Unicode");
        p.prepareToPlay(48000, 256);
        require(render(p) < 1e-6, "paused deck silent without input");
        p.setParam("play", 1);
        require(render(p) > 0.1f, "demo playback emits audio");
        const auto before = p.progress.load();
        render(p);
        require(p.progress.load() > before, "forward position advances");
        p.setParam("play", 0);
        p.seek.store(0.5);
        p.keySpeed.store(-1.7f);
        render(p);
        require(p.progress.load() < 0.5 && p.actualSpeed.load() < -1.6f, "keyboard reverse scratch");
        p.keySpeed.store(0);
        p.mouseHeld.store(true);
        p.mouseSpeed.store(2.2f);
        render(p);
        require(p.actualSpeed.load() > 2.1f, "mouse scratch velocity");
        p.mouseSpeed.store(0);
        render(p, 64);
        require(std::abs(p.actualSpeed.load()) < 0.001f, "holding still stops record");
        require(render(p) < 1e-6, "stationary record emits no held-sample DC");
        p.mouseHeld.store(false);
        p.setParam("play", 1);
        p.cutHeld.store(true);
        render(p);
        require(render(p) < 1e-6, "momentary cut closes deck");
        p.cutHeld.store(false);
        p.setParam("fader", 0);
        render(p);
        require(render(p) < 1e-6, "cut-mode left edge closes deck");
        p.setParam("reverse", 1);
        require(render(p) > 0.1f, "hamster reverses fader");
        p.setParam("reverse", 0);
        p.setParam("cutMode", 0);
        p.setParam("play", 0);
        render(p);
        require(std::abs(render(p, 1, 0.4f) - 0.4f * juce::Decibels::decibelsToGain(-6.0f)) < 0.001f,
                "crossfader passes DAW input");
        p.setParam("fader", 1);
        p.setParam("cutMode", 1);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 36, (juce::uint8)100), 64);
        juce::AudioBuffer<float> block(2, 256);
        block.clear();
        p.seek.store(0.5);
        p.processBlock(block, midi);
        render(p);
        require(p.actualSpeed.load() < -0.99f, "MIDI note 36 reverse scratch");
        midi.addEvent(juce::MidiMessage::allNotesOff(1), 0);
        block.clear();
        p.processBlock(block, midi);
        render(p, 64);
        require(std::abs(p.actualSpeed.load()) < 0.001f, "MIDI all-notes-off releases scratch");
        p.setParam("loop", 0);
        p.seek.store(0.9999);
        p.setParam("play", 1);
        render(p, 64);
        require(render(p) < 1e-6 && p.progress.load() == 1.0, "non-loop playback stops at end");
        p.setParam("loop", 1);
        p.seek.store(0.9999);
        require(render(p) > 0.1f, "loop wraps into sample");
        p.cue.store(0.25);
        p.returnToCue();
        render(p, 1);
        require(p.progress.load() > 0.25 && p.progress.load() < 0.26, "cue seeks precisely");
        p.setParam("gain", -12);
        p.setParam("pitch", 7);
        juce::MemoryBlock state;
        p.getStateInformation(state);
        NeedleProcessor restored;
        restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        require(std::abs(restored.param("gain") + 12) < 0.001f &&
                    std::abs(restored.param("pitch") - 7) < 0.001f && restored.cue.load() == 0.25,
                "project state restores parameters and cue");
        // Real PCM file: stereo 44.1 kHz, generated independently from the decoder.
        auto temp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getNonexistentChildFile("needle-test", ".wav");
        {
            juce::FileOutputStream out(temp);
            const int samples = 44100;
            out.write("RIFF", 4);
            out.writeInt(36 + samples * 4);
            out.write("WAVEfmt ", 8);
            out.writeInt(16);
            out.writeShort(1);
            out.writeShort(2);
            out.writeInt(44100);
            out.writeInt(44100 * 4);
            out.writeShort(4);
            out.writeShort(16);
            out.write("data", 4);
            out.writeInt(samples * 4);
            for (int n = 0; n < samples; ++n)
            {
                const short s = static_cast<short>(std::sin(n * 0.03) * 18000);
                out.writeShort(s);
                out.writeShort(-s);
            }
        }
        p.cue.store(0);
        p.loadFile(temp);
        for (int n = 0; n < 1000 && p.loading.load(); ++n)
            juce::Thread::sleep(5);
        require(!p.loading.load() && p.view().name == temp.getFileName() &&
                    std::abs(p.view().seconds - 1) < 0.001,
                "async WAV load and sample rate");
        p.prepareToPlay(48000, 256);
        p.seek.store(0);
        p.setParam("pitch", 0);
        p.setParam("gain", -6);
        p.setParam("play", 1);
        require(render(p) > 0.1f, "loaded stereo PCM plays through resampler");
        juce::MemoryBlock fileState;
        p.getStateInformation(fileState);
        restored.setStateInformation(fileState.getData(), static_cast<int>(fileState.getSize()));
        for (int n = 0; n < 1000 && restored.loading.load(); ++n)
            juce::Thread::sleep(5);
        require(restored.view().name == temp.getFileName(), "project state reloads external file");
        p.loadFile(temp.getSiblingFile("missing-needle.wav"));
        for (int n = 0; n < 1000 && p.loading.load(); ++n)
            juce::Thread::sleep(5);
        require(!p.loading.load() &&
                    p.view().status !=
                        juce::String::fromUTF8("\xd0\x93\xd0\xbe\xd1\x82\xd0\xbe\xd0\xb2\xd0\xbe") &&
                    p.view().name == temp.getFileName(),
                "bad file reports error and preserves loaded audio");
        temp.deleteFile();
        if (argc > 2)
        {
            const juce::File fixtures(juce::String::fromUTF8(argv[2]));
            for (const auto *extension : {"wav", "mp3", "flac", "aiff", "ogg"})
            {
                const auto file = fixtures.getChildFile(juce::String("tone.") + extension);
                p.loadFile(file);
                for (int n = 0; n < 1000 && p.loading.load(); ++n)
                    juce::Thread::sleep(5);
                require(!p.loading.load() && p.view().name == file.getFileName(),
                        (juce::String("decodes ") + extension).toRawUTF8());
                p.seek.store(0);
                require(render(p) > 0.02f, (juce::String("renders ") + extension).toRawUTF8());
            }
        }
        // Headless native component snapshot; the actual editor code paints this image.
        runUpgradeTests(juce::File::getCurrentWorkingDirectory().getChildFile("work/upgrade-test-artifacts"));
        p.loadDemo();
        p.prepareToPlay(48000, 256);
        p.setParam("play", 0);
        std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
        auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.5f);
        auto destination = argc > 1 ? juce::File(juce::String::fromUTF8(argv[1]))
                                    : juce::File::getCurrentWorkingDirectory().getChildFile("preview.png");
        destination.deleteFile();
        juce::FileOutputStream stream(destination);
        juce::PNGImageFormat png;
        require(stream.openedOk() && png.writeImageToStream(image, stream), "native editor renders PNG");
        stream.flush();
        auto *native = dynamic_cast<NeedleEditor *>(editor.get());
        require(native != nullptr, "native editor type");
        for (int panel = 1; panel < 8; ++panel)
        {
            native->selectPanel(panel);
            auto panelImage = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
            auto panelPath = destination.getSiblingFile("panel-" + juce::String(panel) + ".png");
            panelPath.deleteFile();
            juce::FileOutputStream panelFile(panelPath);
            require(panelFile.openedOk() && png.writeImageToStream(panelImage, panelFile),
                    "tool panel renders");
        }
        editor->setSize(940, 760);
        native->selectPanel(3);
        auto narrow = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
        auto narrowPath = destination.getSiblingFile("laptop-preview.png");
        narrowPath.deleteFile();
        juce::FileOutputStream narrowFile(narrowPath);
        require(narrowFile.openedOk() && png.writeImageToStream(narrow, narrowFile),
                "940x760 laptop layout renders");
        native->selectPanel(0);
        editor->setSize(SkratchSkin::width, SkratchSkin::height);
        runSkinTests(p, *native, destination.getParentDirectory());
        runInstrumentTests(destination.getParentDirectory());
        runDjTests(*native, destination.getParentDirectory());
        runMixerTests();
        std::cout << "All tests passed" << std::endl;
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "FAIL " << e.what() << std::endl;
        return 1;
    }
}
