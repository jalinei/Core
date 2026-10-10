# Control task timing statistics

`CONFIG_OWNTECH_TASK_JITTER_STATS=y` (default on Cortex-M4) enables a DWT
statistics window when `task.startCritical()` starts the control task. Control
updates always run: exceeding 4 us only increments a statistics counter.

The first 1,000,000 invocations produce one automatic serial-console report:

- Invocation and interval counts, configured period and CPU frequency.
- Minimum, maximum and signed mean period jitter, in microseconds.
- Mean absolute period jitter, in microseconds.
- Number and percentage of intervals with absolute jitter strictly above 4 us.
- Minimum, maximum and mean user callback execution duration, in microseconds.

Jitter is `(current callback entry - previous callback entry) - task period`.
The timestamp is taken after safety checks, ADC dispatch and RS485 dispatch,
immediately before the monitoring bookkeeping and user callback. It includes
variations in that preceding work. Execution duration measures only the user
callback, including any interrupts that preempt it. It excludes dispatch and
statistics bookkeeping. The instrumentation adds some CPU overhead.

The first invocation has no preceding interval, so an uninterrupted window
contains 999,999 interval measurements. Stop/start preserves collected samples
but excludes the stopped interval. Redefining the task resets an unfinished
window. After publication the report is frozen until reboot, and the ISR stops
reading DWT and accumulating statistics. Printing occurs in a background thread
after collection, never in the control interrupt.

The completed raw cycle counts can also be read from background/thread context:

```cpp
TaskJitterStats stats;
if (task.getCriticalJitterStats(stats)) {
    // Convert cycles to microseconds: cycles * 1e6 / stats.cpu_hz.
}
```

The CPU clock must remain constant during collection. Unsigned subtraction
handles counter wrap provided each measured interval/duration is shorter than
one 32-bit wrap (about 25.3 seconds at 170 MHz). Debugger halts can distort
measurements. This measures period jitter, not lateness relative to the timer's
hardware release, and cannot detect a callback that never returns.

This application uses a 200 us period; collection takes approximately 200
seconds (3 min 20 s). Master UID: `0x002B001A`; USB ID: `3232500B002B001A`.

Disable with `CONFIG_OWNTECH_TASK_JITTER_STATS=n` in `zephyr/prj.conf` to remove
the monitor and reporting thread.

Host verification:

```sh
g++ -std=c++11 -Wall -Wextra -Werror \
  -Izephyr/modules/owntech_task_api/zephyr/public_api \
  -Izephyr/modules/owntech_task_api/zephyr/src \
  tests/task_jitter_monitor_test.cpp -o /tmp/task_jitter_monitor_test
/tmp/task_jitter_monitor_test
```

## Histogram and abnormal-event spacing

The monitor also collects 161 uniform bins of approximately 0.25 us, centered
at zero (43 cycles per bin at 170 MHz), covering approximately +/-20.36 us.
The reported width is in CPU cycles; edges for bin `i` are
`(i - 80) * width - width / 2` inclusive to that value plus `width` exclusive.
Underflow and overflow counts retain events outside this range. Histogram
counts plus both overflow counts equal the number of measured intervals.
The histogram is printed after collection, with BEGIN/END markers and one
integer count per bin. The control interrupt performs integer binning only.

An abnormal event is an interval with absolute jitter strictly greater than
4 us. Elapsed CPU cycles between consecutive abnormal interval endpoints
produce minimum, maximum and mean event spacing. Accumulation handles multiple
DWT wraps between events. Adjacent abnormal intervals count separately; a late
callback followed by a compensating early callback can therefore produce a
gap near one control period. Stop/start excludes gaps across stopped periods.
This measures event spacing rather than the interval between independent
underlying disturbances. With fewer than two events, no gap is reported.

For the main spacing measurement, consecutive abnormal intervals form one
episode. A positive error and its immediately following negative compensation
therefore count once. Episode start-to-start gaps are reported separately from
the raw abnormal-interval gaps. This is a grouping rule, not proof of independent
physical disturbances: a normal interval splits episodes. Histograms retain
both signed errors, since period jitter is the difference between successive
callback latencies. Stop/start excludes episode gaps across stopped periods.

