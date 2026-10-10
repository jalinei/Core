/*
 * Copyright (c) 2022-present LAAS-CNRS
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published by
 *   the Free Software Foundation, either version 2.1 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1
 */

/*
 * @date   2024
 * @author Clément Foucher <clement.foucher@laas.fr>
 * @author Ayoub Farah Hassan <ayoub.farah-hassan@laas.fr>
 */


/* Current module */
#include "scheduling_common.h"

/* OwnTech Power API */
#include "timer.h"
#include "hrtim.h"
#include "SpinAPI.h"
#ifdef CONFIG_OWNTECH_TASK_JITTER_STATS
#include <stdio.h>
#include <soc.h>
#include "task_jitter_monitor.h"
#include "task_jitter_comparison.h"
#include "task_jitter_isr.h"
#ifdef CONFIG_OWNTECH_TASK_IRQ_LOCK_TRACE
#include "owntech_irq_trace.h"
#endif
#endif
#ifdef CONFIG_OWNTECH_COMMUNICATION_ENABLE_RS485
#include "communication_dispatch.h"
#endif

#include "safety_internal.h"
#include "SafetyAPI.h"

/* Size of stack area used by error thread */
#define STACKSIZE 512
#define PRIORITY 0

void thread_error(void *, void *, void *);

K_THREAD_DEFINE(thread_error_id, STACKSIZE, thread_error, NULL, NULL, NULL,
				PRIORITY, 0, 0);

/**
 *  Local variables and constants
 */

/* Timer device */
static const struct device* timer6 = DEVICE_DT_GET(TIMER6_DEVICE);

/* Task status */
static task_status_t uninterruptibleTaskStatus = task_status_t::inexistent;

/* Interrupt source */
static scheduling_interrupt_source_t interrupt_source = source_uninitialized;

/* For HRTIM interrupts */
static task_function_t user_periodic_task = NULL;

/* Data dispatch */
static bool do_data_dispatch = false;
static uint32_t task_period = 0;

/* Safety */
static bool safety_alert = false;

#ifdef CONFIG_OWNTECH_TASK_JITTER_STATS
extern "C" { volatile uint32_t owntech_task_isr_entry_cycles = 0; }
static TaskJitterMonitor jitter_monitor;
static TaskJitterComparison jitter_comparison;
static TaskJitterStats completed_isr_stats;
static TaskJitterStats completed_jitter_stats;
// Release/acquire publishes an immutable snapshot without calling the kernel
// from the zero-latency TIM6 interrupt. Never overwrite a published snapshot.
static uint32_t jitter_report_ready = 0;
static bool jitter_initialized = false;
static bool jitter_active = false;

static void start_jitter_monitor()
{
	if (__atomic_load_n(&jitter_report_ready, __ATOMIC_ACQUIRE)) return;
	if (!jitter_initialized) {
		CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
		DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
		const uint32_t before = DWT->CYCCNT;
		__DSB();
		__NOP();
		__NOP();
		if (DWT->CYCCNT == before || SystemCoreClock == 0) {
			printk("Task jitter statistics unavailable: DWT counter not running\n");
			jitter_active = false;
			return;
		}
		jitter_monitor.configure(SystemCoreClock, task_period);
        jitter_comparison.configure(SystemCoreClock, task_period);
		jitter_initialized = true;
		jitter_active = true;
	}
	jitter_monitor.restart();
    jitter_comparison.restart();
#ifdef CONFIG_OWNTECH_TASK_IRQ_LOCK_TRACE
    owntech_irq_trace_start(SystemCoreClock, task_period);
#endif
}

