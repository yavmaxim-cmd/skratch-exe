#pragma once
#include "DjFx.h"
#include <utility>

namespace dj
{
class InputStrip
{
  public:
    void prepare(double rate) noexcept;
    void configure(const Values &) noexcept;
    bool active() const noexcept
    {
        return targetWet > 0 || wet > 0.00001f;
    }
    void frame(float *samples, int channels) noexcept;

  private:
    double rate = 48000;
    float smooth = 0, wet = 0, targetWet = 0, gain = 1, targetGain = 1;
    float hp = 0, lp = 1, targetHp = 0, targetLp = 1, hpWet = 0, lpWet = 0, targetHpWet = 0, targetLpWet = 0;
    float threshold = .01f, attack = .01f, release = .001f, envelope = 1;
    bool gateEnabled = false, gateOpen = true;
    std::array<float, 2> lowState{}, highState{};
    std::array<float, 7> last{};
    bool configured = false;
};

class ManualCoast
{
  public:
    void prepare(double rate) noexcept
    {
        sampleRate = rate;
        speed = 0;
        wasManual = enabled = false;
    }
    void configure(bool on, float friction) noexcept;
    void cancel() noexcept
    {
        wasManual = false;
        speed = 0;
    }
    bool active() const noexcept
    {
        return enabled;
    }
    bool isMoving() const noexcept
    {
        return !wasManual && std::abs(speed) > .00001;
    }
    double apply(double target, double actual, bool manual, bool canCoast) noexcept;

  private:
    double sampleRate = 48000, speed = 0, decay = 0;
    bool wasManual = false, enabled = false;
};

class Morph
{
  public:
    void prepare(double rate) noexcept
    {
        step = 1 - std::exp(-1 / (rate * .0018));
        blend = amount = 0;
        lastA = lastB = -1;
        offsetSpeed = offsetFader = 0;
    }
    bool active(const Values &p) const noexcept
    {
        return p[morphOn] > .5f || blend > .00001;
    }
    std::pair<float, float> frame(const Values &, float originalSpeed, float originalFader, float speedA,
                                  float faderA, float speedB, float faderB) noexcept;

  private:
    double step = .012, blend = 0, amount = 0;
    int lastA = -1, lastB = -1;
    float lastSpeed = 0, lastFader = 1, offsetSpeed = 0, offsetFader = 0;
};
} // namespace dj