## Plotting a captured histogram

The hardware capture is saved in
`docs/twist-histogram-stats-3232500B002B001A.log`. The plot, vector export and
exact histogram CSV are in `docs/plots/twist-jitter-3232500B002B001A.*`.
The plot shows the central distribution with linear counts and the tails
with logarithmic counts. Both refer to the same measured intervals.

To regenerate, with Python, matplotlib and numpy installed:

```sh
python3 scripts/plot_task_jitter.py \
  docs/twist-histogram-stats-3232500B002B001A.log \
  --output docs/plots/twist-jitter-3232500B002B001A
```

The script checks all 161 bin records, overflow counts and metadata before
plotting. CSV edges are inclusive on the left and exclusive on the right.
The caption's idle-mode label describes this capture; change it for power-mode
measurements. Collection adds monitoring overhead, so compare timing results
under matching instrumentation and workloads.

## Paired ISR / callback comparison

The HRTIM driver reads DWT at the first C statement in `_hrtim_callback()`.
This occurs after hardware exception entry, compiler prologue and Zephyr's
regular ISR wrapper, before HRTIM synchronization handling and data dispatch.
It is a handler-entry period-jitter measurement, not absolute timer-release
latency. The callback timestamp remains before monitoring bookkeeping.
Both measurements cover the same million completed control invocations.

The ISR-to-callback delay includes synchronization handling and ADC/RS485
dispatch, plus the timestamp store/load overhead. Histogram accumulation runs
after both timestamps and is excluded from this delay. ISR execution duration
includes monitoring overhead and the callback. The original callback execution
duration excludes accumulation. Monitoring changes add CPU overhead.

Separate ISR and callback histogram sections include sample counts and episode
gaps. Paired counts distinguish intervals abnormal at both points or only one.
Up to 1024 abnormal paired records are retained, with explicit dropped-record
count; aggregate statistics always cover the whole window. Event fields are
invocation number, ISR jitter cycles, callback jitter cycles, and current
ISR-to-callback delay cycles. TIM6 retains its callback-only report.


The paired capture is in
`docs/twist-isr-callback-comparison-3232500B002B001A.log`; plots and CSVs are
in `docs/plots/twist-isr-callback-comparison-3232500B002B001A.*`.
Regenerate with:

```sh
python3 scripts/plot_task_jitter_comparison.py \
  docs/twist-isr-callback-comparison-3232500B002B001A.log \
  --output docs/plots/twist-isr-callback-comparison-3232500B002B001A
```

The comparison script validates matching invocation counts, interval counts,
CPU frequency and bin widths, the histogram totals, paired abnormal totals,
and recorded/dropped event counts. The event CSV permits inspecting each
retained abnormal interval. Its invocation number times the nominal period is
an approximate elapsed-time coordinate. The idle-mode label describes this
capture, and must be changed for other workloads.

Host verification of the paired monitor:

```sh
g++ -std=c++11 -Wall -Wextra -Werror \
  -Izephyr/modules/owntech_task_api/zephyr/public_api \
  -Izephyr/modules/owntech_task_api/zephyr/src \
  tests/task_jitter_comparison_test.cpp -o /tmp/task_jitter_comparison_test
/tmp/task_jitter_comparison_test
```

## Temporary interrupt-masking trace

`CONFIG_OWNTECH_TASK_IRQ_LOCK_TRACE=y` adds the diagnostic. Apply the guarded
Zephyr 4.0 patch with `scripts/irq_trace_kernel_patch.py --framework <Zephyr>
apply` before building. Original bytes and patched checksums are backed up in
`/tmp/owntech-irq-trace-kernel-backup.json`. The same command with `restore`
restores those bytes, refusing to overwrite unrelated subsequent edits.
Disable the trace configuration before rebuilding without the patch.