static void print_jitter_report(const TaskJitterStats &s, const char *execution_label = "Control execution")
{
	const double cycles_per_us = s.cpu_hz / 1000000.0;
	printf("Control timing: %u invocations, %u intervals, period %u us, CPU %u Hz\n",
		   s.invocations, s.intervals, s.period_us, s.cpu_hz);
	// Floating-point formatting is enabled for printf in this project's libc.
	printf("Period jitter (us): min %.6f, max %.6f, mean %.6f, mean absolute %.6f\n",
		   s.min_jitter_cycles / cycles_per_us,
		   s.max_jitter_cycles / cycles_per_us,
		   s.intervals ? s.jitter_sum_cycles / (cycles_per_us * s.intervals) : 0.0,
		   s.intervals ? s.absolute_jitter_sum_cycles / (cycles_per_us * s.intervals) : 0.0);
	printf("Absolute jitter > 4 us: %u (%.6f%%); no updates skipped\n",
		   s.over_4us, s.intervals ? 100.0 * s.over_4us / s.intervals : 0.0);
	printf("%s (us): min %.6f, max %.6f, mean %.6f\n", execution_label,
		   s.min_execution_cycles / cycles_per_us,
		   s.max_execution_cycles / cycles_per_us,
		   s.execution_sum_cycles / (cycles_per_us * s.invocations));
    if (s.abnormal_gap_count) {
        printf("Abnormal interval gaps (us): count %u, min %.6f, max %.6f, mean %.6f\n",
               s.abnormal_gap_count, s.min_abnormal_gap_cycles / cycles_per_us,
               s.max_abnormal_gap_cycles / cycles_per_us,
               s.abnormal_gap_sum_cycles / (cycles_per_us * s.abnormal_gap_count));
    } else {
        printf("Abnormal interval gaps: no consecutive abnormal intervals measured\n");
    }
    printf("Abnormal episodes: %u (consecutive intervals grouped)\n", s.abnormal_episodes);
    if (s.episode_gap_count) {
        printf("Episode start gaps (us): count %u, min %.6f, max %.6f, mean %.6f\n",
               s.episode_gap_count, s.min_episode_gap_cycles / cycles_per_us,
               s.max_episode_gap_cycles / cycles_per_us,
               s.episode_gap_sum_cycles / (cycles_per_us * s.episode_gap_count));
    } else {
        printf("Episode start gaps: fewer than two episodes in a continuous run\n");
    }
    // Integer output preserves the exact bin counts and cycle-based edges.
    printf("JITTER_HISTOGRAM_BEGIN,%u,%u,%u,%u,%u\n", s.cpu_hz,
           s.histogram_width_cycles, s.intervals,
           s.histogram_underflow, s.histogram_overflow);
    for (uint32_t i = 0; i < TaskJitterStats::histogram_bins; ++i)
        printf("JITTER_BIN,%u,%u\n", i, s.histogram[i]);
    printf("JITTER_HISTOGRAM_END\n");
    fflush(stdout);
}

static void jitter_report_thread(void *, void *, void *)
{
	while (!__atomic_load_n(&jitter_report_ready, __ATOMIC_ACQUIRE))
		k_msleep(1000);
    printf("TIMING_SECTION_BEGIN,CALLBACK\n");
    print_jitter_report(completed_jitter_stats);
    printf("TIMING_SECTION_END,CALLBACK\n");
    const TaskJitterComparisonStats &c = jitter_comparison.stats;
    if (c.samples) {
        printf("TIMING_SECTION_BEGIN,ISR\n");
        print_jitter_report(completed_isr_stats, "HRTIM handler through callback");
        printf("TIMING_SECTION_END,ISR\n");
        const double cycles_per_us = completed_isr_stats.cpu_hz / 1000000.0;
        printf("ISR-to-callback (us): samples %u, min %.6f, max %.6f, mean %.6f\n",
               c.samples, c.min_dispatch_cycles / cycles_per_us,
               c.max_dispatch_cycles / cycles_per_us,
               c.dispatch_sum_cycles / (cycles_per_us * c.samples));
        printf("Paired abnormal intervals: both %u, ISR only %u, callback only %u\n",
               c.both_abnormal, c.isr_only_abnormal, c.control_only_abnormal);
        printf("JITTER_EVENTS_BEGIN,%u,%u\n", c.recorded_events, c.dropped_events);
        for (uint32_t i = 0; i < c.recorded_events; ++i) {
            const TaskJitterEvent &event = c.events[i];
            printf("JITTER_EVENT,%u,%lld,%lld,%u\n", event.invocation,
                   static_cast<long long>(event.isr_jitter_cycles),
                   static_cast<long long>(event.control_jitter_cycles), event.dispatch_cycles);
        }
        printf("JITTER_EVENTS_END\n");
    }
#ifdef CONFIG_OWNTECH_TASK_IRQ_LOCK_TRACE
    owntech_irq_trace_print();
#endif
    printf("TIMING_COMPARISON_END\n");
    fflush(stdout);
}

