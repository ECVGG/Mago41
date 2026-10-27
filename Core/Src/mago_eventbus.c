/**
 * @file mago_eventbus.c
 * @brief Mago41 - Message Bus Implementation
 */

#include "mago_eventbus.h"
#include "mago_hal.h"
#include <string.h>

/* ============================================================================
 * INTERNAL STATE
 * ============================================================================ */

static mago_eventbus_t g_eventbus;

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

bool mago_eventbus_init(mago_eventbus_t* bus) {
    if (!bus) bus = &g_eventbus;
    memset(bus, 0, sizeof(mago_eventbus_t));

    // Init queue
    if (!mago_ring_init(&bus->queue, bus->queue_buffer, sizeof(bus->queue_buffer))) {
        return false;
    }

    // Init pool
    mago_pool_init(&bus->pool, bus->pool_blocks, sizeof(mago_event_t), MAGO_EVT_BUS_POOL_BLOCKS);

    return true;
}

/* ============================================================================
 * SUBSCRIPTION MANAGEMENT
 * ============================================================================ */

int mago_eventbus_subscribe(mago_eventbus_t* bus, mago_event_id_t id,
                            mago_event_cb_t cb, void* ctx) {
    if (!bus || !cb || id >= MAGO_EVT_COUNT) return -1;

    for (uint8_t i = 0; i < MAGO_EVT_BUS_MAX_SUBS; i++) {
        if (!bus->subscriptions[i].active) {
            bus->subscriptions[i] = (mago_event_sub_t){
                .id = id,
                .cb = cb,
                .ctx = ctx,
                .active = true
            };
            bus->sub_count++;
            return (int)i;
        }
    }
    return -1;  // No free slots
}

bool mago_eventbus_unsubscribe(mago_eventbus_t* bus, int handle) {
    if (!bus || handle < 0 || handle >= MAGO_EVT_BUS_MAX_SUBS) return false;
    if (!bus->subscriptions[handle].active) return false;

    bus->subscriptions[handle].active = false;
    bus->sub_count--;
    return true;
}

/* ============================================================================
 * EVENT POSTING
 * ============================================================================ */

static bool eventbus_post_internal(mago_eventbus_t* bus, const mago_event_t* evt, bool from_isr) {
    if (!bus || !evt) return false;

    // Try to push to queue
    if (!mago_ring_push(&bus->queue, evt, sizeof(mago_event_t))) {
        bus->events_dropped++;
        return false;
    }

    bus->events_posted++;
    return true;
}

bool mago_eventbus_post_isr(mago_eventbus_t* bus, const mago_event_t* evt) {
    return eventbus_post_internal(bus ? bus : &g_eventbus, evt, true);
}

bool mago_eventbus_post(mago_eventbus_t* bus, const mago_event_t* evt, uint32_t timeout_ms) {
    if (!bus) bus = &g_eventbus;

    mago_time_us_t start = MAGO_TIMER_GET_US();
    mago_time_us_t timeout_us = MAGO_MS_TO_US(timeout_ms);

    while (true) {
        if (eventbus_post_internal(bus, evt, false)) {
            return true;
        }

        if (MAGO_TIMER_GET_US() - start >= timeout_us) {
            return false;
        }

        // Brief yield
        MAGO_DELAY_US(100);
    }
}

bool mago_eventbus_post_simple(mago_eventbus_t* bus, mago_event_id_t id,
                               const void* data, size_t len, uint32_t timeout_ms) {
    mago_event_t evt = {
        .id = id,
        .timestamp = MAGO_TIMER_GET_US(),
        .data_len = (uint16_t)MAGO_MIN(len, 32)
    };
    if (data && len > 0) {
        memcpy(evt.payload.raw, data, evt.data_len);
    }
    return mago_eventbus_post(bus, &evt, timeout_ms);
}

/* ============================================================================
 * EVENT PROCESSING
 * ============================================================================ */

