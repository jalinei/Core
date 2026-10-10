#ifndef OWNTECH_TASK_JITTER_COMPARISON_H_
#define OWNTECH_TASK_JITTER_COMPARISON_H_

#include "task_jitter_monitor.h"

struct TaskJitterEvent {
    uint32_t invocation;
    uint32_t dispatch_cycles;
    int64_t isr_jitter_cycles;
    int64_t control_jitter_cycles;
};

struct TaskJitterComparisonStats {
    static constexpr uint32_t event_capacity = 1024;
    uint32_t samples;
    uint32_t min_dispatch_cycles;
    uint32_t max_dispatch_cycles;
    uint64_t dispatch_sum_cycles;
    uint32_t both_abnormal;
    uint32_t isr_only_abnormal;
    uint32_t control_only_abnormal;
    uint32_t recorded_events;
    uint32_t dropped_events;
    TaskJitterEvent events[event_capacity];
};

/** Paired measurements. Accumulation occurs after both timestamps have been
 * taken, keeping histogram bookkeeping out of the ISR-to-callback delay.
 */
class TaskJitterComparison {
public:
    TaskJitterMonitor isr;
    TaskJitterComparisonStats stats = {};

    void configure(uint32_t hz, uint32_t period_us) {
        isr.configure(hz, period_us);
        stats = {};
        expected_cycles = (static_cast<uint64_t>(hz) * period_us + 500000) / 1000000;
        threshold_cycles = (4ULL * hz) / 1000000;
        previous_valid = false;
    }

    void restart() {
        isr.restart();
        previous_valid = false;
    }

    void entry(uint32_t isr_stamp, uint32_t callback_stamp) {
        if (isr.complete()) return;
        const uint32_t delay = callback_stamp - isr_stamp;
        if (stats.samples == 0 || delay < stats.min_dispatch_cycles)
            stats.min_dispatch_cycles = delay;
        if (delay > stats.max_dispatch_cycles) stats.max_dispatch_cycles = delay;
        stats.dispatch_sum_cycles += delay;
        if (previous_valid) {
            const int64_t ij = static_cast<int64_t>(static_cast<uint32_t>(isr_stamp - previous_isr)) - expected_cycles;
            const int64_t cj = static_cast<int64_t>(static_cast<uint32_t>(callback_stamp - previous_callback)) - expected_cycles;
            const bool ia = static_cast<uint64_t>(ij < 0 ? -ij : ij) > threshold_cycles;
            const bool ca = static_cast<uint64_t>(cj < 0 ? -cj : cj) > threshold_cycles;
            if (ia && ca) ++stats.both_abnormal;
            else if (ia) ++stats.isr_only_abnormal;
            else if (ca) ++stats.control_only_abnormal;
            if (ia || ca) {
                if (stats.recorded_events < TaskJitterComparisonStats::event_capacity) {
                    stats.events[stats.recorded_events++] = {stats.samples + 1, delay, ij, cj};
                } else ++stats.dropped_events;
            }
        }
        previous_isr = isr_stamp;
        previous_callback = callback_stamp;
        previous_valid = true;
        ++stats.samples;
        isr.entry(isr_stamp);
        // Only ISR-entry period statistics are used; no fake execution time.
    }

    void exit(uint32_t isr_stamp, uint32_t end) { isr.exit(isr_stamp, end); }

private:
    int64_t expected_cycles = 0;
    uint64_t threshold_cycles = 0;
    uint32_t previous_isr = 0;
    uint32_t previous_callback = 0;
    bool previous_valid = false;
};

#endif
