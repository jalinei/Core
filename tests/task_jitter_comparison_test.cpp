#include <assert.h>
#include "task_jitter_comparison.h"

static void invoke(TaskJitterComparison &monitor, uint32_t stamp, uint32_t delay)
{
    monitor.entry(stamp, stamp + delay);
    monitor.exit(stamp, stamp + delay + 1000);
}

int main()
{
    TaskJitterComparison monitor;
    monitor.configure(170000000, 200);
    uint32_t stamp = UINT32_MAX - 100;
    invoke(monitor, stamp, 100);
    stamp += 34000 + 850; invoke(monitor, stamp, 100);
    stamp += 34000 - 850; invoke(monitor, stamp, 100);
    assert(monitor.stats.both_abnormal == 2);
    stamp += 34000; invoke(monitor, stamp, 950);
    assert(monitor.stats.control_only_abnormal == 1);
    stamp += 34000 + 850; invoke(monitor, stamp, 100);
    assert(monitor.stats.isr_only_abnormal == 1);
    assert(monitor.stats.recorded_events == 4);
    assert(monitor.stats.events[2].isr_jitter_cycles == 0);
    assert(monitor.stats.events[2].control_jitter_cycles == 850);
    assert(monitor.stats.min_dispatch_cycles == 100);
    assert(monitor.stats.max_dispatch_cycles == 950);
    assert(monitor.stats.dispatch_sum_cycles == 1350);
    assert(monitor.isr.stats.invocations == 5);
    assert(monitor.isr.stats.intervals == 4);
    for (uint32_t i = 0; i < 1100; ++i) {
        stamp += 34000;
        invoke(monitor, stamp, i % 2 == 0 ? 950 : 100);
    }
    assert(monitor.stats.recorded_events == 1024);
    assert(monitor.stats.dropped_events == 80);
    assert(monitor.stats.control_only_abnormal == 1101);
    monitor.restart();
    stamp += 10000000; invoke(monitor, stamp, 100);
    assert(monitor.stats.recorded_events == 1024);
    assert(monitor.stats.dropped_events == 80);
    assert(monitor.isr.stats.intervals == 1104);
}