uint32_t mago_eventbus_process(mago_eventbus_t* bus, uint32_t max_events) {
    if (!bus) bus = &g_eventbus;
    if (!bus->sub_count) return 0;

    uint32_t processed = 0;
    mago_event_t evt;

    while (processed < max_events && mago_ring_pop(&bus->queue, &evt, sizeof(mago_event_t))) {
        // Dispatch to subscribers
        for (uint8_t i = 0; i < MAGO_EVT_BUS_MAX_SUBS; i++) {
            mago_event_sub_t* sub = &bus->subscriptions[i];
            if (sub->active && sub->id == evt.id) {
                sub->cb(&evt, sub->ctx);
                bus->events_dispatched++;
            }
        }
        processed++;
    }

    return processed;
}

/* ============================================================================
 * STATS
 * ============================================================================ */

void mago_eventbus_get_stats(const mago_eventbus_t* bus,
                             uint32_t* posted, uint32_t* dropped, uint32_t* dispatched) {
    if (!bus) bus = &g_eventbus;
    if (posted) *posted = bus->events_posted;
    if (dropped) *dropped = bus->events_dropped;
    if (dispatched) *dispatched = bus->events_dispatched;
}

/* ============================================================================
 * EVENT HELPERS
 * ============================================================================ */

mago_event_t mago_event_make_gps_fix(const mago_gps_nav_t* gps) {
    mago_event_t evt = {
        .id = MAGO_EVT_GPS_FIX,
        .timestamp = MAGO_TIMER_GET_US(),
        .data_len = sizeof(evt.payload.gps_fix)
    };
    if (gps) {
        evt.payload.gps_fix.lat = gps->lat_deg7 * 1e-7f;
        evt.payload.gps_fix.lon = gps->lon_deg7 * 1e-7f;
        evt.payload.gps_fix.alt = gps->alt_mm * 1e-3f;
        evt.payload.gps_fix.sats = gps->sats_used;
    }
    return evt;
}

mago_event_t mago_event_make_tx_done(uint8_t protocol, uint16_t len, bool success) {
    mago_event_t evt = {
        .id = success ? MAGO_EVT_TX_COMPLETE : MAGO_EVT_RADIO_TX_FAILED,
        .timestamp = MAGO_TIMER_GET_US(),
        .data_len = 4
    };
    evt.payload.tx_done.protocol = protocol;
    evt.payload.tx_done.len = len;
    return evt;
}

mago_event_t mago_event_make_rx_packet(uint8_t protocol, uint8_t* data, uint16_t len, int8_t rssi) {
    mago_event_t evt = {
        .id = MAGO_EVT_RX_PACKET,
        .timestamp = MAGO_TIMER_GET_US(),
        .data_len = sizeof(evt.payload.rx_packet)
    };
    evt.payload.rx_packet.data = data;
    evt.payload.rx_packet.len = len;
    evt.payload.rx_packet.rssi = rssi;
    return evt;
}

mago_event_t mago_event_make_battery(uint16_t mv, int16_t temp) {
    mago_event_t evt = {
        .id = (mv < 3200) ? MAGO_EVT_CRITICAL_BATTERY : MAGO_EVT_LOW_BATTERY,
        .timestamp = MAGO_TIMER_GET_US(),
        .data_len = sizeof(evt.payload.battery)
    };
    evt.payload.battery.mv = mv;
    evt.payload.battery.temp = temp;
    return evt;
}

mago_event_t mago_event_make_error(uint32_t code, const char* msg) {
    mago_event_t evt = {
        .id = MAGO_EVT_WATCHDOG,
        .timestamp = MAGO_TIMER_GET_US(),
        .data_len = sizeof(evt.payload.error)
    };
    evt.payload.error.code = code;
    evt.payload.error.msg = msg;
    return evt;
}

mago_event_t mago_event_make_button(bool long_press) {
    mago_event_t evt = {
        .id = long_press ? MAGO_EVT_BUTTON_LONG : MAGO_EVT_BUTTON_PRESS,
        .timestamp = MAGO_TIMER_GET_US(),
        .data_len = 0
    };
    return evt;
}

/* ============================================================================
 * GLOBAL ACCESS
 * ============================================================================ */

mago_eventbus_t* mago_eventbus_global(void) {
    return &g_eventbus;
}