Coverage: Cortex-M4 C arch_irq_lock/unlock, PendSV's assembly BASEPRI changes,
and CPU idle PRIMASK changes. PM is disabled in this firmware; startup reset
masking is outside collection. Arbitrary vendor/direct CMSIS masking and
per-IRQ NVIC disables are not covered. Nested masks are grouped until both
BASEPRI and PRIMASK are restored to zero. Caller and restore addresses are
inline PCs for the C paths, assembly PCs for PendSV, and function addresses
for idle. IPSR identifies the context that started the outer interval.

Timestamp boundaries are diagnostic approximations: the start is recorded
after the masking instruction; the end is recorded before the restoring
instruction and before the trace-hook bookkeeping returns. Pending HRTIM
IRQ state is read while still masked. Extra hook overhead is included in
the actual masking delay, so these timestamps bracket it imperfectly.
Do not interpret them as cycle-exact instruction boundaries.

Idle's WFI/WFE time is recorded separately. Subtract it from total mask
duration to obtain traced busy time, on the DWT clock basis. DWT clock
stoppage during sleep would limit wall-time interpretation. Up to 1024
abnormal HRTIM intervals retain previous/current entry timestamps and the
most recently completed mask interval, including caller, restore site,
sleep time, kind flags (1 C lock, 2 PendSV, 4 idle), starting IPSR and pending
HRTIM state. Recorded and dropped counts are explicit. These are nearby
mask records, not proof that every recorded mask caused its paired outlier.
Trace state freezes with the million-invocation window; output is deferred.
Stop/start restarts the masking-trace window.


### Collected interrupt-mask trace

The diagnostic ran on master `3232500B002B001A` for 1,000,000 invocations at
200 us. The full report is `docs/twist-irq-mask-trace-3232500B002B001A.log`;
resolved records, summary and plots are
`docs/plots/twist-irq-mask-analysis-3232500B002B001A.*`.
The matching diagnostic ELF, binary, kernel backup and patched kernel sources
are archived locally under `.pio/diagnostics/irq-mask-3232500B002B001A/`.
Keep the ELF when moving these files: caller decoding requires this exact build.
Inline source line numbers refer to the archived patched sources.

Regenerate the analysis with:

```sh
python3 scripts/analyze_irq_mask_trace.py \
  docs/twist-irq-mask-trace-3232500B002B001A.log \
  --elf .pio/diagnostics/irq-mask-3232500B002B001A/firmware.elf \
  --addr2line /home/jal/.platformio/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-addr2line \
  --output docs/plots/twist-irq-mask-analysis-3232500B002B001A
```

All 1715 positive abnormal ISR intervals had a recently completed mask with
HRTIM pending. Pending masks also occur on most ordinary periods; association
alone does not establish causation. Of the 502 positive records retained,
295 started in SysTick, with the last lock caller resolving through
`z_time_slice`; 171 resolved through `idle`, 24 through `z_tick_sleep`, eight
through `sys_clock_tick_get`, three through USB `cdc_acm_poll_out`, and one
through `k_work_schedule_for_queue`. These are the first retained records,
not a random sample: 2439 abnormal intervals exceeded the 1024-record capacity.
Only 71 of those 502 positive records had a traced busy mask exceeding 4 us.
The maximum traced busy mask across the whole window was 22.341 us.

There is an additional blocking mechanism: HRTIM uses regular IRQ priority 0
(flags 0), mapped to `_IRQ_PRIO_OFFSET` by Cortex-M `irq_manage.c`.
`drivers/timer/cortex_m_systick.c` sets SysTick to that same hardware priority.
HRTIM therefore cannot preempt an already running SysTick handler. A short
last lock in SysTick can accompany a much longer ISR delay, so the observations
support investigating the entire SysTick handler as well as masked sections.
This instrumentation increases interrupt delays and must not be compared as
an uninstrumented timing baseline. The plot's expected-entry marker comes
from the previous ISR entry plus 200 us, not a hardware timer release stamp.

After collecting the trace, the shared SDK is restored and the trace option
is disabled for subsequent builds. The board retains the diagnostic firmware;
its trace is frozen after collection.