K_THREAD_DEFINE(jitter_report_id, 1536, jitter_report_thread, NULL, NULL, NULL,
				7, 0, 0);
#endif

bool scheduling_get_critical_jitter_stats(TaskJitterStats &stats)
{
#ifdef CONFIG_OWNTECH_TASK_JITTER_STATS
	if (__atomic_load_n(&jitter_report_ready, __ATOMIC_ACQUIRE)) {
		stats = completed_jitter_stats;
		return true;
	}
#else
	(void)stats;
#endif
	return false;
}


/* Private API */


void thread_error(void *, void *, void *)
{
	while (1)
	{
		if (safety_alert)
		{
			printk("SAFETY ERROR : reset the MCU \n");

			#ifdef CONFIG_SHIELD_TWIST
				printk(	"problem with I1 : %s \n"
						"problem with V1 : %s \n"
						"problem with I2 : %s \n"
						"problem with V2 : %s \n"
						"problem with Ihigh : %s \n"
						"problem with Vhigh : %s \n"
						, safety.getChannelError(I1_LOW) ? "true" : "false"
						, safety.getChannelError(V1_LOW) ? "true" : "false"
						, safety.getChannelError(I2_LOW) ? "true" : "false"
						, safety.getChannelError(I2_LOW) ? "true" : "false"
						, safety.getChannelError(I_HIGH) ? "true" : "false"
						, safety.getChannelError(V_HIGH) ? "true" : "false"
						);
			#endif

		}

		k_msleep(200);
	}
}

void user_task_proxy()
{
#ifdef CONFIG_OWNTECH_SAFETY_API

	if (safety_task() != 0) safety_alert = true;

#endif

	if (user_periodic_task == NULL) return;

	if (do_data_dispatch == true)
	{
		spin.data.doFullDispatch();
	}

	#ifdef CONFIG_OWNTECH_COMMUNICATION_ENABLE_RS485
	communication_dispatch();
	#endif

#ifdef CONFIG_OWNTECH_TASK_JITTER_STATS
	if (jitter_active) {
        const uint32_t callback_stamp = DWT->CYCCNT;
        const uint32_t isr_stamp = owntech_task_isr_entry_cycles;
        jitter_monitor.entry(callback_stamp);
        if (interrupt_source == source_hrtim)
            jitter_comparison.entry(isr_stamp, callback_stamp);
		const uint32_t start = DWT->CYCCNT;
		user_periodic_task();
		const uint32_t end = DWT->CYCCNT;
		jitter_monitor.exit(start, end);
        if (interrupt_source == source_hrtim)
            jitter_comparison.exit(isr_stamp, end);
		if (jitter_monitor.complete()) {
#ifdef CONFIG_OWNTECH_TASK_IRQ_LOCK_TRACE
            owntech_irq_trace_stop();
#endif
			completed_jitter_stats = jitter_monitor.stats;
            completed_isr_stats = jitter_comparison.isr.stats;

			__atomic_store_n(&jitter_report_ready, 1U, __ATOMIC_RELEASE);
			jitter_active = false;
		}
		return;
	}
#endif
	user_periodic_task();
}

/* Public API */

void scheduling_set_uninterruptible_synchronous_task_interrupt_source(
									scheduling_interrupt_source_t int_source)
{
	interrupt_source = int_source;
}

