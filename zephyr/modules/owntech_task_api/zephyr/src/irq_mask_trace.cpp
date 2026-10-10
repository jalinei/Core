#include "owntech_irq_trace.h"
#include "IrqMaskTraceState.h"
#include <soc.h>
#include <stdio.h>
#include <zephyr/kernel.h>

#ifndef OWNTECH_IRQ_TRACE_PATCH_PRESENT
#error "Apply scripts/irq_trace_kernel_patch.py before building the IRQ trace configuration"
#endif

struct IrqTraceEvent {
    uint32_t previous_isr;
    uint32_t isr;
    uint32_t sequence;
    IrqMaskInterval mask;
};

static constexpr uint32_t capacity = 1024;
static IrqTraceEvent events[capacity];
static IrqMaskTraceState trace;
static volatile bool enabled = false;
static uint32_t hz;
static uint32_t expected;
static uint32_t threshold;
static uint32_t previous_isr;
static bool previous_valid;
static uint32_t count, dropped;
static uint32_t intervals, pending_intervals, long_pending_intervals;
static uint32_t max_busy_cycles;
static uint32_t positive_events, recent_pending_positive_events;

extern "C" void owntech_irq_trace_start(uint32_t cpu_hz, uint32_t period_us)
{
    enabled = false;
    trace = {};
    hz = cpu_hz;
    expected = (static_cast<uint64_t>(cpu_hz) * period_us + 500000) / 1000000;
    threshold = (4ULL * cpu_hz) / 1000000;
    count = dropped = intervals = pending_intervals = long_pending_intervals = 0;
    max_busy_cycles = positive_events = recent_pending_positive_events = 0;
    previous_valid = false;
    enabled = true;
}

extern "C" void owntech_irq_trace_stop(void) { enabled = false; }

extern "C" void owntech_irq_trace_begin(uint32_t caller, uint32_t kind)
{
    if (!enabled) return;
    trace.begin(DWT->CYCCNT, caller, kind, __get_IPSR());
}

extern "C" void owntech_irq_trace_before_restore(uint32_t basepri, uint32_t primask, uint32_t site)
{
    if (!enabled || !trace.open || basepri || primask) return;
    // IRQ 67 is HRTIM master. Capture whether it is pending while still masked.
    const uint32_t pending = (NVIC->ISPR[67 / 32] >> (67 % 32)) & 1U;
    if (!trace.restore(DWT->CYCCNT, basepri, primask, site, pending)) return;
    ++intervals;
    const uint32_t duration = trace.latest.end - trace.latest.start;
    const uint32_t busy = duration - trace.latest.sleep_cycles;
    if (busy > max_busy_cycles) max_busy_cycles = busy;
    if (pending) {
        ++pending_intervals;
        if (busy > threshold) ++long_pending_intervals;
    }
}

extern "C" void owntech_irq_trace_sleep_begin(void)
{
    if (enabled) trace.sleep_begin(DWT->CYCCNT);
}
extern "C" void owntech_irq_trace_sleep_end(void)
{
    if (enabled) trace.sleep_end(DWT->CYCCNT);
}

extern "C" void owntech_irq_trace_isr(uint32_t now)
{
    if (!enabled) return;
    if (previous_valid) {
        const int64_t jitter = static_cast<int64_t>(static_cast<uint32_t>(now - previous_isr)) - expected;
        const uint64_t magnitude = jitter < 0 ? -jitter : jitter;
        if (magnitude > threshold) {
            if (jitter > 0) {
                ++positive_events;
                // This is a correlation criterion, not a latency estimate.
                if (trace.latest_valid && trace.latest.pending &&
                    static_cast<uint32_t>(now - trace.latest.end) < expected / 4)
                    ++recent_pending_positive_events;
            }
            if (count < capacity) {
                events[count++] = {previous_isr, now, trace.sequence,
                                  trace.latest_valid ? trace.latest : IrqMaskInterval{}};
            } else ++dropped;
        }
    }
    previous_isr = now;
    previous_valid = true;
}

extern "C" void owntech_irq_trace_print(void)
{
    printf("IRQ_TRACE_SUMMARY,%u,%u,%u,%u,%u,%u,%u,%u,%u\n", hz, expected,
           intervals, pending_intervals, long_pending_intervals, max_busy_cycles,
           positive_events, recent_pending_positive_events, threshold);
    printf("IRQ_TRACE_BEGIN,%u,%u\n", count, dropped);
    // All cycle values are raw uint32 DWT ticks; use modular differences.
    for (uint32_t i = 0; i < count; ++i) {
        const IrqTraceEvent &e = events[i];
        const IrqMaskInterval &m = e.mask;
        printf("IRQ_TRACE,%u,%u,%u,%u,%u,0x%08x,0x%08x,%u,%u,%u,%u\n",
               e.previous_isr, e.isr, e.sequence, m.start, m.end,
               m.caller, m.restore_site, m.sleep_cycles, m.kind, m.ipsr, m.pending);
    }
    printf("IRQ_TRACE_END\n");
    fflush(stdout);
}
