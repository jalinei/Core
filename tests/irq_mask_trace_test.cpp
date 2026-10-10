#include <assert.h>
#include "IrqMaskTraceState.h"

int main()
{
    IrqMaskTraceState trace;
    trace.begin(UINT32_MAX - 100, 0x1234, 1, 15);
    trace.begin(UINT32_MAX - 80, 0x9999, 2, 14);
    assert(trace.current.caller == 0x1234);
    assert(trace.current.kind == 3);
    assert(!trace.restore(20, 32, 0, 0x2222, 1));
    assert(!trace.restore(25, 0, 1, 0x2222, 1));
    assert(trace.restore(40, 0, 0, 0x2222, 1));
    assert(trace.latest.end - trace.latest.start == 141);
    assert(trace.latest.pending == 1);
    assert(trace.latest.ipsr == 15);
    assert(trace.latest.restore_site == 0x2222);
    assert(!trace.restore(50, 0, 0, 0, 0));
    assert(trace.sequence == 1);
    trace.begin(100, 0x4567, 4, 0);
    trace.sleep_begin(110);
    trace.sleep_end(210);
    assert(trace.restore(220, 0, 0, 0x4568, 1));
    assert(trace.latest.sleep_cycles == 100);
    assert(trace.latest.end - trace.latest.start - trace.latest.sleep_cycles == 20);
    assert(trace.sequence == 2);
}
