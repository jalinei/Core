#ifndef OWNTECH_IRQ_MASK_TRACE_STATE_H_
#define OWNTECH_IRQ_MASK_TRACE_STATE_H_
#include <stdint.h>

struct IrqMaskInterval {
    uint32_t start;
    uint32_t end;
    uint32_t caller;
    uint32_t restore_site;
    uint32_t sleep_cycles;
    uint32_t kind;
    uint32_t ipsr;
    uint32_t pending;
};

class IrqMaskTraceState {
public:
    bool open = false;
    bool sleeping = false;
    bool latest_valid = false;
    IrqMaskInterval current = {};
    IrqMaskInterval latest = {};
    uint32_t sequence = 0;

    void begin(uint32_t now, uint32_t caller, uint32_t kind, uint32_t ipsr) {
        if (!open) {
            current = {now, 0, caller, 0, 0, kind, ipsr, 0};
            open = true;
        } else {
            current.kind |= kind;
        }
    }

    void sleep_begin(uint32_t now) {
        if (!open) return;
        sleep_start = now;
        sleeping = true;
        current.kind |= 4;
    }

    void sleep_end(uint32_t now) {
        if (sleeping) current.sleep_cycles += now - sleep_start;
        sleeping = false;
    }

    bool restore(uint32_t now, uint32_t basepri, uint32_t primask,
                 uint32_t site, uint32_t pending) {
        if (!open || basepri || primask) return false;
        sleep_end(now);
        current.end = now;
        current.restore_site = site;
        current.pending = pending;
        latest = current;
        latest_valid = true;
        open = false;
        ++sequence;
        return true;
    }

private:
    uint32_t sleep_start = 0;
};
#endif
