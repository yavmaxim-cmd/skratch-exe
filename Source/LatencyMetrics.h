#pragma once
#include <atomic>
#include <cstdint>

struct LatencySeries
{
    std::atomic<int64_t> samples{0}, totalTicks{0}, maxTicks{0};

    void add(int64_t ticks) noexcept
    {
        if (ticks < 0)
            return;
        samples.fetch_add(1, std::memory_order_relaxed);
        totalTicks.fetch_add(ticks, std::memory_order_relaxed);
        auto maximum = maxTicks.load(std::memory_order_relaxed);
        // Each series has a single writer.
        if (ticks > maximum)
            maxTicks.store(ticks, std::memory_order_relaxed);
    }
};

struct LatencyMetrics
{
    std::atomic<bool> enabled{false};
    std::atomic<int64_t> mouseTick{0}, padTick{0};
    std::atomic<int64_t> lateBlocks{0}, takeContention{0};
    LatencySeries mouseHandler, mouseToPaint, mouseToDsp, padToDsp, block, platterPaint;
};
