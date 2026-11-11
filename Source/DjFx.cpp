#include "DjFx.h"

namespace dj
{
namespace
{
constexpr float pi = 3.14159265358979323846f;
float db(float x)
{
    return std::pow(10.0f, x / 20);
}
float lerp(float a, float b, float x)
{
    return a + (b - a) * x;
}
float coefficient(float hz, float rate)
{
    return 1 - std::exp(-2 * pi * hz / rate);
}
} // namespace

void Rack::prepare(double rate)
{
    sampleRate = static_cast<float>(rate);
    delaySize = static_cast<int>(std::ceil(rate * 6.02)) + 4;
    for (auto &channel : delay)
        channel.assign(static_cast<size_t>(delaySize), 0.0f);
    state = {};
    eq = filtering = echo = crusher = distortion = stutter = echoMute = freezeBlend = {};
    delayWrite = delayValid = holdCountdown = delayFade = filterCountdown = 0;
    delayFadeLength = std::max(1, static_cast<int>(rate * .008));
    delayOld = delayNew = 1;
    global = globalTarget = limit = limitTarget = 0;
    gainLow = gainMid = gainHigh = 1;
    gateGain = 1;
    freePhase = gateOrigin = 0;
    gateTriggered = gatePhaseReset = false;
    lowCoefficient = coefficient(260, sampleRate);
    highCoefficient = coefficient(3000, sampleRate);
    smooth = 1 - std::exp(-1.0f / (sampleRate * .0016f));
    gateSmooth = 1 - std::exp(-1.0f / (sampleRate * .0006f));
    filterConfigured = false;
    configured = false;
}

void Rack::configure(const Values &p, double tempo)
{
    if (configured && tempo == lastTempo && p == settings)
        return;
    configured = true;
    lastTempo = tempo;
    settings = p;
    globalTarget = p[enabled] > .5f ? p[mix] : 0;
    limitTarget = p[limiter];
    const bool run = globalTarget > 0;
    eq.target = run && (p[low] != 0 || p[mid] != 0 || p[high] != 0 || p[lowKill] > .5f || p[midKill] > .5f ||
                        p[highKill] > .5f)
                    ? 1.0f
                    : 0.0f;
    if (eq.target > 0)
    {
        targetLow = p[lowKill] > .5f ? 0 : db(p[low]);
        targetMid = p[midKill] > .5f ? 0 : db(p[mid]);
        targetHigh = p[highKill] > .5f ? 0 : db(p[high]);
    }
    const float position = p[filter];
    filtering.target = run && p[filterOn] > .5f ? p[filterMix] * std::min(1.0f, std::abs(position) * 6) : 0;
    if (filtering.target > 0)
    {
        const float amount = std::abs(position);
        const float hz = position < 0 ? 18500 * std::exp2(-8.2f * amount) : 30 * std::exp2(9.0f * amount);
        targetIntegrator = std::tan(pi * std::min(hz, sampleRate * .42f) / sampleRate);
        targetDamping = 1 / (.55f + 2.4f * p[resonance]);
        targetHighPass = position > 0 ? 1.0f : 0.0f;
        poles = p[slope] > .5f ? 2 : 1;
        if (!filterConfigured)
        {
            for (auto &ch : state)
                ch.ic1 = ch.ic2 = {};
            integrator = targetIntegrator;
            k = targetDamping;
            highPass = targetHighPass;
            filterCountdown = 0;
            filterConfigured = true;
        }
    }
    echo.target = run && p[echoOn] > .5f ? p[echoMix] : 0;
    echoMute.target = run && p[echoOn] > .5f && p[echoOut] > .5f ? 1.0f : 0.0f;
    freezeBlend.target = p[freeze];
    if (echo.running() || echoMute.running())
    {
        const double time = p[echoSync] > .5f
                                ? 60 / std::max(20.0, tempo) * echoBeats[static_cast<size_t>(p[echoDivision])]
                                : p[echoMs] * .001;
        const float frames =
            std::clamp(static_cast<float>(time * sampleRate), 1.0f, static_cast<float>(delaySize - 2));
        if (std::abs(frames - delayNew) > .5f)
        {
            delayOld = delayValid == 0
                           ? frames
                           : lerp(delayNew, delayOld, static_cast<float>(delayFade) / delayFadeLength);
            delayNew = frames;
            delayFade = delayFadeLength;
        }
    }
    crusher.target = run && p[crushOn] > .5f ? p[crushMix] : 0;
    if (crusher.target > 0)
    {
        quantum = static_cast<float>(1 << (static_cast<int>(p[bits]) - 1));
        holdLength = static_cast<int>(p[downsample]);
        crushOutput = db(p[crushGain]);
    }
    distortion.target = run && p[driveOn] > .5f ? p[driveMix] : 0;
    if (distortion.target > 0)
    {
        driveAlgorithm = static_cast<int>(p[driveMode]);
        driveFactor = db(p[drive]);
        driveOutput = db(p[driveGain]);
        toneCoefficient = coefficient(800 * std::exp2(4.5f * p[tone]), sampleRate);
    }
    const bool trigger = p[gateFire] > .5f;
    if (trigger && !gateTriggered)
        gatePhaseReset = true;
    gateTriggered = trigger;
    stutter.target = run && p[gateOn] > .5f && (p[gateMode] < .5f || trigger) ? p[gateMix] : 0;
    gatePeriod = gateBeats[static_cast<size_t>(p[gateDivision])];
    if (!active())
    {
        delayValid = 0;
        global = 0;
    }
}

void Rack::frame(float *samples, int channels, double beat, float master) noexcept
{
    global += (globalTarget - global) * smooth;
    limit += (limitTarget - limit) * smooth;
    const bool doEq = eq.running(), doFilter = filtering.running(), doCrush = crusher.running(),
               doDrive = distortion.running(), doGate = stutter.running(),
               doEcho = echo.running() || echoMute.running();
    float eqMix = 0, filterBlend = 0, driveBlend = 0, crushBlend = 0, gate = 1, echoBlend = 0, mute = 0,
          frozen = 0;
    if (doEq)
    {
        eqMix = eq.step(smooth);
        gainLow = lerp(gainLow, targetLow, smooth);
        gainMid = lerp(gainMid, targetMid, smooth);
        gainHigh = lerp(gainHigh, targetHigh, smooth);
    }
    if (doFilter)
    {
        filterBlend = filtering.step(smooth);
        highPass = lerp(highPass, targetHighPass, smooth);
        if (filterCountdown-- <= 0)
        {
            const float interval = std::min(1.0f, 7 * smooth);
            integrator = lerp(integrator, targetIntegrator, interval);
            k = lerp(k, targetDamping, interval);
            a1 = 1 / (1 + integrator * (integrator + k));
            a2 = integrator * a1;
            a3 = integrator * a2;
            filterCountdown = 7;
        }
    }
    else
        filterConfigured = false;
    if (doCrush)
        crushBlend = crusher.step(smooth);
    if (doDrive)
        driveBlend = distortion.step(smooth);
    if (doGate)
    {
        if (gatePhaseReset)
        {
            gateOrigin = beat;
            freePhase = 0;
            gatePhaseReset = false;
        }
        const double cycles = settings[gateSync] > .5f ? (beat - gateOrigin) / gatePeriod : freePhase;
        const float target = cycles - std::floor(cycles) < settings[duty] ? 1 : 1 - settings[depth];
        gateGain = lerp(gateGain, target, gateSmooth);
        gate = lerp(1, gateGain, stutter.step(smooth));
        freePhase += settings[gateRate] / sampleRate;
        if (freePhase >= 1)
            freePhase -= 1;
    }
    if (doEcho)
    {
        echoBlend = echo.step(smooth);
        mute = echoMute.step(smooth);
        frozen = freezeBlend.step(smooth);
    }
    else
        delayValid = 0;

    const auto readDelay = [&](int c, float offset)
    {
        const int d = static_cast<int>(offset);
        if (d > delayValid)
            return 0.0f;
        const int a = delayWrite >= d ? delayWrite - d : delayWrite + delaySize - d;
        const float b = d < delayValid ? delay[c][static_cast<size_t>(a == 0 ? delaySize - 1 : a - 1)] : 0;
        return lerp(delay[c][static_cast<size_t>(a)], b, offset - d);
    };
    for (int c = 0; c < channels; ++c)
    {
        const float dry = samples[c];
        auto &ch = state[c];
        float x = dry;
        if (doEq)
        {
            ch.lo = lerp(ch.lo, x, lowCoefficient);
            ch.hi = lerp(ch.hi, x, highCoefficient);
            const float shaped = ch.lo * gainLow + (ch.hi - ch.lo) * gainMid + (x - ch.hi) * gainHigh;
            x = lerp(x, shaped, eqMix);
        }
        if (doFilter)
        {
            float filtered = x;
            for (int stage = 0; stage < poles; ++stage)
            {
                const float v3 = filtered - ch.ic2[stage];
                const float v1 = a1 * ch.ic1[stage] + a2 * v3;
                const float v2 = ch.ic2[stage] + a2 * ch.ic1[stage] + a3 * v3;
                ch.ic1[stage] = 2 * v1 - ch.ic1[stage];
                ch.ic2[stage] = 2 * v2 - ch.ic2[stage];
                filtered = lerp(v2, filtered - k * v1 - v2, highPass);
            }
            x = lerp(x, std::clamp(filtered, -4.0f, 4.0f), filterBlend);
        }
        if (doCrush)
        {
            if (holdCountdown == 0)
                ch.held = std::round(std::clamp(x, -1.0f, 1.0f) * quantum) / quantum;
            x = lerp(x, std::clamp(ch.held * crushOutput, -1.5f, 1.5f), crushBlend);
        }
        if (doDrive)
        {
            const float y = x * driveFactor;
            const float hard = std::clamp(y, -1.0f, 1.0f);
            const float shaped =
                driveAlgorithm == 1
                    ? hard
                    : (driveAlgorithm == 0 ? hard * (1.5f - .5f * hard * hard) : y / (1 + std::abs(y)));
            ch.filtered = lerp(ch.filtered, shaped, toneCoefficient);
            x = lerp(x, std::clamp(ch.filtered * driveOutput, -1.5f, 1.5f), driveBlend);
        }
        x *= gate;
        if (doEcho)
        {
            float wet = readDelay(c, delayNew);
            if (delayFade > 0)
                wet = lerp(wet, readDelay(c, delayOld), static_cast<float>(delayFade) / delayFadeLength);
            delay[c][static_cast<size_t>(delayWrite)] = std::clamp(
                x * (1 - frozen) * (1 - mute) + wet * lerp(settings[feedback], 1, frozen), -1.5f, 1.5f);
            x = x * (1 - mute) * (1 - echoBlend) + wet * echoBlend;
        }
        x = lerp(dry * (1 - mute), x, global) * master;
        const float magnitude = std::abs(x);
        if (magnitude > .78f)
            x = lerp(x, std::copysign(.78f + .22f * (magnitude - .78f) / (magnitude - .56f), x), limit);
        samples[c] = x;
    }
    if (doCrush)
        holdCountdown = holdCountdown <= 0 ? holdLength - 1 : holdCountdown - 1;
    else
        holdCountdown = 0;
    if (doEcho)
    {
        if (++delayWrite == delaySize)
            delayWrite = 0;
        delayValid = std::min(delayValid + 1, delaySize - 1);
        if (delayFade > 0)
            --delayFade;
        if (!echo.running() && !echoMute.running())
            delayValid = 0;
    }
}

void Motor::configure(const Values &p)
{
    enabled = p[motorOn] > .5f;
    bypass = p[motorBypass] > .5f;
    startStep = p[torque] / std::max(1.0, p[startMs] * .001 * sampleRate);
    brakeStep = 1 / std::max(1.0, p[brakeMs] * .001 * sampleRate);
    spinRate = p[spinSpeed];
    shape = p[acceleration];
    drag = p[friction];
}

void Motor::action(unsigned flags, double currentVelocity)
{
    if ((flags & (1u << spinAction)) != 0)
    {
        mode = 2;
        phase = 0;
        brakeFrom = -spinRate;
    }
    else if ((flags & (1u << brakeAction)) != 0)
    {
        mode = 1;
        phase = 0;
        brakeFrom = currentVelocity;
    }
}

double Motor::tick(double requested, bool manual) noexcept
{
    if (manual && bypass)
    {
        mode = 0;
        previousTarget = requested;
        return velocity = requested;
    }
    if (mode != 0)
    {
        if (mode == 3)
        {
            if (requested != previousTarget)
                mode = 0;
            else
                return manual ? requested : 0;
        }
        else
        {
            phase = std::min(1.0, phase + brakeStep * (mode == 2 ? .7 : 1.0));
            const auto remaining = 1 - phase;
            velocity = brakeFrom * remaining * (1 - drag * phase);
            if (phase >= 1)
                mode = 3;
            previousTarget = requested;
            return manual ? requested : velocity;
        }
    }
    previousTarget = requested;
    if (!enabled)
        return velocity = requested;
    const double difference = requested - velocity;
    const double amount = std::min(1.0, std::abs(difference));
    const double step = requested == 0
                            ? brakeStep
                            : startStep * std::max(.08, 1 + (shape - 1) * (amount - .5)) * (1 - drag * .65);
    velocity += std::clamp(difference, -step, step);
    return manual ? requested : velocity;
}
} // namespace dj
