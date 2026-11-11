#include "PluginProcessor.h"
#include <functional>
#include <iostream>
#include <stdexcept>

namespace
{
constexpr int rate = 48000;
int failures = 0;
using Setup = std::function<void(NeedleProcessor &)>;
using Signal = std::function<float(int)>;
using Stereo = std::array<std::vector<float>, 2>;

Signal sine(double hz, float amplitude = .2f)
{
    return [=](int i) { return amplitude * static_cast<float>(std::sin(2 * skratch::pi * hz * i / rate)); };
}

void report(const char *id, const char *metric, double a, double b, bool pass)
{
    std::cout << id << ",\"" << metric << "\"," << a << ',' << b << ',' << (pass ? "PASS" : "FAIL") << std::endl;
    if (!pass)
        ++failures;
}

Stereo render(const Setup &setup, const Signal &signal, int blocks = 96, bool instrument = false,
              const std::function<void(NeedleProcessor &, juce::MidiBuffer &, int)> &events = {})
{
    NeedleProcessor p(instrument);
    p.setParam("source", 1);
    p.setParam("gain", 0);
    p.setNonRealtime(true);
    p.prepareToPlay(rate, 256);
    setup(p);
    juce::AudioBuffer<float> audio(2, 256);
    juce::MidiBuffer midi;
    Stereo result;
    for (int n = 0; n < blocks; ++n)
    {
        midi.clear();
        if (events)
            events(p, midi, n);
        for (int i = 0; i < 256; ++i)
        {
            const float x = signal(n * 256 + i);
            audio.setSample(0, i, x);
            audio.setSample(1, i, -.63f * x);
        }
        p.processBlock(audio, midi);
        for (int c = 0; c < 2; ++c)
        {
            const auto *begin = audio.getReadPointer(c);
            if (!std::all_of(begin, begin + 256, [](float v) { return std::isfinite(v) && std::abs(v) <= 1; }))
                throw std::runtime_error("invalid output");
            result[c].insert(result[c].end(), begin, begin + 256);
        }
    }
    return result;
}

double rms(const std::vector<float> &data, size_t first = 4096, size_t count = 0)
{
    count = count == 0 ? data.size() - first : count;
    double sum = 0;
    for (size_t i = first; i < first + count; ++i)
        sum += data[i] * data[i];
    return std::sqrt(sum / count);
}

double difference(const Stereo &a, const Stereo &b)
{
    double sum = 0;
    for (int c = 0; c < 2; ++c)
        for (size_t i = 0; i < a[c].size(); ++i)
        {
            const double d = a[c][i] - b[c][i];
            sum += d * d;
        }
    return std::sqrt(sum / (2 * a[0].size()));
}

double spectral(const std::vector<float> &a, int hz)
{
    constexpr int start = 4800, count = 9600;
    double real = 0, imag = 0;
    for (int i = 0; i < count; ++i)
    {
        const double phase = 2 * skratch::pi * hz * i / rate;
        real += a[start + i] * std::cos(phase);
        imag += a[start + i] * std::sin(phase);
    }
    return 2 * std::hypot(real, imag) / count;
}

void wav(const juce::File &directory, const char *name, const Stereo &samples)
{
    auto file = directory.getChildFile(juce::String(name) + ".wav");
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    juce::WavAudioFormat format;
    const auto options = juce::AudioFormatWriterOptions{}.withSampleRate(rate).withNumChannels(2).withBitsPerSample(24);
    auto writer = format.createWriterFor(stream, options);
    const int count = static_cast<int>(samples[0].size());
    juce::AudioBuffer<float> audio(2, count);
    for (int c = 0; c < 2; ++c)
        audio.copyFrom(c, 0, samples[c].data(), count);
    if (!writer || !writer->writeFromAudioSampleBuffer(audio, 0, count))
        throw std::runtime_error("WAV writer");
}

Setup control(dj::Param parameter, float value, Setup base = {})
{
    return [=](NeedleProcessor &p) {
        if (base)
            base(p);
        p.setDjParam(parameter, value);
    };
}

void level(dj::Param id, float a, float b, const Signal &signal, double minRatio, double maxRatio,
           Setup base = {})
{
    const auto x = render(control(id, a, base), signal);
    const auto y = render(control(id, b, base), signal);
    const double left = rms(y[0]) / rms(x[0]);
    const double right = rms(y[1]) / rms(x[1]);
    report(dj::definitions[id].id, "RMS ratio L/R", left, right,
           left >= minRatio && left <= maxRatio && right >= minRatio && right <= maxRatio);
}
}

