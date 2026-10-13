/* SPDX-License-Identifier: LGPL-2.1 */
#pragma once
#include <stdint.h>
#include <soc.h>

enum Rs485ProfileStage {
    RS485_PROFILE_TOTAL, RS485_PROFILE_POLL_BEFORE,
    RS485_PROFILE_EXTRA_TX_POLL, RS485_PROFILE_POSITION,
    RS485_PROFILE_COPY, RS485_PROFILE_POLL_AFTER,
    RS485_PROFILE_PARSE, RS485_PROFILE_PREPARE,
    RS485_PROFILE_CLOCK, RS485_PROFILE_STAGE_COUNT
};

#ifdef RS485_DISPATCH_PROFILE
struct Rs485ProfileStats {
    uint32_t count, minimum, maximum, last;
    uint64_t cycles, bytes;
};
extern volatile Rs485ProfileStats rs485_profile_stats[RS485_PROFILE_STAGE_COUNT];
// 0: off; 1: outer; 2: RS485_PROFILE_DETAIL. Stops at 100k; reset stats only when off.
extern volatile uint32_t rs485_profile_mode;
inline void rs485_profile_init()
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}
class Rs485ProfileScope {
    Rs485ProfileStage stage;
    bool active;
    uint32_t start;
    uint32_t bytes = 0;
public:
    __attribute__((always_inline)) explicit Rs485ProfileScope(Rs485ProfileStage s) : stage(s), active(
#ifdef RS485_PROFILE_DETAIL
        rs485_profile_mode == 2 ||
#endif
        ((s == RS485_PROFILE_TOTAL || s == RS485_PROFILE_PARSE ||
          s == RS485_PROFILE_PREPARE || s == RS485_PROFILE_CLOCK) && rs485_profile_mode != 0))
    {
        if (active) start = DWT->CYCCNT;
    }
    void setBytes(uint32_t n) { bytes = n; }
    __attribute__((always_inline)) ~Rs485ProfileScope()
    {
        if (!active) return;
        const uint32_t elapsed = DWT->CYCCNT - start;
        auto &stats = rs485_profile_stats[stage];
        if (stats.count == 0 || elapsed < stats.minimum) stats.minimum = elapsed;
        if (elapsed > stats.maximum) stats.maximum = elapsed;
        stats.last = elapsed;
        stats.cycles = stats.cycles + elapsed;
        stats.bytes = stats.bytes + bytes;
        stats.count = stats.count + 1;
        if (stage == RS485_PROFILE_TOTAL && stats.count >= 100000)
            rs485_profile_mode = 0;
    }
};
#else
inline void rs485_profile_init() {}
class Rs485ProfileScope {
public:
    explicit Rs485ProfileScope(Rs485ProfileStage) {}
    void setBytes(uint32_t) {}
};
#endif
