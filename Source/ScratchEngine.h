#pragma once
#include <algorithm>
#include <cmath>
#include <array>

namespace needle
{
inline double wrap(double p, int length) noexcept
{
    if (length <= 0 || !std::isfinite(p))
        return 0.0;
    p = std::fmod(p, static_cast<double>(length));
    return p < 0.0 ? p + length : p;
}

inline float readCubic(const float *data, int length, double position) noexcept
{
    if (length <= 0)
        return 0.0f;
    position = wrap(position, length);
    const int i = static_cast<int>(position);
    const float t = static_cast<float>(position - i);
    const auto at = [=](int n) { return data[(n % length + length) % length]; };
    const auto a = at(i - 1), b = at(i), c = at(i + 1), d = at(i + 2);
    return b + 0.5f * t * (c - a + t * (2 * a - 5 * b + 4 * c - d + t * (3 * (b - c) + d - a)));
}

struct Engine
{
    double position = 0.0, velocity = 0.0;
    float deckGain = 0.0f, inputGain = 0.0f;
    double outputRate = 48000.0;
    double speedRamp = 0.0069;
    float gainRamp = 0.0138f, cutIn = 0.04f, curveExponent = 1;
    float previousX = -1, targetDeck = 0, targetInput = 0;
    bool previousGate = false, previousMode = false;
    int mode = 0;
    void configure(int algorithm, float responseMs, float edge, float exponent, bool lowLatency = false)
    {
        mode = std::clamp(algorithm, 0, 3);
        if (edge != cutIn || exponent != curveExponent)
            previousX = -1;
        cutIn = std::clamp(edge, 0.001f, 0.4f);
        curveExponent = std::clamp(exponent, 0.15f, 4.0f);
        double ms = mode == 0 ? 3.0 : (mode == 1 ? 0.35 : (mode == 2 ? 25.0 : 0.1));
        if (lowLatency)
        {
            ms = std::min(ms, 0.35);
            responseMs = std::min(responseMs, 0.45f);
        }
        speedRamp = 1 - std::exp(-1.0 / (outputRate * ms * 0.001));
        gainRamp = static_cast<float>(1 - std::exp(-1.0 / (outputRate * std::max(0.1f, responseMs) * 0.001)));
    }
    void prepare(double rate)
    {
        outputRate = std::max(1.0, rate);
        velocity = 0.0;
        deckGain = inputGain = 0.0f;
    }
    void reset()
    {
        position = 0.0;
        velocity = 0.0;
    }
    void tick(double targetVelocity, float fader, bool reverse, bool cutMode, bool closed)
    {
        targetVelocity = std::clamp(targetVelocity, -12.0, 12.0);
        if (mode == 3)
            targetVelocity = std::round(targetVelocity * 32) / 32;
        velocity += (targetVelocity - velocity) * speedRamp;
        const float x = std::clamp(reverse ? 1.0f - fader : fader, 0.0f, 1.0f);
        if (x != previousX || cutMode != previousMode || closed != previousGate)
        {
            previousX = x;
            previousMode = cutMode;
            previousGate = closed;
            if (cutMode)
            {
                targetDeck = !closed && x > cutIn ? 1.0f : 0.0f;
                targetInput = 1.0f;
            }
            else
            {
                const float angle = std::pow(x, curveExponent) * 1.57079632679f;
                targetDeck = closed ? 0.0f : std::sin(angle);
                targetInput = std::cos(angle);
            }
        }
        deckGain += (targetDeck - deckGain) * gainRamp;
        inputGain += (targetInput - inputGain) * gainRamp;
    }
};

// Polyphase windowed-sinc bank; all expensive coefficient calculation happens
// during construction, never in processBlock. 12 conservative cutoff banks.
struct SincReader
{
    static constexpr int banks = 12, phases = 256, taps = 16;
    std::array<float, banks * phases * taps> table{};
    SincReader()
    {
        constexpr double pi = 3.14159265358979323846;
        for (int bank = 0; bank < banks; ++bank)
            for (int phase = 0; phase < phases; ++phase)
            {
                const double cutoff = 1.0 / (bank + 1), fraction = phase / static_cast<double>(phases);
                double sum = 0;
                for (int tap = 0; tap < taps; ++tap)
                {
                    const double x = tap - 7 - fraction, z = pi * x * cutoff;
                    const double sinc = std::abs(z) < 1e-9 ? 1 : std::sin(z) / z;
                    const double window = 0.5 + 0.5 * std::cos(pi * x / 8);
                    const float v = static_cast<float>(sinc * cutoff * window);
                    table[(bank * phases + phase) * taps + tap] = v;
                    sum += v;
                }
                for (int tap = 0; tap < taps; ++tap)
                    table[(bank * phases + phase) * taps + tap] /= static_cast<float>(sum);
            }
    }
    float read(const float *data, int length, double position, double increment, bool loop) const noexcept
    {
        if (length < 1)
            return 0;
        if (loop && (position < 0 || position >= length))
            position = wrap(position, length);
        const int integer = static_cast<int>(std::floor(position));
        const double fraction = position - integer;
        const int phase = std::clamp(static_cast<int>(fraction * phases), 0, phases - 1);
        const double speed = std::clamp(std::abs(increment), 1.0, static_cast<double>(banks));
        const int bank = std::clamp(static_cast<int>(std::floor(speed)) - 1, 0, banks - 1),
                  next = std::min(banks - 1, bank + 1);
        const float blend = static_cast<float>(speed - std::floor(speed));
        const float *coefficients = &table[(bank * phases + phase) * taps];
        const float *adjacent = &table[(next * phases + phase) * taps];
        float sum = 0;
        if (integer >= 7 && integer < length - 8)
        {
            const auto *samples = data + integer - 7;
            for (int tap = 0; tap < taps; ++tap)
                sum += samples[tap] * (coefficients[tap] + (adjacent[tap] - coefficients[tap]) * blend);
        }
        else
        {
            for (int tap = 0; tap < taps; ++tap)
            {
                int index = integer + tap - 7;
                if (loop)
                    index = length < taps
                                ? (index % length + length) % length
                                : (index < 0 ? index + length : (index >= length ? index - length : index));
                else if (index < 0 || index >= length)
                    continue;
                sum += data[index] * (coefficients[tap] + (adjacent[tap] - coefficients[tap]) * blend);
            }
        }
        return sum;
    }
};
} // namespace needle
