#ifndef OWNTECH_IRQ_TRACE_H_
#define OWNTECH_IRQ_TRACE_H_
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void owntech_irq_trace_begin(uint32_t caller, uint32_t kind);
void owntech_irq_trace_before_restore(uint32_t basepri, uint32_t primask, uint32_t site);
void owntech_irq_trace_sleep_begin(void);
void owntech_irq_trace_sleep_end(void);
void owntech_irq_trace_start(uint32_t cpu_hz, uint32_t period_us);
void owntech_irq_trace_stop(void);
void owntech_irq_trace_isr(uint32_t cycles);
void owntech_irq_trace_print(void);
#ifdef __cplusplus
}
#endif
#endif
