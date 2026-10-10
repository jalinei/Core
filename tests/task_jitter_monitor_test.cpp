#include <assert.h>
#include "task_jitter_monitor.h"

static void invoke(TaskJitterMonitor &monitor, uint32_t start, uint32_t duration)
{
    monitor.entry(start);
    monitor.exit(start, start + duration);
}

int main()
{
    TaskJitterMonitor monitor;
    monitor.configure(170000000, 500);
    // First sample and execution timing both cross the 32-bit wrap.
    uint32_t now = UINT32_MAX - 100;
    invoke(monitor, now, 170);
    assert(monitor.stats.intervals == 0);
    now += 85000 + 680; // Exactly +4 us: not an exceedance.
    invoke(monitor, now, 340);
    now += 85000 - 680; // Exactly -4 us: not an exceedance.
    invoke(monitor, now, 510);
    now += 85000 + 681;
    invoke(monitor, now, 170);
    now += 85000 - 681;
    invoke(monitor, now, 170);
    assert(monitor.stats.intervals == 4);
    assert(monitor.stats.over_4us == 2);
    assert(monitor.stats.abnormal_gap_count == 1);
    assert(monitor.stats.min_abnormal_gap_cycles == 84319);
    assert(monitor.stats.max_abnormal_gap_cycles == 84319);
    assert(monitor.stats.min_jitter_cycles == -681);
    assert(monitor.stats.max_jitter_cycles == 681);
    assert(monitor.stats.jitter_sum_cycles == 0);
    assert(monitor.stats.absolute_jitter_sum_cycles == 2722);
    assert(monitor.stats.min_execution_cycles == 170);
    assert(monitor.stats.max_execution_cycles == 510);
    assert(monitor.stats.execution_sum_cycles == 1360);

    // A stop/start gap must not enter the interval statistics.
    monitor.restart();
    now += 123456789;
    invoke(monitor, now, 170);
    assert(monitor.stats.intervals == 4);
    while (!monitor.complete()) {
        now += 85000;
        invoke(monitor, now, 170);
    }
    assert(monitor.stats.invocations == 1000000);
    assert(monitor.stats.intervals == 999998);
    assert(monitor.stats.execution_sum_cycles == 170000510);
    invoke(monitor, now + 999999, 999999);
    assert(monitor.stats.invocations == 1000000);
    assert(monitor.stats.over_4us == 2);
    assert(monitor.stats.abnormal_gap_count == 1);
    assert(monitor.stats.min_abnormal_gap_cycles == 84319);
    assert(monitor.stats.max_abnormal_gap_cycles == 84319);

    monitor.configure(170000000, 500);
    now = 0;
    for (uint32_t i = 0; i < 1000000; ++i) {
        invoke(monitor, now, 170);
        now += 85000;
    }
    assert(monitor.complete());
    assert(monitor.stats.intervals == 999999);
    assert(monitor.stats.jitter_sum_cycles == 0);
    assert(monitor.stats.absolute_jitter_sum_cycles == 0);
    assert(monitor.stats.over_4us == 0);
    assert(monitor.stats.histogram[80] == 999999);
    assert(monitor.stats.histogram_underflow == 0);
    assert(monitor.stats.histogram_overflow == 0);

    // Exact histogram edges, both overflow buckets, and long event spacing.
    monitor.configure(170000000, 500);
    now = UINT32_MAX - 100;
    invoke(monitor, now, 170);
    now += 85000 - 21; invoke(monitor, now, 170);
    now += 85000 + 21; invoke(monitor, now, 170);
    now += 85000 - 22; invoke(monitor, now, 170);
    now += 85000 + 22; invoke(monitor, now, 170);
    assert(monitor.stats.histogram[80] == 2);
    assert(monitor.stats.histogram[79] == 1);
    assert(monitor.stats.histogram[81] == 1);
    now += 85000 - 3462; invoke(monitor, now, 170);
    now += 85000 + 3462; invoke(monitor, now, 170);
    assert(monitor.stats.histogram_underflow == 1);
    assert(monitor.stats.histogram_overflow == 1);
    assert(monitor.stats.abnormal_gap_count == 1);
    assert(monitor.stats.min_abnormal_gap_cycles == 88462);
    assert(monitor.stats.abnormal_episodes == 1);
    assert(monitor.stats.episode_gap_count == 0);
    for (uint32_t i = 0; i < 100000; ++i) {
        now += 85000; invoke(monitor, now, 170);
    }
    now += 85000 + 681; invoke(monitor, now, 170);
    assert(monitor.stats.abnormal_gap_count == 2);
    assert(monitor.stats.max_abnormal_gap_cycles == 8500085681ULL);
    assert(monitor.stats.abnormal_gap_sum_cycles == 8500174143ULL);
    assert(monitor.stats.abnormal_episodes == 2);
    assert(monitor.stats.episode_gap_count == 1);
    assert(monitor.stats.min_episode_gap_cycles == 8500174143ULL);
    assert(monitor.stats.max_episode_gap_cycles == 8500174143ULL);
    monitor.restart();
    now += 85000; invoke(monitor, now, 170);
    now += 85000 + 681; invoke(monitor, now, 170);
    assert(monitor.stats.abnormal_gap_count == 2);
    assert(monitor.stats.abnormal_episodes == 3);
    assert(monitor.stats.episode_gap_count == 1);
    uint32_t total = monitor.stats.histogram_underflow + monitor.stats.histogram_overflow;
    for (uint32_t count : monitor.stats.histogram) total += count;
    assert(total == monitor.stats.intervals);
}
