#include "MixerDsp.h"

namespace dj
{
namespace
{
constexpr double twoPi = 6.2831853071795864769;
float mixValue(float from, float to, float fraction) noexcept
{
    return from + (to - from) * fraction;
}
} // namespace

void InputStrip::prepare(double sampleRate) noexcept
{
    rate = sampleRate;
    smooth = static_cast<float>(1 - std::exp(-1 / (rate * .0018)));
    wet = targetWet = hp = hpWet = lpWet = 0;
    gain = lp = envelope = 1;
    lowState = highState = {};
    gateOpen = true;
    configured = false;
}

void InputStrip::configure(const Values &p) noexcept
{
    const std::array<float, 7> values{p[inputGain],      p[inputHp],     p[inputLp],     p[noiseOn],
                                      p[noiseThreshold], p[noiseAttack], p[noiseRelease]};
    if (configured && values == last)
        return;
    configured = true;
    last = values;
    const auto filter = [&](float hz)
    { return static_cast<float>(1 - std::exp(-twoPi * std::min<double>(hz, rate * .44) / rate)); };
    const auto timing = [&](float ms) { return static_cast<float>(1 - std::exp(-1000 / (rate * ms))); };
    const bool boosted = std::abs(p[inputGain]) > .0001f;
    targetGain = boosted ? std::pow(10.0f, p[inputGain] / 20) : 1.0f;
    targetHpWet = p[inputHp] >= 15 ? 1.0f : 0.0f;
    targetLpWet = p[inputLp] < 19999 ? 1.0f : 0.0f;
    targetHp = filter(std::max(15.0f, p[inputHp]));
    targetLp = filter(p[inputLp]);
    gateEnabled = p[noiseOn] > .5f;
    threshold = std::pow(10.0f, p[noiseThreshold] / 20);
    attack = timing(p[noiseAttack]);
    release = timing(p[noiseRelease]);
    targetWet = gateEnabled || targetHpWet > 0 || targetLpWet > 0 || boosted ? 1.0f : 0.0f;
}

void InputStrip::frame(float *samples, int channels) noexcept
{
    wet = mixValue(wet, targetWet, smooth);
    gain = mixValue(gain, targetGain, smooth);
    hp = mixValue(hp, targetHp, smooth);
    lp = mixValue(lp, targetLp, smooth);
    hpWet = mixValue(hpWet, targetHpWet, smooth);
    lpWet = mixValue(lpWet, targetLpWet, smooth);
    float shaped[2]{}, peak = 0;
    for (int c = 0; c < channels; ++c)
    {
        const float x = std::isfinite(samples[c]) ? std::clamp(samples[c], -8.0f, 8.0f) * gain : 0;
        lowState[c] = mixValue(lowState[c], x, hp);
        const float afterHighPass = x - lowState[c] * hpWet;
        highState[c] = mixValue(highState[c], afterHighPass, lp);
        shaped[c] = mixValue(afterHighPass, highState[c], lpWet);
        peak = std::max(peak, std::abs(shaped[c]));
    }
    gateOpen = !gateEnabled || peak >= threshold * (gateOpen ? .72f : 1.0f);
    envelope =
        mixValue(envelope, gateOpen ? 1.0f : 0.0f, !gateEnabled ? smooth : (gateOpen ? attack : release));
    for (int c = 0; c < channels; ++c)
        samples[c] = mixValue(std::isfinite(samples[c]) ? samples[c] : 0, shaped[c] * envelope, wet);
}

void ManualCoast::configure(bool on, float friction) noexcept
{
    if (!on)
    {
        enabled = wasManual = false;
        speed = 0;
        return;
    }
    enabled = true;
    decay = std::exp(-1 / (sampleRate * (.075 + .42 * (1 - friction))));
}

double ManualCoast::apply(double target, double actual, bool manual, bool canCoast) noexcept
{
    if (manual)
    {
        speed = actual;
        wasManual = true;
        return target;
    }
    wasManual = false;
    if (!canCoast)
    {
        speed = 0;
        return target;
    }
    speed *= decay;
    if (std::abs(speed) < .00001)
        speed = 0;
    return speed == 0 ? target : speed;
}

std::pair<float, float> Morph::frame(const Values &p, float originalSpeed, float originalFader, float speedA,
                                     float faderA, float speedB, float faderB) noexcept
{
    amount += (p[morphAmount] - amount) * step;
    blend += (p[morphOn] - blend) * step;
    const float speed = mixValue(originalSpeed, mixValue(speedA, speedB, static_cast<float>(amount)),
                                 static_cast<float>(blend));
    const float fader = mixValue(originalFader, mixValue(faderA, faderB, static_cast<float>(amount)),
                                 static_cast<float>(blend));
    const int a = static_cast<int>(p[morphA]), b = static_cast<int>(p[morphB]);
    if (lastA != a || lastB != b)
    {
        if (lastA >= 0)
        {
            offsetSpeed = lastSpeed - speed;
            offsetFader = lastFader - fader;
        }
        lastA = a;
        lastB = b;
    }
    lastSpeed = std::clamp(speed + offsetSpeed, -6.0f, 6.0f);
    lastFader = std::clamp(fader + offsetFader, 0.0f, 1.0f);
    offsetSpeed *= static_cast<float>(1 - step);
    offsetFader *= static_cast<float>(1 - step);
    return {lastSpeed, lastFader};
}
} // namespace dj