int main(int argc, char **argv)
{
    juce::ScopedJuceInitialiser_GUI juce;
    try
    {
        const auto examples = juce::File::getCurrentWorkingDirectory().getChildFile(argc > 1 ? argv[1] : "audio-examples");
        if (examples.createDirectory().failed())
            throw std::runtime_error("WAV directory");
        std::cout << "APVTS ID,metric,A,B,result\n";
        const Setup neutral = [](NeedleProcessor &) {};
        const auto rack = control(dj::enabled, 1);
        const auto dry = render(neutral, sine(1000));
        const auto neutralRack = render(rack, sine(1000));
        report("dj.enabled", "neutral null RMS", 0, difference(dry, neutralRack), dry == neutralRack);
        level(dj::inputGain, 0, -12, sine(1000), .250, .252);
        level(dj::inputHp, 0, 1500, sine(100), .05, .13);
        level(dj::inputLp, 20000, 600, sine(8000), .05, .12);
        const auto gate = control(dj::noiseOn, 1);
        level(dj::noiseThreshold, -80, -20, sine(1000, .01f), 0, .25, gate);
        level(dj::low, 0, 12, sine(100), 3.4, 4.2, rack);
        level(dj::mid, 0, 12, sine(1000), 2.8, 4.2, rack);
        level(dj::high, 0, 12, sine(8000), 3.0, 4.2, rack);
        level(dj::filter, 0, -.9f, sine(8000), 0, .12, rack);
        level(dj::resonance, 0, 1, sine(2200), 1.25, 6, control(dj::filter, -.375f, rack));
        level(dj::mix, 0, 1, sine(8000), 0, .12, control(dj::filter, -.9f, rack));

        const auto echo = [](NeedleProcessor &p) {
            p.setDjParam(dj::enabled, 1);
            p.setDjParam(dj::echoOn, 1);
            p.setDjParam(dj::echoMix, 1);
            p.setDjParam(dj::echoSync, 0);
        };
        const Signal impulse = [](int i) { return i == 4800 ? .4f : 0; };
        const auto shortEcho = control(dj::echoMs, 50, echo);
        const auto e = render(shortEcho, impulse);
        const auto longEcho = render(control(dj::echoMs, 100, echo), impulse);
        report("dj.echoMs", "impulse at frames 7200 and 9600", e[0][7200], longEcho[0][9600],
               e[0][7200] > .39 && longEcho[0][9600] > .39 && std::abs(longEcho[0][7200]) < .001);
        const auto lowFeedback = render(control(dj::feedback, .2f, shortEcho), impulse);
        const auto highFeedback = render(control(dj::feedback, .8f, shortEcho), impulse);
        report("dj.feedback", "second echo amplitude", lowFeedback[0][9600], highFeedback[0][9600],
               std::abs(lowFeedback[0][9600] - .08) < .002 && std::abs(highFeedback[0][9600] - .32) < .002);
        const auto loweredTail = render(shortEcho, impulse, 96, false,
            [](NeedleProcessor &p, juce::MidiBuffer &, int n) { if (n == 26) p.setParam("gain", -20); });
        report("gain", "stored echo amplitude with master 0 / -20 dB", e[0][7200], loweredTail[0][7200],
               std::abs(loweredTail[0][7200] / e[0][7200] - .1) < .002);

        const auto drive = control(dj::driveMix, 1, control(dj::driveOn, 1, rack));
        const auto lowDrive = render(control(dj::drive, 0, drive), sine(1000));
        const auto highDrive = render(control(dj::drive, 24, drive), sine(1000));
        const double lowThird = spectral(lowDrive[0], 3000), highThird = spectral(highDrive[0], 3000);
        report("dj.drive", "3 kHz harmonic with 1 kHz input", lowThird, highThird,
               highThird > .02 && highThird > lowThird * 10);
        const auto crush = control(dj::crushMix, 1, control(dj::crushOn, 1, rack));
        const auto bits2 = render(control(dj::bits, 2, crush), sine(1000));
        const auto bits16 = render(control(dj::bits, 16, crush), sine(1000));
        report("dj.bits", "low-amplitude quantized RMS", rms(bits2[0]), rms(bits16[0]),
               rms(bits2[0]) < .001 && rms(bits16[0]) > .1);
        const auto rate1 = render(control(dj::downsample, 1, crush), sine(8000));
        const auto rate48 = render(control(dj::downsample, 48, crush), sine(8000));
        report("dj.downsample", "8 kHz output amplitude", spectral(rate1[0], 8000), spectral(rate48[0], 8000),
               spectral(rate1[0], 8000) > .18 && spectral(rate48[0], 8000) < .01);
        const auto stutter = control(dj::gateSync, 0, control(dj::gateOn, 1, rack));
        level(dj::duty, .2f, .8f, sine(1000), 1.7, 2.8, stutter);

        const auto fileDeck = [](NeedleProcessor &p) { p.setParam("source", 0); p.setParam("play", 1); };
        const Signal silence = [](int) { return 0.0f; };
        for (bool instrument : {false, true})
        {
            const auto a = render(fileDeck, silence, 96, instrument);
            const auto b = render(control(dj::inputGain, -12, fileDeck), silence, 96, instrument);
            const double ratio = rms(b[0]) / rms(a[0]);
            report("dj.inputGain", instrument ? "Instrument deck gain ratio" : "FX file deck gain ratio",
                   rms(a[0]), ratio, rms(a[0]) > .01 && std::abs(ratio - .251189) < .003);
        }
        const auto learned = render([](NeedleProcessor &p) { p.learnTarget.store(dj::learnBase + dj::inputGain); },
            sine(1000), 96, false, [](NeedleProcessor &, juce::MidiBuffer &m, int n) {
                if (n == 20) m.addEvent(juce::MidiMessage::controllerEvent(1, 74, 0), 73);
            });
        report("dj.inputGain", "MIDI Learn gain ratio before/after event", rms(learned[0], 4500, 500),
               rms(learned[0], 18000, 2000), rms(learned[0], 18000, 2000) < .012);
        const auto bypassedDrive = render(control(dj::enabled, 0, control(dj::drive, 24, drive)), sine(1000));
        report("dj.enabled", "drive bypass null RMS", 0, difference(dry, bypassedDrive), dry == bypassedDrive);

        const auto deckLevel = [&](const char *id, float x, float y, Setup base, double maxRatio) {
            const auto a = render([&](NeedleProcessor &p) { fileDeck(p); base(p); p.setParam(id, x); }, silence);
            const auto b = render([&](NeedleProcessor &p) { fileDeck(p); base(p); p.setParam(id, y); }, silence);
            const double ratio = rms(b[0]) / rms(a[0]);
            report(id, "deck RMS before / after ratio", rms(a[0]), ratio, ratio < maxRatio);
        };
        deckLevel("fader", 1, 0, neutral, .001);
        deckLevel("cutIn", .01f, .35f, [](NeedleProcessor &p) { p.setParam("fader", .2f); }, .001);
        deckLevel("curve", .5f, 3, [](NeedleProcessor &p) {
            p.setParam("cutMode", 0); p.setParam("fader", .5f);
        }, .26);
        const auto response = [&](float milliseconds) {
            return render([&](NeedleProcessor &p) { fileDeck(p); p.setParam("response", milliseconds);
                    p.setParam("fader", 0); }, silence, 96, false,
                [](NeedleProcessor &p, juce::MidiBuffer &, int n) { if (n == 20) p.setParam("fader", 1); });
        };
        const auto fast = response(.1f), slow = response(15);
        report("response", "RMS during opening (first 5 ms)", rms(fast[0], 5120, 240), rms(slow[0], 5120, 240),
               rms(fast[0], 5120, 240) > rms(slow[0], 5120, 240) * 2.5);
        const auto motion = [&](Setup setup, int at, Setup action = {}) {
            float speed = 0;
            const auto sound = render(setup, silence, 96, true,
                [&](NeedleProcessor &p, juce::MidiBuffer &, int n) {
                    if (n == 12 && action) action(p);
                    if (n == at) speed = p.actualSpeed.load();
                });
            return std::make_pair(speed, rms(sound[0], 256 * (at - 1), 256));
        };
        const auto pitch = [&](float value) {
            return motion([&](NeedleProcessor &p) { fileDeck(p); p.setParam("pitch", value); }, 12);
        };
        const auto p0 = pitch(0), p40 = pitch(40);
        report("pitch", "measured playback ratio", p0.first, p40.first,
               std::abs(p40.first / p0.first - 1.4) < .002 && p40.second > .001);
        const auto sens = [&](float value) {
            return motion([&](NeedleProcessor &p) { p.setParam("sensitivity", value); p.keySpeed.store(1); }, 12);
        };
        const auto sens1 = sens(1), sens2 = sens(2);
        report("sensitivity", "measured key scratch ratio", sens1.first, sens2.first,
               std::abs(sens2.first / sens1.first - 2) < .002 && sens2.second > .001);
        const auto motor = control(dj::motorOn, 1, fileDeck);
        const auto torqueLow = motion(control(dj::torque, .25f, motor), 8);
        const auto torqueHigh = motion(control(dj::torque, 2, motor), 8);
        report("dj.torque", "start velocity at 42.7 ms", torqueLow.first, torqueHigh.first,
               torqueHigh.first > 4 * torqueLow.first && torqueHigh.second > .001);
        const auto quickStart = motion(control(dj::startMs, 20, motor), 8);
        const auto slowStart = motion(control(dj::startMs, 800, motor), 8);
        report("dj.startMs", "start velocity at 42.7 ms", quickStart.first, slowStart.first,
               quickStart.first > 8 * slowStart.first);
        const auto brake = [](NeedleProcessor &p) { p.requestDjAction(dj::brakeAction); };
        const auto fastBrake = motion(control(dj::brakeMs, 20, motor), 22, brake);
        const auto slowBrake = motion(control(dj::brakeMs, 700, motor), 22, brake);
        report("dj.brakeMs", "speed 53.3 ms after brake", fastBrake.first, slowBrake.first,
               std::abs(fastBrake.first) < .01 && slowBrake.first > .2);
        const auto spin = [](NeedleProcessor &p) { p.requestDjAction(dj::spinAction); };
        const auto slowSpin = motion(control(dj::spinSpeed, .5f, motor), 22, spin);
        const auto fastSpin = motion(control(dj::spinSpeed, 4, motor), 22, spin);
        report("dj.spinSpeed", "reverse speed 53.3 ms after spinback", slowSpin.first, fastSpin.first,
               fastSpin.first < -2 && slowSpin.first < -.1 && slowSpin.first > -.5);
        const auto release = [](NeedleProcessor &p) { p.keySpeed.store(0); };
        const auto coast = [](NeedleProcessor &p) { p.setDjParam(dj::coastOn, 1); p.keySpeed.store(2); };
        const auto lowFriction = motion(control(dj::friction, 0, coast), 60, release);
        const auto highFriction = motion(control(dj::friction, 1, coast), 60, release);
        report("dj.friction", "manual coast speed after 256 ms", lowFriction.first, highFriction.first,
               lowFriction.first > .9 && highFriction.first < .1);
        const auto morph = [](NeedleProcessor &p) {
            p.setDjParam(dj::morphOn, 1);
            p.setDjParam(dj::morphA, 1);
            p.setDjParam(dj::morphB, 10);
        };
        const auto pattern = [](NeedleProcessor &, juce::MidiBuffer &midi, int n) {
            if (n == 1) midi.addEvent(juce::MidiMessage::noteOn(1, 48, 1.f), 29);
        };
        const auto morphA = render(control(dj::morphAmount, 0, morph), silence, 96, true, pattern);
        const auto morphB = render(control(dj::morphAmount, 1, morph), silence, 96, true, pattern);
        report("dj.morphAmount", "MIDI pattern stereo null RMS", 0, difference(morphA, morphB),
               difference(morphA, morphB) > .025);
        const auto syncEcho = control(dj::echoSync, 1, control(dj::echoDivision, 4, echo));
        const auto tempoEcho = [&](float bpm, dj::Param parameter, float division) {
            return render([&](NeedleProcessor &p) { syncEcho(p); p.setParam("sync", 0); p.setParam("bpm", bpm);
                         p.setDjParam(parameter, division); }, impulse);
        };
        const auto bpm120 = tempoEcho(120, dj::echoDivision, 4), bpm240 = tempoEcho(240, dj::echoDivision, 4);
        report("bpm", "1/16 echo at frame 10800 / 7800", bpm120[0][10800], bpm240[0][7800],
               bpm120[0][10800] > .39 && bpm240[0][7800] > .39);
        const auto eighth = tempoEcho(120, dj::echoDivision, 3);
        report("dj.echoDivision", "1/16 and 1/8 echo at expected frames", bpm120[0][10800], eighth[0][16800],
               bpm120[0][10800] > .39 && eighth[0][16800] > .39 && std::abs(eighth[0][10800]) < .001);
        const auto swing = [&](float amount) {
            return render([&](NeedleProcessor &p) { p.setParam("sync", 0); p.setParam("quantize", 1);
                p.setParam("swing", amount); }, silence, 96, true, pattern);
        };
        const auto firstAudio = [](const Stereo &samples) {
            return std::find_if(samples[0].begin(), samples[0].end(), [](float x) { return std::abs(x) > .0001f; }) -
                samples[0].begin();
        };
        const auto straight = firstAudio(swing(0)), swung = firstAudio(swing(.5f));
        report("swing", "first audible frame on odd 1/16 grid", straight, swung,
               straight < 6500 && std::abs(swung - straight - 1500) <= 2);

        level(dj::noiseOn, 0, 1, sine(1000, .01f), 0, .25,
              control(dj::noiseThreshold, -20));
        level(dj::filterOn, 0, 1, sine(8000), 0, .12, control(dj::filter, -.9f, rack));
        level(dj::filterMix, 0, 1, sine(8000), 0, .12, control(dj::filter, -.9f, rack));
        level(dj::slope, 0, 1, sine(8000), 0, .25, control(dj::filter, -.62f, rack));
        level(dj::lowKill, 0, 1, sine(100), 0, .4, rack);
        level(dj::midKill, 0, 1, sine(1000), 0, .65, rack);
        level(dj::highKill, 0, 1, sine(8000), 0, .5, rack);
        level(dj::crushOn, 0, 1, sine(1000), 0, .01, control(dj::bits, 2, crush));
        level(dj::crushMix, 0, 1, sine(1000), 0, .01, control(dj::bits, 2, crush));
        level(dj::crushGain, 0, -12, sine(1000), .250, .252, crush);
        level(dj::driveGain, 0, -12, sine(1000), .250, .252, drive);
        level(dj::tone, 1, 0, sine(8000), 0, .18, drive);
        level(dj::gateMix, 0, 1, sine(1000), .55, .82, stutter);
        level(dj::depth, 0, 1, sine(1000), .55, .82, stutter);
        level(dj::gateOn, 0, 1, sine(1000), .55, .82, stutter);
        level(dj::gateFire, 0, 1, sine(1000), .55, .82, control(dj::gateMode, 1, stutter));
        const auto rateGate = [&](float hz) { return render(control(dj::gateRate, hz, stutter), [](int) { return .2f; }); };
        const auto openings = [](const Stereo &signal) {
            int count = 0;
            for (size_t i = 4097; i < signal[0].size(); ++i)
                if (signal[0][i] > .1f && signal[0][i - 1] <= .1f) ++count;
            return count;
        };
        const auto count4 = openings(rateGate(4)), count20 = openings(rateGate(20));
        report("dj.gateRate", "gate openings after startup at 4 / 20 Hz", count4, count20,
               count4 >= 1 && count20 >= 8 && count20 > count4 * 3);
        const auto disabledEcho = render(control(dj::echoOn, 0, shortEcho), impulse);
        report("dj.echoOn", "delayed impulse disabled/enabled", disabledEcho[0][7200], e[0][7200],
               disabledEcho[0][7200] == 0 && e[0][7200] > .39);
        const auto partialEcho = render(control(dj::echoMix, .25f, shortEcho), impulse);
        report("dj.echoMix", "direct / delayed impulse at 25 percent", partialEcho[0][4800], partialEcho[0][7200],
               std::abs(partialEcho[0][4800] - .3) < .002 && std::abs(partialEcho[0][7200] - .1) < .002);
        level(dj::driveOn, 0, 1, sine(1000, .02f), 2, 4, drive);
        level(dj::driveMix, 0, 1, sine(1000, .02f), 2, 4, drive);
        level(dj::gateMode, 0, 1, sine(1000), 1.25, 1.85, stutter);
        const auto syncedGate = control(dj::gateSync, 1, stutter);
        const auto gateDivision = [&](float division) {
            return openings(render(control(dj::gateDivision, division, syncedGate), [](int) { return .2f; }));
        };
        const int quarter = gateDivision(0), thirtySecond = gateDivision(3);
        report("dj.gateDivision", "gate openings at 1/4 and 1/32", quarter, thirtySecond,
               quarter <= 1 && thirtySecond >= 6);
        report("dj.gateSync", "free 4 Hz / host quarter gate openings", count4, quarter,
               count4 == 2 && quarter <= 1);
        const auto timingGate = control(dj::noiseThreshold, -30, gate);
        const Signal attackSignal = [](int i) { return i < 16384 ? 0.f : .2f; };
        const Signal releaseSignal = [](int i) { return i < 16384 ? .2f : .01f; };
        const auto gateAttackFast = render(control(dj::noiseAttack, .2f, timingGate), attackSignal);
        const auto gateAttackSlow = render(control(dj::noiseAttack, 15, timingGate), attackSignal);
        report("dj.noiseAttack", "level 2.1 ms after crossing above threshold", gateAttackFast[0][16484],
               gateAttackSlow[0][16484], gateAttackFast[0][16484] > .08f && gateAttackSlow[0][16484] < .08f);
        const auto gateReleaseFast = render(control(dj::noiseRelease, 6, timingGate), releaseSignal);
        const auto gateReleaseSlow = render(control(dj::noiseRelease, 250, timingGate), releaseSignal);
        report("dj.noiseRelease", "level 30 ms after crossing below threshold", gateReleaseFast[0][17824],
               gateReleaseSlow[0][17824], gateReleaseFast[0][17824] < .0002f && gateReleaseSlow[0][17824] > .008f);
        const auto driveMode = [&](float mode) {
            return render(control(dj::driveMode, mode, control(dj::drive, 22, drive)), sine(1000));
        };
        const auto soft = driveMode(0), hard = driveMode(1), saturate = driveMode(2);
        const auto softHard = difference(soft, hard), hardSat = difference(hard, saturate);
        report("dj.driveMode", "soft/hard and hard/sat null RMS", softHard, hardSat,
               softHard > .001 && hardSat > .01);
        const auto motorOff = motion(control(dj::motorOn, 0, fileDeck), 8);
        report("dj.motorOn", "start speed with motor off / slow torque", motorOff.first, torqueLow.first,
               motorOff.first > .99 && torqueLow.first < .1);
        const auto shapeA = motion(control(dj::acceleration, .4f, motor), 8);
        const auto shapeB = motion(control(dj::acceleration, 3, motor), 8);
        report("dj.acceleration", "start speed with soft / aggressive acceleration", shapeA.first, shapeB.first,
               shapeB.first > shapeA.first * 1.5);
        const auto coastOff = motion(control(dj::coastOn, 0, coast), 60, release);
        report("dj.coastOn", "speed after release with coast disabled / enabled", coastOff.first, lowFriction.first,
               std::abs(coastOff.first) < .001 && lowFriction.first > .9);
        const auto morphOff = render(control(dj::morphOn, 0, morph), silence, 96, true, pattern);
        const auto morphAChanged = render(control(dj::morphA, 10, morph), silence, 96, true, pattern);
        const auto morphBChanged = render(control(dj::morphB, 1, control(dj::morphAmount, 1, morph)),
                                          silence, 96, true, pattern);
        report("dj.morphOn", "disabled vs enabled pattern null RMS", 0, difference(morphOff, morphA),
               difference(morphOff, morphA) > .025);
        report("dj.morphA", "changed A, endpoint A null RMS", 0, difference(morphA, morphAChanged),
               difference(morphA, morphAChanged) > .025);
        report("dj.morphB", "changed B, endpoint B null RMS", 0, difference(morphB, morphBChanged),
               difference(morphB, morphBChanged) > .025);

        const auto clippingInput = sine(1000, .96f);
        const auto unlimited = render(neutral, clippingInput);
        const auto limited = render(control(dj::limiter, 1), clippingInput);
        report("dj.limiter", "positive peak before/after", unlimited[0][4812], limited[0][4812],
               unlimited[0][4812] > .95 && limited[0][4812] < .90 && limited[0][4812] > .78);

        {
            NeedleProcessor processor;
            processor.setParam("source", 1);
            processor.setParam("gain", 0);
            processor.setDjParam(dj::inputGain, -12);
            processor.prepareToPlay(rate, 256);
            processor.monitor.enabled.store(true);
            juce::AudioBuffer<float> buffer(2, 256);
            juce::MidiBuffer midi;
            for (int block = 0; block < 32; ++block)
            {
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                {
                    buffer.setSample(0, i, .2f);
                    buffer.setSample(1, i, -.2f);
                }
                processor.processBlock(buffer, midi);
            }
            const auto in = processor.monitor.inputRms.load();
            const auto out = processor.monitor.outputRms.load();
            report("monitor", "host input and output RMS", in, out,
                   processor.monitor.strip.load() && !processor.monitor.rack.load() &&
                   std::abs(in - .2) < .0001 && std::abs(out - .2 * .251189) < .0001);
            processor.monitor.enabled.store(false);
            buffer.clear();
            processor.processBlock(buffer, midi);
            report("monitor", "disabled leaves last snapshot untouched", out, processor.monitor.outputRms.load(),
                   processor.monitor.outputRms.load() == out);
        }

        const auto scratch = motion([](NeedleProcessor &p) { p.setParam("scratch", -2); }, 12);
        report("scratch", "automated reverse scratch velocity / RMS", scratch.first, scratch.second,
               scratch.first < -1.99 && scratch.second > .02);
        const auto speed45 = motion([&](NeedleProcessor &p) { fileDeck(p); p.setParam("rpm", 1); }, 12);
        report("rpm", "33 and 45 RPM playback speed", p0.first, speed45.first,
               std::abs(speed45.first / p0.first - 1.35) < .002);
        const auto reversed = motion([&](NeedleProcessor &p) { fileDeck(p); p.setParam("deckReverse", 1); }, 12);
        report("deckReverse", "reverse playback speed / RMS", reversed.first, reversed.second,
               reversed.first < -.99 && reversed.second > .02);
        const auto loopOutput = [&](bool loop) {
            return render([&](NeedleProcessor &p) { fileDeck(p); p.setParam("loop", loop); p.seek.store(.98); },
                          silence);
        };
        const double stopped = rms(loopOutput(false)[0], 10000), wrapped = rms(loopOutput(true)[0], 10000);
        report("loop", "end-of-file RMS stopped / looping", stopped, wrapped, stopped < .001 && wrapped > .1);
        deckLevel("reverse", 0, 1, neutral, .001);
        deckLevel("cutMode", 1, 0, [](NeedleProcessor &p) { p.setParam("fader", .2f); }, .35);
        const auto renderAlgorithm = [&](int algorithm) {
            return render([&](NeedleProcessor &p) { p.setParam("mode", algorithm); p.setParam("source", 0); },
                          silence, 96, false,
                [](NeedleProcessor &p, juce::MidiBuffer &, int n) {
                    p.keySpeed.store(n % 2 == 0 ? 1 : -1);
                });
        };
        const auto direct = renderAlgorithm(1), tape = renderAlgorithm(2);
        report("mode", "Direct vs Tape scratch stereo null RMS", 0, difference(direct, tape),
               difference(direct, tape) > .02);
        const auto lengthRender = [&](float beats) {
            return render([&](NeedleProcessor &p) { p.setParam("length", beats); }, silence, 96, true, pattern);
        };
        const auto shortPattern = lengthRender(.125f), longPattern = lengthRender(1);
        report("length", "note pattern length .125 vs 1 beat null RMS", 0, difference(shortPattern, longPattern),
               difference(shortPattern, longPattern) > .05);
        const auto hotCue = [&](float base) {
            return render([&](NeedleProcessor &p) { p.setDjParam(dj::cueBase, base); p.hotCues[0].store(.25); },
                silence, 96, true, [](NeedleProcessor &, juce::MidiBuffer &m, int n) {
                    if (n == 1) m.addEvent(juce::MidiMessage::noteOn(1, 80, 1.f), 29);
                });
        };
        const auto unmappedCue = hotCue(72), mappedCue = hotCue(80);
        report("dj.cueBase", "same Note On with different Hot Cue base, RMS",
               rms(unmappedCue[0]), rms(mappedCue[0]), rms(unmappedCue[0]) < .001 && rms(mappedCue[0]) > .1);
        const auto holdMotor = [](bool bypass) {
            auto params = dj::defaults();
            params[dj::motorOn] = 1;
            params[dj::motorBypass] = bypass;
            dj::Motor motor;
            motor.prepare(rate);
            motor.configure(params);
            motor.action(1u << dj::spinAction, 1);
            motor.tick(1, true);
            return motor.tick(1, false);
        };
        const auto continued = holdMotor(false), cancelled = holdMotor(true);
        report("dj.motorBypass", "speed after manual interruption bypass off/on",
               continued, cancelled, continued < -2.9 && cancelled > .99);

        constexpr dj::Param padMappings[]{dj::padEcho, dj::padBrake, dj::padSpin, dj::padHotCue,
                                          dj::padStutter, dj::padFx, dj::padReset};
        for (size_t i = 0; i < std::size(padMappings); ++i)
        {
            std::array<int, 7> assignments;
            assignments.fill(-1);
            assignments[i] = 12;
            std::array<bool, 32> pressed{}, released{};
            pressed[12] = true;
            const auto active = dj::mapFxButtons(pressed, released, assignments);
            const auto held = dj::mapFxButtons(pressed, pressed, assignments);
            const auto up = dj::mapFxButtons(released, pressed, assignments);
            const int observed = i == dj::cueAction ? active.cue : static_cast<int>(active.actions);
            const int expected = i == dj::cueAction ? 1 : 1 << i;
            report(dj::definitions[padMappings[i]].id, "simulated press action observed/expected",
                   observed, expected, observed == expected && held.actions == 0 && held.cue == 0 &&
                   (i != dj::cueAction || up.cue == -1));
        }
        const auto padAudio = [&](float fader) {
            float minimumSpeed = 0;
            const auto audio = render([](NeedleProcessor &p) { p.setParam("fader", 0); }, silence, 96, true,
                [&](NeedleProcessor &p, juce::MidiBuffer &, int n) {
                    if (n > 8)
                        minimumSpeed = std::min(minimumSpeed, p.actualSpeed.load());
                    p.publishPadAxes(true, -2, fader);
                });
            return std::make_pair(minimumSpeed, rms(audio[0]));
        };
        const auto padOpen = padAudio(1), padCut = padAudio(0);
        report("gamepad", "simulated left stick speed and right stick audio",
               padOpen.first, padCut.second, padOpen.first < -1.99 && padOpen.second > .02 && padCut.second < .001);

        const Signal phrase = [](int i) {
            const double t = i / static_cast<double>(rate);
            const double kick = std::fmod(t, .25), hat = std::fmod(t + .0625, .125);
            const float noise = ((static_cast<unsigned>(i * 1u) * 747796405u + 2891336453u) >> 24) / 127.5f - 1;
            return static_cast<float>(.35 * std::exp(-24 * kick) * std::sin(2 * skratch::pi * (52 * kick - 17 * std::exp(-30 * kick)))
               + .10 * noise * std::exp(-45 * hat) + .08 * std::sin(2 * skratch::pi * 333 * t));
        };
        wav(examples, "00-dry", render(neutral, phrase, 384));
        wav(examples, "01-dj-filter", render(control(dj::filter, -.7f, rack), phrase, 384));
        wav(examples, "02-echo", render(control(dj::echoMs, 240, echo), phrase, 384));
        wav(examples, "03-bitcrusher", render(control(dj::bits, 4, crush), phrase, 384));
        wav(examples, "04-drive", render(control(dj::drive, 24, drive), phrase, 384));
        wav(examples, "05-stutter", render(stutter, phrase, 384));
        wav(examples, "06-eq", render(control(dj::high, -16, control(dj::low, 5, rack)), phrase, 384));
        wav(examples, "07-input-strip", render(control(dj::inputHp, 400, control(dj::inputLp, 2500)), phrase, 384));
        wav(examples, "08-limiter", render(control(dj::limiter, 1), clippingInput, 384));
        wav(examples, "08-limiter-dry", render(neutral, clippingInput, 384));
        wav(examples, "09-noise-gate", render(control(dj::noiseThreshold, -16, gate), phrase, 384));
        return failures == 0 ? 0 : 1;
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
