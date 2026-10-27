/**
 * @file mago_eventbus.h
 * @brief Mago41 - Message Bus / Event System
 *
 * Event-driven architecture - core diferencia vs RS41HUP_V2
 * No polling loops, todo pasa por eventos tipados
 * Lock-free SPSC queues para comunicación ISR->Task
 */

#ifndef MAGO_EVENTBUS_H
#define MAGO_EVENTBUS_H

#include <stdint.h>
#include <stdbool.h>
#include "mago_types.h"

/* ============================================================================
 * EVENT BUS CONFIGURATION
 * ============================================================================ */

#define MAGO_EVT_BUS_MAX_SUBS     32
#define MAGO_EVT_BUS_QUEUE_DEPTH  64
#define MAGO_EVT_BUS_POOL_BLOCKS  16

/* ============================================================================
 * EVENT BUS STATE
 * ============================================================================ */

typedef struct {
    mago_event_sub_t subscriptions[MAGO_EVT_BUS_MAX_SUBS];
    uint8_t sub_count;

    // Event queue for ISR -> Task
    mago_ring_t queue;
    uint8_t queue_buffer[MAGO_EVT_BUS_QUEUE_DEPTH * sizeof(mago_event_t)];

    // Memory pool for dynamic events
    mago_pool_t pool;
    mago_event_t pool_blocks[MAGO_EVT_BUS_POOL_BLOCKS];

    // Stats
    uint32_t events_posted;
    uint32_t events_dropped;
    uint32_t events_dispatched;
} mago_eventbus_t;

/* ============================================================================
 * API
 * ============================================================================ */

// Initialize event bus
bool mago_eventbus_init(mago_eventbus_t* bus);

// Subscribe to event (returns subscription handle)
int mago_eventbus_subscribe(mago_eventbus_t* bus, mago_event_id_t id,
                            mago_event_cb_t cb, void* ctx);

// Unsubscribe
bool mago_eventbus_unsubscribe(mago_eventbus_t* bus, int handle);

// Post event from ISR (non-blocking, may drop if full)
bool mago_eventbus_post_isr(mago_eventbus_t* bus, const mago_event_t* evt);

// Post event from task (blocking with timeout)
bool mago_eventbus_post(mago_eventbus_t* bus, const mago_event_t* evt, uint32_t timeout_ms);

// Post simple event (helper)
bool mago_eventbus_post_simple(mago_eventbus_t* bus, mago_event_id_t id,
                               const void* data, size_t len, uint32_t timeout_ms);

// Process queued events (call from main loop)
uint32_t mago_eventbus_process(mago_eventbus_t* bus, uint32_t max_events);

// Get stats
void mago_eventbus_get_stats(const mago_eventbus_t* bus,
                             uint32_t* posted, uint32_t* dropped, uint32_t* dispatched);

// Helper: Create common events
mago_event_t mago_event_make_gps_fix(const mago_gps_nav_t* gps);
mago_event_t mago_event_make_tx_done(uint8_t protocol, uint16_t len, bool success);
mago_event_t mago_event_make_rx_packet(uint8_t protocol, uint8_t* data, uint16_t len, int8_t rssi);
mago_event_t mago_event_make_battery(uint16_t mv, int16_t temp);
mago_event_t mago_event_make_error(uint32_t code, const char* msg);
mago_event_t mago_event_make_button(bool long_press);

#endif // MAGO_EVENTBUS_H