int8_t scheduling_define_uninterruptible_synchronous_task(
									task_function_t periodic_task,
									uint32_t task_period_us)
{
	if ( (uninterruptibleTaskStatus != task_status_t::inexistent) &&
		 (uninterruptibleTaskStatus != task_status_t::suspended))
		return -1;

	if (periodic_task == NULL)
		return -1;

	if (interrupt_source == source_tim6)
	{
		if (device_is_ready(timer6) == false)
			return -1;

		task_period = task_period_us;
		user_periodic_task = periodic_task;
#ifdef CONFIG_OWNTECH_TASK_JITTER_STATS
		jitter_initialized = false;
#endif

		/* Everything OK, go on with timer configuration */
		struct timer_config_t timer_cfg = {0};
		timer_cfg.timer_enable_irq       = 1;
		timer_cfg.timer_irq_callback     = user_task_proxy;
		timer_cfg.timer_irq_t_usec       = task_period_us;
		timer_cfg.timer_use_zero_latency = 1;

		timer_config(timer6, &timer_cfg);

		uninterruptibleTaskStatus = task_status_t::defined;

		return 0;
	}
	else if (interrupt_source == source_hrtim)
	{
		uint32_t hrtim_period_us = hrtim_period_Master_get_us();

		if (hrtim_period_us == 0)
			return -1;

		if (task_period_us % hrtim_period_us != 0)
			return -1;

		uint32_t repetition = task_period_us / hrtim_period_us;

		if (repetition == 0)
			return -1;

		if (hrtim_PeriodicEvent_configure(MSTR, repetition, user_task_proxy) != 0)
			return -1;
		task_period = task_period_us;
		user_periodic_task = periodic_task;
#ifdef CONFIG_OWNTECH_TASK_JITTER_STATS
		jitter_initialized = false;
#endif

		uninterruptibleTaskStatus = task_status_t::defined;

		return 0;
	}

	return -1;
}

void scheduling_start_uninterruptible_synchronous_task(
									bool manage_data_acquisition)
{
	if ( (uninterruptibleTaskStatus != task_status_t::defined) &&
		 (uninterruptibleTaskStatus != task_status_t::suspended) )
		return;

	if (interrupt_source == scheduling_interrupt_source_t::source_uninitialized)
		return;

	if ( (manage_data_acquisition == true) && (spin.data.started() == false) )
	{
		/**
		 * If Data Acquisition has not been started yet,
		 * then Scheduling will be in charge of data dispatch
		 */
		do_data_dispatch = true;

		/* Configure Data Acquisition module */
		spin.data.setDispatchMethod(DispatchMethod_t::externally_triggered);

		uint32_t repetition;
		if (interrupt_source == scheduling_interrupt_source_t::source_hrtim)
		{
			repetition = hrtim_PeriodicEvent_GetRep(MSTR);
		}
		else /* (interrupt_source == scheduling_interrupt_source_t::source_tim6) */
		{
			uint32_t hrtim_period_us = hrtim_period_Master_get_us();
			if (hrtim_period_us == 0)
			{
				return;
			}

			repetition = task_period / hrtim_period_us;
		}
		spin.data.setRepetitionsBetweenDispatches(repetition);

		/* Then start it */
		spin.data.start();
	}

	if (interrupt_source == source_tim6)
	{
		if (device_is_ready(timer6) == false)
			return;

#ifdef CONFIG_OWNTECH_TASK_JITTER_STATS
		start_jitter_monitor();
#endif
		timer_start(timer6);

		uninterruptibleTaskStatus = task_status_t::running;
	}
	else if (interrupt_source == source_hrtim)
	{
		if (user_periodic_task == NULL)
			return;

#ifdef CONFIG_OWNTECH_TASK_JITTER_STATS
		start_jitter_monitor();
#endif
		if (hrtim_PeriodicEvent_en(MSTR) != 0)
		{
			printk("Unable to start critical task: invalid HRTIM synchronization timing\n");
			return;
		}

		uninterruptibleTaskStatus = task_status_t::running;
	}
}

void scheduling_stop_uninterruptible_synchronous_task()
{
	if (uninterruptibleTaskStatus != task_status_t::running)
		return;

	if (interrupt_source == source_tim6)
	{
		if (device_is_ready(timer6) == false)
			return;

		timer_stop(timer6);

		uninterruptibleTaskStatus = task_status_t::suspended;
	}
	else if (interrupt_source == source_hrtim)
	{
		hrtim_PeriodicEvent_dis(MSTR);

		uninterruptibleTaskStatus = task_status_t::suspended;
	}
}
