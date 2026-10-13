/* SPDX-License-Identifier: LGPL-2.1 */
#pragma once
#include <stdint.h>
#include <soc.h>

enum AdcProfileStage {
    ADC_PROFILE_TOTAL, ADC_PROFILE_ADC1, ADC_PROFILE_ADC2, ADC_PROFILE_ADC3,
    ADC_PROFILE_ADC4, ADC_PROFILE_ADC5, ADC_PROFILE_CURSOR, ADC_PROFILE_COPY,
    ADC_PROFILE_CLOCK, ADC_PROFILE_STAGE_COUNT
};
#ifdef ADC_DISPATCH_PROFILE
struct AdcProfileStats {
    uint32_t count, minimum, maximum, last;
    uint64_t cycles, samples;
};
static_assert(sizeof(AdcProfileStats) == 32, "Host profiler record layout");
extern volatile AdcProfileStats adc_profile_stats[ADC_PROFILE_STAGE_COUNT];
// 0: off; 1: outer; 2: ADC_PROFILE_DETAIL. Host clears stats only when off.
extern volatile uint32_t adc_profile_mode;
inline void adc_profile_init()
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}
class AdcProfileScope {
    AdcProfileStage stage;
    bool active;
    uint32_t start;
    uint32_t samples = 0;
public:
    __attribute__((always_inline)) explicit AdcProfileScope(AdcProfileStage s)
        : stage(s), active(
#ifdef ADC_PROFILE_DETAIL
          adc_profile_mode == 2 ||
#endif
          ((s == ADC_PROFILE_TOTAL || s == ADC_PROFILE_CLOCK) && adc_profile_mode != 0))
    {
        if (active) start = DWT->CYCCNT;
    }
    void setSamples(uint32_t n) { samples = n; }
    __attribute__((always_inline)) ~AdcProfileScope()
    {
        if (!active) return;
        const uint32_t elapsed = DWT->CYCCNT - start;
        auto &stats = adc_profile_stats[stage];
        if (stats.count == 0 || elapsed < stats.minimum) stats.minimum = elapsed;
        if (elapsed > stats.maximum) stats.maximum = elapsed;
        stats.last = elapsed;
        stats.cycles = stats.cycles + elapsed;
        stats.samples = stats.samples + samples;
        stats.count = stats.count + 1;
        if (stage == ADC_PROFILE_TOTAL && stats.count >= 100000) adc_profile_mode = 0;
    }
};
#else
inline void adc_profile_init() {}
class AdcProfileScope {
public:
    explicit AdcProfileScope(AdcProfileStage) {}
    void setSamples(uint32_t) {}
};
#endif
