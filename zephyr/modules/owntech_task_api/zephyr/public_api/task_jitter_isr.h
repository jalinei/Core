#ifndef OWNTECH_TASK_JITTER_ISR_H_
#define OWNTECH_TASK_JITTER_ISR_H_
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Written at the first C statement in the HRTIM handler; no kernel calls.
extern volatile uint32_t owntech_task_isr_entry_cycles;
#ifdef __cplusplus
}
#endif
#endif
