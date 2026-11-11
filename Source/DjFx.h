#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace dj
{
enum Param : size_t
{
    enabled,
    mix,
    limiter,
    low,
    mid,
    high,
    lowKill,
    midKill,
    highKill,
    filter,
    resonance,
    slope,
    filterOn,
    filterMix,
    echoOn,
    echoMix,
    echoMs,
    feedback,
    echoSync,
    echoDivision,
    freeze,
    echoOut,
    crushOn,
    bits,
    downsample,
    crushMix,
    crushGain,
    driveOn,
    driveMode,
    drive,
    tone,
    driveMix,
    driveGain,
    gateOn,
    gateRate,
    gateDivision,
    duty,
    depth,
    gateSync,
    gateMode,
    gateMix,
    gateFire,
    motorOn,
    torque,
    startMs,
    brakeMs,
    spinSpeed,
    acceleration,
    friction,
    motorBypass,
    brake,
    spin,
    cueMode,
    cueBase,
    padBank,
    padCue,
    padEcho,
    padBrake,
    padSpin,
    padHotCue,
    padStutter,
    padFx,
    padReset,
    inputGain,
    inputHp,
    inputLp,
    noiseOn,
    noiseThreshold,
    noiseAttack,
    noiseRelease,
    morphOn,
    morphA,
    morphB,
    morphAmount,
    coastOn,
    count
};
struct Definition
{
    const char *id, *name;
    float minimum, maximum, initial, step;
};
inline constexpr std::array<Definition, count> definitions{
    {{"dj.enabled", "FX enable", 0, 1, 0, 1},
     {"dj.mix", "FX dry/wet", 0, 1, 1, 0},
     {"dj.limiter", "Limiter", 0, 1, 0, 1},
     {"dj.low", "Low EQ dB", -18, 12, 0, 0},
     {"dj.mid", "Mid EQ dB", -18, 12, 0, 0},
     {"dj.high", "High EQ dB", -18, 12, 0, 0},
     {"dj.lowKill", "Low kill", 0, 1, 0, 1},
     {"dj.midKill", "Mid kill", 0, 1, 0, 1},
     {"dj.highKill", "High kill", 0, 1, 0, 1},
     {"dj.filter", "LP / HP", -1, 1, 0, 0},
     {"dj.resonance", "Filter resonance", 0, 1, .2f, 0},
     {"dj.slope", "Filter slope", 0, 1, 0, 1},
     {"dj.filterOn", "Filter enable", 0, 1, 1, 1},
     {"dj.filterMix", "Filter dry/wet", 0, 1, 1, 0},
     {"dj.echoOn", "Echo enable", 0, 1, 0, 1},
     {"dj.echoMix", "Echo dry/wet", 0, 1, .35f, 0},
     {"dj.echoMs", "Echo time ms", 10, 2000, 375, 0},
     {"dj.feedback", "Echo feedback", 0, .94f, .45f, 0},
     {"dj.echoSync", "Echo sync", 0, 1, 1, 1},
     {"dj.echoDivision", "Echo division", 0, 9, 3, 1},
     {"dj.freeze", "Echo freeze", 0, 1, 0, 1},
     {"dj.echoOut", "Echo out", 0, 1, 0, 1},
     {"dj.crushOn", "Crusher enable", 0, 1, 0, 1},
     {"dj.bits", "Bit depth", 2, 16, 8, 1},
     {"dj.downsample", "Rate reduction", 1, 48, 8, 1},
     {"dj.crushMix", "Crusher dry/wet", 0, 1, .4f, 0},
     {"dj.crushGain", "Crusher output dB", -24, 6, 0, 0},
     {"dj.driveOn", "Drive enable", 0, 1, 0, 1},
     {"dj.driveMode", "Drive mode", 0, 2, 0, 1},
     {"dj.drive", "Drive dB", 0, 30, 9, 0},
     {"dj.tone", "Drive tone", 0, 1, .65f, 0},
     {"dj.driveMix", "Drive dry/wet", 0, 1, .5f, 0},
     {"dj.driveGain", "Drive output dB", -24, 6, -6, 0},
     {"dj.gateOn", "Stutter enable", 0, 1, 0, 1},
     {"dj.gateRate", "Stutter rate Hz", .5f, 40, 8, 0},
     {"dj.gateDivision", "Stutter division", 0, 7, 2, 1},
     {"dj.duty", "Stutter duty", .05f, .95f, .5f, 0},
     {"dj.depth", "Stutter depth", 0, 1, 1, 0},
     {"dj.gateSync", "Stutter sync", 0, 1, 1, 1},
     {"dj.gateMode", "Stutter trigger mode", 0, 1, 0, 1},
     {"dj.gateMix", "Stutter dry/wet", 0, 1, 1, 0},
     {"dj.gateFire", "Stutter trigger", 0, 1, 0, 1},
     {"dj.motorOn", "Motor enable", 0, 1, 0, 1},
     {"dj.torque", "Motor torque", .25f, 4, 1, 0},
     {"dj.startMs", "Motor start ms", 5, 1600, 160, 0},
     {"dj.brakeMs", "Motor brake ms", 10, 2400, 440, 0},
     {"dj.spinSpeed", "Spinback speed", .25f, 8, 3, 0},
     {"dj.acceleration", "Motor curve", .4f, 3, 1, 0},
     {"dj.friction", "Motor friction", 0, 1, .22f, 0},
     {"dj.motorBypass", "Manual motor bypass", 0, 1, 1, 1},
     {"dj.brake", "Vinyl brake trigger", 0, 1, 0, 1},
     {"dj.spin", "Spinback trigger", 0, 1, 0, 1},
     {"dj.cueMode", "Hot Cue mode", 0, 2, 0, 1},
     {"dj.cueBase", "Hot Cue first MIDI note", 0, 120, 72, 1},
     {"dj.padBank", "Gamepad FX button bank", 0, 1, 0, 1},
     {"dj.padCue", "Gamepad Hot Cue slot", 1, 8, 1, 1},
     {"dj.padEcho", "Pad Echo Out button", -1, 31, 0, 1},
     {"dj.padBrake", "Pad Brake button", -1, 31, 1, 1},
     {"dj.padSpin", "Pad Spinback button", -1, 31, 2, 1},
     {"dj.padHotCue", "Pad Hot Cue button", -1, 31, 3, 1},
     {"dj.padStutter", "Pad Stutter button", -1, 31, 4, 1},
     {"dj.padFx", "Pad FX Toggle button", -1, 31, 5, 1},
     {"dj.padReset", "Pad Filter Reset button", -1, 31, 6, 1},
     {"dj.inputGain", "Input gain dB", -24, 18, 0, 0},
     {"dj.inputHp", "Input high-pass Hz", 0, 1500, 0, 0},
     {"dj.inputLp", "Input low-pass Hz", 600, 20000, 20000, 0},
     {"dj.noiseOn", "Noise Gate enable", 0, 1, 0, 1},
     {"dj.noiseThreshold", "Noise Gate threshold dB", -80, -6, -48, 0},
     {"dj.noiseAttack", "Noise Gate attack ms", .2f, 15, 1.0f, 0},
     {"dj.noiseRelease", "Noise Gate release ms", 6, 250, 65, 0},
     {"dj.morphOn", "Pattern Morph enable", 0, 1, 0, 1},
     {"dj.morphA", "Pattern Morph A", 0, 15, 0, 1},
     {"dj.morphB", "Pattern Morph B", 0, 15, 3, 1},
     {"dj.morphAmount", "Pattern Morph amount", 0, 1, 0, 0},
     {"dj.coastOn", "Manual vinyl coast", 0, 1, 0, 1}}};

using Values = std::array<float, count>;
inline Values defaults()
{
    Values result{};
    for (size_t i = 0; i < count; ++i)
        result[i] = definitions[i].initial;
    return result;
}
enum Action : unsigned
{
    echoAction,
    brakeAction,
    spinAction,
    cueAction,
    stutterAction,
    fxAction,
    resetFilterAction
};
struct ButtonEdges
{
    unsigned actions = 0;
    int cue = 0;
};

inline ButtonEdges mapFxButtons(const std::array<bool, 32> &current, const std::array<bool, 32> &previous,
                                const std::array<int, 7> &indices) noexcept
{
    ButtonEdges result;
    for (size_t action = 0; action < indices.size(); ++action)
    {
        const int index = indices[action];
        if (index < 0 || index >= static_cast<int>(current.size()) || current[index] == previous[index])
            continue;
        if (action == cueAction)
            result.cue = current[index] ? 1 : -1;
        else if (current[index])
            result.actions |= 1u << action;
    }
    return result;
}
inline constexpr int learnBase = 32;
inline constexpr std::array<double, 10> echoBeats{4,       2,       1,       .5,      .25,
                                                  8.0 / 3, 4.0 / 3, 2.0 / 3, 1.0 / 3, 1.0 / 6};
inline constexpr std::array<double, 8> gateBeats{1, .5, .25, .125, 2.0 / 3, 1.0 / 3, 1.0 / 6, 1.0 / 12};

class Rack
{
  public:
    void prepare(double sampleRate);
    void configure(const Values &, double tempo);
    bool active() const noexcept
    {
        return eq.running() || filtering.running() || echo.running() || echoMute.running() ||
               crusher.running() || distortion.running() || stutter.running();
    }
    void frame(float *samples, int channels, double beat, float master = 1.0f) noexcept;
    bool limiterActive() const noexcept { return limitTarget > 0 || limit > 0.00001f; }
    unsigned activeModules() const noexcept
    {
        return static_cast<unsigned>(eq.running()) |
               (static_cast<unsigned>(filtering.running()) << 1) |
               (static_cast<unsigned>(echo.running() || echoMute.running()) << 2) |
               (static_cast<unsigned>(crusher.running()) << 3) |
               (static_cast<unsigned>(distortion.running()) << 4) |
               (static_cast<unsigned>(stutter.running()) << 5) |
               (static_cast<unsigned>(limiterActive()) << 6);
    }

  private:
    struct Channel
    {
        float lo = 0, hi = 0, filtered = 0, held = 0;
        std::array<float, 2> ic1{}, ic2{};
    };
    struct Blend
    {
        float value = 0, target = 0;
        float step(float coefficient) noexcept
        {
            return value += (target - value) * coefficient;
        }
        bool running() const noexcept
        {
            return target > 0 || value > 0.00001f;
        }
    };
    std::array<Channel, 2> state{};
    std::array<std::vector<float>, 2> delay;
    Values settings{};
    Blend eq, filtering, echo, crusher, distortion, stutter, echoMute, freezeBlend;
    float sampleRate = 48000, smooth = .015f, gateSmooth = .025f, global = 0, globalTarget = 0, limit = 0,
          limitTarget = 0;
    float lowCoefficient = 0, highCoefficient = 0, toneCoefficient = 0, gateGain = 1;
    float a1 = 1, a2 = 0, a3 = 0, k = 1, highPass = 0, driveFactor = 1, driveOutput = 1, crushOutput = 1;
    float integrator = 0, targetIntegrator = 0, targetDamping = 1, targetHighPass = 0;
    float gainLow = 1, gainMid = 1, gainHigh = 1, targetLow = 1, targetMid = 1, targetHigh = 1;
    float quantum = 128;
    int delayWrite = 0, delayValid = 0, delaySize = 0, holdCountdown = 0, delayFade = 0, delayFadeLength = 1;
    int holdLength = 1, driveAlgorithm = 0, poles = 1;
    int filterCountdown = 0;
    float delayOld = 1, delayNew = 1;
    double freePhase = 0, gatePeriod = 1;
    bool filterConfigured = false, gateTriggered = false, gatePhaseReset = false;
    double gateOrigin = 0;
    double lastTempo = 0;
    bool configured = false;
};

class Motor
{
  public:
    void prepare(double rate)
    {
        sampleRate = rate;
        velocity = 0;
        phase = 0;
        mode = 0;
    }
    void configure(const Values &);
    void action(unsigned flags, double currentVelocity);
    void resume() noexcept
    {
        mode = 0;
    }
    double tick(double requested, bool manual) noexcept;
    bool active() const noexcept
    {
        return enabled || mode != 0;
    }
    bool interrupting() const noexcept
    {
        return mode != 0;
    }

  private:
    double sampleRate = 48000, velocity = 0, phase = 0, brakeFrom = 0, previousTarget = 0;
    double startStep = .001, brakeStep = .001, spinRate = 3, shape = 1, drag = .22;
    int mode = 0;
    bool enabled = false, bypass = true;
};
} // namespace dj
