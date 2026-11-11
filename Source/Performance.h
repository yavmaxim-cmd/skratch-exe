#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace skratch
{
constexpr int patternCount = 16, nodes = 65, maxPoints = 120000, maxEvents = 8192;
constexpr double pi = 3.14159265358979323846;
struct Point
{
    double beat = 0;
    float value = 0;
};
struct Event
{
    double beat = 0;
    int type = 0;
    double value = 0;
    double duration = 0;
    float velocity = 1;
};
enum EventType
{
    Seek,
    Transport,
    Cue,
    PatternNote,
    Tempo,
    Cut
};
struct Take
{
    std::vector<Point> platter, fader;
    std::vector<Event> events;
    double length = 4, bpm = 120;
    bool patternPriority = false;
    void reserve()
    {
        platter.reserve(maxPoints);
        fader.reserve(maxPoints);
        events.reserve(maxEvents);
    }
    void clear()
    {
        platter.clear();
        fader.clear();
        events.clear();
        length = 0;
    }
};
inline float curve(const std::vector<Point> &points, double beat, size_t &cursor, float fallback)
{
    if (points.empty())
        return fallback;
    cursor = std::min(cursor, points.size() - 1);
    while (cursor + 1 < points.size() && points[cursor + 1].beat <= beat)
        ++cursor;
    while (cursor > 0 && points[cursor].beat > beat)
        --cursor;
    if (beat < points.front().beat)
        return points.front().value;
    if (cursor + 1 == points.size())
        return points.back().value;
    const auto &a = points[cursor];
    const auto &b = points[cursor + 1];
    const float t =
        static_cast<float>(std::clamp((beat - a.beat) / std::max(1e-9, b.beat - a.beat), 0.0, 1.0));
    return a.value + (b.value - a.value) * t;
}
struct Pattern
{
    std::array<float, nodes> platter{}, fader{};
    double beats = 1;
    float sample(const std::array<float, nodes> &lane, double phase) const
    {
        const double p = std::clamp(phase, 0.0, 1.0) * (nodes - 1);
        const int i = std::min(nodes - 2, static_cast<int>(p));
        return lane[i] + (lane[i + 1] - lane[i]) * static_cast<float>(p - i);
    }
};
inline const char *patternName(int i)
{
    static const char *names[] = {
        "Baby",  "Forward",   "Backward", "Chirp",        "Transformer", "Stab",     "Crab",     "Flare",
        "Orbit", "Tape stop", "Reverse",  "Scratch stop", "Custom 1",    "Custom 2", "Custom 3", "Custom 4"};
    return names[std::clamp(i, 0, 15)];
}
inline Pattern makePattern(int id)
{
    Pattern p;
    for (int n = 0; n < nodes; ++n)
    {
        const float t = n / static_cast<float>(nodes - 1);
        float s = std::sin(t * 2 * pi) * 2, f = 1;
        switch (id)
        {
        case 1:
            s = 2 * std::sin(t * pi);
            break;
        case 2:
            s = -2 * std::sin(t * pi);
            break;
        case 3:
            f = (t < 0.08f || (t > 0.43f && t < 0.57f) || t > 0.92f) ? 0 : 1;
            break;
        case 4:
            f = std::fmod(t * 8, 1.0f) < 0.45f ? 0 : 1;
            break;
        case 5:
            s = 2 * (1 - t);
            f = t < 0.55f ? 1 : 0;
            break;
        case 6:
            f = std::fmod(t * 12, 1.0f) < 0.5f ? 0 : 1;
            break;
        case 7:
            f = (t > 0.2f && t < 0.28f) || (t > 0.7f && t < 0.78f) ? 0 : 1;
            break;
        case 8:
            s = 2.5f * std::sin(t * 4 * pi);
            f = std::fmod(t * 8, 1.0f) < 0.25f ? 0 : 1;
            break;
        case 9:
            s = (1 - t) * (1 - t);
            break;
        case 10:
            s = -1;
            break;
        case 11:
            s = 0;
            break;
        default:
            break;
        }
        p.platter[n] = s;
        p.fader[n] = f;
    }
    return p;
}
struct AxisCalibration
{
    float minimum = -1, center = 0, maximum = 1;
    float normalized(float raw) const
    {
        const float span = raw >= center ? maximum - center : center - minimum;
        return std::clamp((raw - center) / std::max(0.001f, span), -1.0f, 1.0f);
    }
};
struct PadConfig
{
    int device = -1, platterAxis = 1, faderAxis = 2, slowAxis = 4, fastAxis = 5;
    float deadzone = 0.12f, sensitivity = 1, curve = 1.5f, maxSpeed = 4, cutIn = 0.04f;
    bool invertPlatter = true, invertFader = false, binary = false, enabled = false;
    std::array<AxisCalibration, 8> calibration{};
    // Cue, Stop, Next cue, Record, Previous/Next sample, Previous/Next preset.
    std::array<int, 8> buttons{{0, 1, 2, 3, 4, 5, 6, 7}};
};
struct PadState
{
    int id = -1, axisCount = 0, buttonCount = 0;
    bool connected = false, standard = false;
    unsigned availableAxes = 0xff;
    std::array<float, 8> axes{};
    std::array<bool, 32> buttons{};
};
inline float deadzone(float x, float dead)
{
    if (!std::isfinite(x))
        return 0;
    dead = std::clamp(dead, 0.0f, 0.95f);
    return std::abs(x) <= dead ? 0.0f : std::copysign((std::abs(x) - dead) / (1 - dead), x);
}
struct PadOutput
{
    bool active = false;
    float speed = 0, fader = 1;
};
inline PadOutput mapPad(const PadState &pad, const PadConfig &cfg)
{
    if (!cfg.enabled || !pad.connected || cfg.platterAxis < 0 || cfg.faderAxis < 0 ||
        cfg.platterAxis >= pad.axisCount || cfg.faderAxis >= pad.axisCount ||
        (pad.availableAxes & (1u << cfg.platterAxis)) == 0 ||
        (pad.availableAxes & (1u << cfg.faderAxis)) == 0)
        return {};
    float x = cfg.calibration[cfg.platterAxis].normalized(pad.axes[cfg.platterAxis]);
    x = deadzone(x, cfg.deadzone) * (cfg.invertPlatter ? -1 : 1);
    float speed = std::copysign(std::pow(std::abs(x), std::clamp(cfg.curve, 0.2f, 4.0f)), x) * cfg.maxSpeed *
                  cfg.sensitivity;
    const auto trigger = [&](int a)
    {
        return a >= 0 && a < pad.axisCount && (pad.availableAxes & (1u << a))
                   ? std::clamp((pad.axes[a] + 1) * 0.5f, 0.0f, 1.0f)
                   : 0.0f;
    };
    speed *= std::pow(2.0f, trigger(cfg.fastAxis) - trigger(cfg.slowAxis));
    float f = cfg.calibration[cfg.faderAxis].normalized(pad.axes[cfg.faderAxis]) * (cfg.invertFader ? -1 : 1);
    f = (f + 1) * 0.5f;
    if (cfg.binary)
        f = f > std::clamp(cfg.cutIn, 0.0f, 1.0f) ? 1 : 0;
    return {true, std::clamp(speed, -12.0f, 12.0f), std::clamp(f, 0.0f, 1.0f)};
}
} // namespace skratch
