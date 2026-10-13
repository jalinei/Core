/* SPDX-License-Identifier: LGPL-2.1 */
#include "communication_dispatch.h"
#include "Rs485.h"
#include <string.h>
#include "rs485_profile.h"

#ifdef RS485_DISPATCH_PROFILE
volatile Rs485ProfileStats rs485_profile_stats[RS485_PROFILE_STAGE_COUNT];
volatile uint32_t rs485_profile_mode;
#endif

static uint8_t *rx_ring;
static uint8_t *rx_snapshot;
static uint16_t ring_size;
static uint16_t read_position;
static uint16_t received_size;
static uint32_t dispatch_errors;

void communication_dispatch_reset()
{
    rx_ring = nullptr;
    rx_snapshot = nullptr;
    ring_size = 0;
    read_position = 0;
    received_size = 0;
    dispatch_errors = RS485_ERROR_NONE;
}

void communication_dispatch_configure(uint8_t *ring, uint8_t *snapshot,
                                      uint16_t size)
{
    communication_dispatch_reset();
    rx_ring = ring;
    rx_snapshot = snapshot;
    ring_size = size;
}

uint16_t communication_dispatch_received_size()
{
    return received_size;
}

uint32_t communication_dispatch_errors()
{
    return dispatch_errors;
}

void communication_dispatch()
{
    // Empty scope measures the timestamp/scoping floor, outside total timing.
    { Rs485ProfileScope clock(RS485_PROFILE_CLOCK); }
    Rs485ProfileScope total(RS485_PROFILE_TOTAL);
    if (ring_size == 0)
        return;

    {
        Rs485ProfileScope profile(RS485_PROFILE_POLL_BEFORE);
        dispatch_errors = serial_poll_errors();
    }
    // serial_poll_errors() already polls TX completion. The ring is configured
    // only in synchronous mode, so a second completion poll here is redundant.
    received_size = 0;
    if (dispatch_errors & RS485_RX_ERRORS)
    {
        read_position = 0;
        return;
    }
    uint16_t write_position;
    {
        Rs485ProfileScope profile(RS485_PROFILE_POSITION);
        write_position = serial_rx_write_position();
    }
    /* DMA keeps receiving. The caller must size the ring so it cannot lap
     * this read position between dispatches or during either copy.
     */
    __DMB();
    const uint16_t available = write_position >= read_position ?
        write_position - read_position : ring_size - read_position + write_position;
    const uint16_t tail = ring_size - read_position;
    const uint16_t first = available < tail ? available : tail;
    {
        Rs485ProfileScope profile(RS485_PROFILE_COPY);
        profile.setBytes(available);
        memcpy(rx_snapshot, rx_ring + read_position, first);
        if (available > first)
            memcpy(rx_snapshot + first, rx_ring, available - first);
    }
    total.setBytes(available);
    /* Also reject errors arriving while the DMA ring was being copied. */
    {
        Rs485ProfileScope profile(RS485_PROFILE_POLL_AFTER);
        dispatch_errors |= serial_poll_errors();
    }
    if (dispatch_errors & RS485_RX_ERRORS)
    {
        read_position = 0;
        return;
    }
    read_position = write_position;
    received_size = available;
}
