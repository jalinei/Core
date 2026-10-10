#ifndef OWNTECH_TASK_JITTER_STATS_H_
#define OWNTECH_TASK_JITTER_STATS_H_

#include <stdint.h>

/** One statistics window. Times are CPU cycles; divide by cpu_hz / 1e6
 * to obtain microseconds. No control invocations are skipped.
 */
struct TaskJitterStats {
    static constexpr uint32_t histogram_bins = 161;
    uint32_t cpu_hz;
    uint32_t period_us;
    uint32_t invocations;
    uint32_t intervals;
    uint32_t over_4us;
    uint32_t histogram_width_cycles;
    uint32_t histogram[histogram_bins];
    uint32_t histogram_underflow;
    uint32_t histogram_overflow;
    uint32_t abnormal_gap_count;
    uint64_t min_abnormal_gap_cycles;
    uint64_t max_abnormal_gap_cycles;
    uint64_t abnormal_gap_sum_cycles;
    uint32_t abnormal_episodes;
    uint32_t episode_gap_count;
    uint64_t min_episode_gap_cycles;
    uint64_t max_episode_gap_cycles;
    uint64_t episode_gap_sum_cycles;
    int64_t jitter_sum_cycles;
    uint64_t absolute_jitter_sum_cycles;
    int64_t min_jitter_cycles;
    int64_t max_jitter_cycles;
    uint64_t execution_sum_cycles;
    uint32_t min_execution_cycles;
    uint32_t max_execution_cycles;
};

#endif
