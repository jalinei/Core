#ifndef OWNTECH_TASK_JITTER_MONITOR_H_
#define OWNTECH_TASK_JITTER_MONITOR_H_

#include "TaskJitterStats.h"

/** Single writer (control ISR). A restart discards the previous timestamp,
 * but preserves the statistics collected so far. No kernel calls required.
 */
class TaskJitterMonitor {
public:
    static constexpr uint32_t sample_count = 1000000;

    void configure(uint32_t cpu_hz, uint32_t period_us) {
        stats = {};
        stats.cpu_hz = cpu_hz;
        stats.period_us = period_us;
        // Approximately 0.25 us per bin, rounded to whole CPU cycles.
        stats.histogram_width_cycles = (static_cast<uint64_t>(cpu_hz) + 2000000) / 4000000;
        if (stats.histogram_width_cycles == 0) stats.histogram_width_cycles = 1;
        elapsed_cycles = 0;
        previous_abnormal_cycles = 0;
        abnormal_valid = false;
        episode_valid = false;
        previous_interval_abnormal = false;
        previous_episode_cycles = 0;
        expected_cycles =
            (static_cast<uint64_t>(cpu_hz) * period_us + 500000) / 1000000;
        tolerance_cycles = (4ULL * cpu_hz) / 1000000;
        previous_valid = false;
    }

    void restart() {
        previous_valid = false;
        // Do not invent an event gap across a stopped interval.
        abnormal_valid = false;
        episode_valid = false;
        previous_interval_abnormal = false;
    }

    void entry(uint32_t now) {
        if (complete()) return;
        if (previous_valid) {
            // Unsigned subtraction supports one CYCCNT wrap between entries.
            const uint32_t interval = now - previous;
            elapsed_cycles += interval;
            const int64_t jitter = static_cast<int64_t>(interval) - expected_cycles;
            const uint64_t magnitude = jitter < 0 ? -jitter : jitter;
            if (stats.intervals == 0 || jitter < stats.min_jitter_cycles)
                stats.min_jitter_cycles = jitter;
            if (stats.intervals == 0 || jitter > stats.max_jitter_cycles)
                stats.max_jitter_cycles = jitter;
            stats.jitter_sum_cycles += jitter;
            stats.absolute_jitter_sum_cycles += magnitude;
            // Strictly greater than 4 us, without rounding the threshold.
            const uint32_t width = stats.histogram_width_cycles;
            const int64_t shifted = jitter + 80LL * width + width / 2;
            if (shifted < 0) ++stats.histogram_underflow;
            else if (shifted >= static_cast<int64_t>(TaskJitterStats::histogram_bins) * width)
                ++stats.histogram_overflow;
            else ++stats.histogram[static_cast<uint32_t>(shifted) / width];

            if (magnitude > tolerance_cycles) {
                // A late/early compensation pair forms one contiguous episode.
                if (!previous_interval_abnormal) {
                    if (episode_valid) {
                        const uint64_t gap = elapsed_cycles - previous_episode_cycles;
                        if (stats.episode_gap_count == 0 || gap < stats.min_episode_gap_cycles)
                            stats.min_episode_gap_cycles = gap;
                        if (gap > stats.max_episode_gap_cycles)
                            stats.max_episode_gap_cycles = gap;
                        stats.episode_gap_sum_cycles += gap;
                        ++stats.episode_gap_count;
                    }
                    previous_episode_cycles = elapsed_cycles;
                    episode_valid = true;
                    ++stats.abnormal_episodes;
                }
                if (abnormal_valid) {
                    const uint64_t gap = elapsed_cycles - previous_abnormal_cycles;
                    if (stats.abnormal_gap_count == 0 || gap < stats.min_abnormal_gap_cycles)
                        stats.min_abnormal_gap_cycles = gap;
                    if (gap > stats.max_abnormal_gap_cycles)
                        stats.max_abnormal_gap_cycles = gap;
                    stats.abnormal_gap_sum_cycles += gap;
                    ++stats.abnormal_gap_count;
                }
                previous_abnormal_cycles = elapsed_cycles;
                abnormal_valid = true;
                ++stats.over_4us;
            }
            previous_interval_abnormal = magnitude > tolerance_cycles;
            ++stats.intervals;
        }
        previous = now;
        previous_valid = true;
    }

    void exit(uint32_t start, uint32_t end) {
        if (complete()) return;
        const uint32_t elapsed = end - start;
        if (stats.invocations == 0 || elapsed < stats.min_execution_cycles)
            stats.min_execution_cycles = elapsed;
        if (elapsed > stats.max_execution_cycles)
            stats.max_execution_cycles = elapsed;
        stats.execution_sum_cycles += elapsed;
        ++stats.invocations;
    }

    bool complete() const { return stats.invocations == sample_count; }
    TaskJitterStats stats = {};

private:
    int64_t expected_cycles = 0;
    uint64_t tolerance_cycles = 0;
    uint64_t elapsed_cycles = 0;
    uint64_t previous_abnormal_cycles = 0;
    bool abnormal_valid = false;
    uint64_t previous_episode_cycles = 0;
    bool episode_valid = false;
    bool previous_interval_abnormal = false;
    uint32_t previous = 0;
    bool previous_valid = false;
};

#endif
