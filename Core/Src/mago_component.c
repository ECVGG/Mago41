/**
 * @file mago_component.c
 * @brief Mago41 - Component System Implementation
 */

#include "mago_component.h"
#include "mago_eventbus.h"
#include "mago_hal.h"
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * GLOBAL REGISTRY
 * ============================================================================ */

static mago_component_registry_t g_registry;

/* ============================================================================
 * RADIO COMPONENT IMPLEMENTATION
 * ============================================================================ */

static mago_result_t radio_init(mago_component_t* c, const void* cfg) {
    mago_radio_comp_data_t* d = (mago_radio_comp_data_t*)c->private_data;
    const mago_radio_cfg_t* rcfg = (const mago_radio_cfg_t*)cfg;
    if (!d || !rcfg) return MAGO_ERR_INVAL;

    memset(d, 0, sizeof(mago_radio_comp_data_t));

    // Init HAL radio ops
    if (!MAGO_GPIO_INIT(&g_mago_pins.radio_cs, MAGO_GPIO_OUTPUT_PP, MAGO_GPIO_PULL_UP, MAGO_GPIO_SPEED_HIGH)) return MAGO_ERR_HW;
    if (!MAGO_GPIO_INIT(&g_mago_pins.radio_sck, MAGO_GPIO_AF_PP, MAGO_GPIO_PULL_NONE, MAGO_GPIO_SPEED_HIGH)) return MAGO_ERR_HW;
    if (!MAGO_GPIO_INIT(&g_mago_pins.radio_mosi, MAGO_GPIO_AF_PP, MAGO_GPIO_PULL_NONE, MAGO_GPIO_SPEED_HIGH)) return MAGO_ERR_HW;
    if (!MAGO_GPIO_INIT(&g_mago_pins.radio_miso, MAGO_GPIO_INPUT, MAGO_GPIO_PULL_NONE, MAGO_GPIO_SPEED_HIGH)) return MAGO_ERR_HW;

    MAGO_GPIO_WRITE(&g_mago_pins.radio_cs, true);

    // SPI init
    // d->ops = *mago_hal_get()->spi;  // Would get from HAL

    c->last_error = MAGO_OK;
    return MAGO_OK;
}

static mago_result_t radio_start(mago_component_t* c) {
    mago_radio_comp_data_t* d = (mago_radio_comp_data_t*)c->private_data;
    // Set initial mode
    d->current_mode = MAGO_RADIO_MODE_STANDBY;
    return MAGO_OK;
}

static mago_result_t radio_tick(mago_component_t* c, mago_time_us_t now) {
    // Radio state machine handled by protocol manager
    return MAGO_OK;
}

static mago_result_t radio_event(mago_component_t* c, const mago_event_t* evt) {
    // Handle radio-specific events
    return MAGO_OK;
}

static void radio_deinit(mago_component_t* c) {
    // Cleanup
}

/* ============================================================================
 * GPS COMPONENT IMPLEMENTATION
 * ============================================================================ */

static mago_result_t gps_init(mago_component_t* c, const void* cfg) {
    mago_gps_comp_data_t* d = (mago_gps_comp_data_t*)c->private_data;
    const mago_gps_cfg_t* gcfg = (const mago_gps_cfg_t*)cfg;
    if (!d || !gcfg) return MAGO_ERR_INVAL;

    memset(d, 0, sizeof(mago_gps_comp_data_t));

    // Init UART for GPS
    if (!MAGO_GPIO_INIT(&g_mago_pins.gps_tx, MAGO_GPIO_AF_PP, MAGO_GPIO_PULL_UP, MAGO_GPIO_SPEED_HIGH)) return MAGO_ERR_HW;
    if (!MAGO_GPIO_INIT(&g_mago_pins.gps_rx, MAGO_GPIO_INPUT, MAGO_GPIO_PULL_UP, MAGO_GPIO_SPEED_HIGH)) return MAGO_ERR_HW;
    if (!MAGO_GPIO_INIT(&g_mago_pins.gps_rst, MAGO_GPIO_OUTPUT_PP, MAGO_GPIO_PULL_NONE, MAGO_GPIO_SPEED_LOW)) return MAGO_ERR_HW;

    // Configure UBX messages, baudrate, etc.
    return MAGO_OK;
}

static mago_result_t gps_start(mago_component_t* c) {
    mago_gps_comp_data_t* d = (mago_gps_comp_data_t*)c->private_data;
    // Reset GPS, configure
    return MAGO_OK;
}

static mago_result_t gps_tick(mago_component_t* c, mago_time_us_t now) {
    mago_gps_comp_data_t* d = (mago_gps_comp_data_t*)c->private_data;
    // Parse UBX stream (called from UART RX callback ideally)
    // Update fix age
    if (d->has_fix) {
        d->fix_age_ms = (uint32_t)((now - d->last_itow * 1000ULL) / 1000ULL);
    }
    return MAGO_OK;
}

static mago_result_t gps_event(mago_component_t* c, const mago_event_t* evt) {
    return MAGO_OK;
}

static void gps_deinit(mago_component_t* c) {}

/* ============================================================================
 * POWER COMPONENT IMPLEMENTATION
 * ============================================================================ */

static mago_result_t power_init(mago_component_t* c, const void* cfg) {
    mago_power_comp_data_t* d = (mago_power_comp_data_t*)c->private_data;
    const mago_power_cfg_t* pcfg = (const mago_power_cfg_t*)cfg;
    if (!d || !pcfg) return MAGO_ERR_INVAL;

    memset(d, 0, sizeof(mago_power_comp_data_t));

    // Init ADC
    if (!MAGO_GPIO_INIT(&g_mago_pins.adc_batt, MAGO_GPIO_ANALOG, MAGO_GPIO_PULL_NONE, MAGO_GPIO_SPEED_LOW)) return MAGO_ERR_HW;
    if (!MAGO_GPIO_INIT(&g_mago_pins.adc_temp, MAGO_GPIO_ANALOG, MAGO_GPIO_PULL_NONE, MAGO_GPIO_SPEED_LOW)) return MAGO_ERR_HW;

    // ADC init for battery + temp
    return MAGO_OK;
}

static mago_result_t power_start(mago_component_t* c) {
    return MAGO_OK;
}

static mago_result_t power_tick(mago_component_t* c, mago_time_us_t now) {
    mago_power_comp_data_t* d = (mago_power_comp_data_t*)c->private_data;
    // Read battery voltage and temperature via ADC
    // Update sleep/active time counters
    return MAGO_OK;
}

static mago_result_t power_event(mago_component_t* c, const mago_event_t* evt) {
    if (evt->id == MAGO_EVT_LOW_BATTERY || evt->id == MAGO_EVT_CRITICAL_BATTERY) {
        mago_power_comp_data_t* d = (mago_power_comp_data_t*)c->private_data;
        d->battery_mv = evt->payload.battery.mv;
        d->temp_c = evt->payload.battery.temp;
        if (evt->id == MAGO_EVT_CRITICAL_BATTERY) d->critical_batt = true;
        else d->low_batt_warned = true;
    }
    return MAGO_OK;
}

static void power_deinit(mago_component_t* c) {}

/* ============================================================================
 * WATCHDOG COMPONENT
 * ============================================================================ */

static mago_result_t watchdog_init(mago_component_t* c, const void* cfg) {
    mago_watchdog_comp_data_t* d = (mago_watchdog_comp_data_t*)c->private_data;
    uint32_t* timeout = (uint32_t*)cfg;
    if (!d) return MAGO_ERR_INVAL;

    d->timeout_ms = timeout ? *timeout : 8000;
    d->last_kick = MAGO_TIMER_GET_US() / 1000;
    d->enabled = true;
    d->kick_count = 0;

    // Init HAL watchdog
    return MAGO_OK;
}

static mago_result_t watchdog_tick(mago_component_t* c, mago_time_us_t now) {
    mago_watchdog_comp_data_t* d = (mago_watchdog_comp_data_t*)c->private_data;
    if (!d->enabled) return MAGO_OK;

    uint32_t now_ms = (uint32_t)(now / 1000);
    if (now_ms - d->last_kick >= d->timeout_ms) {
        // Watchdog expired - trigger reset
        return MAGO_ERR_TIMEOUT;
    }
    return MAGO_OK;
}

static mago_result_t watchdog_event(mago_component_t* c, const mago_event_t* evt) {
    if (evt->id == MAGO_EVT_WATCHDOG) {
        mago_watchdog_comp_data_t* d = (mago_watchdog_comp_data_t*)c->private_data;
        d->last_kick = (uint32_t)(evt->timestamp / 1000);
        d->kick_count++;
    }
    return MAGO_OK;
}

static void watchdog_deinit(mago_component_t* c) {}

/* ============================================================================
 * COMPONENT FACTORIES
 * ============================================================================ */

// Static storage for component private data
static mago_radio_comp_data_t g_radio_data;
static mago_gps_comp_data_t g_gps_data;
static mago_power_comp_data_t g_power_data;
static mago_proto_mgr_data_t g_proto_mgr_data;
static mago_telemetry_comp_data_t g_telemetry_data;
static mago_watchdog_comp_data_t g_watchdog_data;

mago_component_t* mago_component_create_radio(const mago_radio_cfg_t* cfg) {
    static mago_component_t comp = {
        .name = "radio",
        .priority = 10,
        .private_data = &g_radio_data,
        .private_size = sizeof(g_radio_data),
        .init = radio_init,
        .start = radio_start,
        .tick = radio_tick,
        .event = radio_event,
        .deinit = radio_deinit,
    };
    return &comp;
}

mago_component_t* mago_component_create_gps(const mago_gps_cfg_t* cfg) {
    static mago_component_t comp = {
        .name = "gps",
        .priority = 20,
        .private_data = &g_gps_data,
        .private_size = sizeof(g_gps_data),
        .init = gps_init,
        .start = gps_start,
        .tick = gps_tick,
        .event = gps_event,
        .deinit = gps_deinit,
    };
    return &comp;
}

mago_component_t* mago_component_create_power(const mago_power_cfg_t* cfg) {
    static mago_component_t comp = {
        .name = "power",
        .priority = 5,  // Init early
        .private_data = &g_power_data,
        .private_size = sizeof(g_power_data),
        .init = power_init,
        .start = power_start,
        .tick = power_tick,
        .event = power_event,
        .deinit = power_deinit,
    };
    return &comp;
}

mago_component_t* mago_component_create_proto_mgr(const mago_system_config_t* cfg) {
    static mago_component_t comp = {
        .name = "protocol_mgr",
        .priority = 30,
        .private_data = &g_proto_mgr_data,
        .private_size = sizeof(g_proto_mgr_data),
        // init/start/tick/event/deinit to be implemented
    };
    return &comp;
}

mago_component_t* mago_component_create_telemetry(void) {
    static mago_component_t comp = {
        .name = "telemetry",
        .priority = 40,
        .private_data = &g_telemetry_data,
        .private_size = sizeof(g_telemetry_data),
    };
    return &comp;
}

mago_component_t* mago_component_create_watchdog(uint32_t timeout_ms) {
    static mago_component_t comp = {
        .name = "watchdog",
        .priority = 1,  // Init first
        .private_data = &g_watchdog_data,
        .private_size = sizeof(g_watchdog_data),
        .init = watchdog_init,
        .tick = watchdog_tick,
        .event = watchdog_event,
        .deinit = watchdog_deinit,
    };
    // Store timeout in private data
    g_watchdog_data.timeout_ms = timeout_ms;
    return &comp;
}

/* ============================================================================
 * REGISTRY IMPLEMENTATION
 * ============================================================================ */

bool mago_components_init(mago_component_registry_t* reg) {
    if (!reg) reg = &g_registry;
    memset(reg, 0, sizeof(mago_component_registry_t));
    reg->initialized = true;
    return true;
}

bool mago_components_register(mago_component_registry_t* reg, const mago_component_t* comp) {
    if (!reg || !comp || reg->count >= MAGO_MAX_COMPONENTS) return false;

    // Insert sorted by priority
    uint8_t pos = reg->count;
    for (uint8_t i = 0; i < reg->count; i++) {
        if (comp->priority < reg->components[i].priority) {
            pos = i;
            break;
        }
    }

    // Shift
    for (uint8_t i = reg->count; i > pos; i--) {
        reg->components[i] = reg->components[i-1];
    }

    reg->components[pos] = *comp;
    reg->count++;
    return true;
}

bool mago_components_unregister(mago_component_registry_t* reg, const char* name) {
    if (!reg || !name) return false;
    for (uint8_t i = 0; i < reg->count; i++) {
        if (strcmp(reg->components[i].name, name) == 0) {
            for (uint8_t j = i; j < reg->count - 1; j++) {
                reg->components[j] = reg->components[j+1];
            }
            reg->count--;
            return true;
        }
    }
    return false;
}

mago_result_t mago_components_init_all(mago_component_registry_t* reg, const mago_system_config_t* cfg) {
    if (!reg) reg = &g_registry;
    if (!reg->initialized) return MAGO_ERR_STATE;

    for (uint8_t i = 0; i < reg->count; i++) {
        mago_component_t* c = &reg->components[i];
        if (c->init) {
            c->last_error = c->init(c, cfg);
            if (MAGO_FAILED(c->last_error)) return c->last_error;
            c->initialized = true;
        }
    }
    return MAGO_OK;
}

mago_result_t mago_components_start_all(mago_component_registry_t* reg) {
    if (!reg) reg = &g_registry;
    for (uint8_t i = 0; i < reg->count; i++) {
        mago_component_t* c = &reg->components[i];
        if (c->start && c->initialized) {
            c->last_error = c->start(c);
            if (MAGO_FAILED(c->last_error)) return c->last_error;
            c->running = true;
        }
    }
    return MAGO_OK;
}

mago_result_t mago_components_stop_all(mago_component_registry_t* reg) {
    if (!reg) reg = &g_registry;
    for (int i = reg->count - 1; i >= 0; i--) {
        mago_component_t* c = &reg->components[i];
        if (c->stop && c->running) {
            c->last_error = c->stop(c);
            if (MAGO_FAILED(c->last_error)) return c->last_error;
            c->running = false;
        }
    }
    return MAGO_OK;
}

void mago_components_deinit_all(mago_component_registry_t* reg) {
    if (!reg) reg = &g_registry;
    for (int i = reg->count - 1; i >= 0; i--) {
        mago_component_t* c = &reg->components[i];
        if (c->deinit && c->initialized) {
            c->deinit(c);
            c->initialized = false;
        }
    }
    reg->count = 0;
}

mago_result_t mago_components_tick_all(mago_component_registry_t* reg, mago_time_us_t now) {
    if (!reg) reg = &g_registry;
    for (uint8_t i = 0; i < reg->count; i++) {
        mago_component_t* c = &reg->components[i];
        if (c->tick && c->running) {
            c->last_error = c->tick(c, now);
            if (MAGO_FAILED(c->last_error)) return c->last_error;
        }
    }
    return MAGO_OK;
}

void mago_components_dispatch_event(mago_component_registry_t* reg, const mago_event_t* evt) {
    if (!reg || !evt) return;
    for (uint8_t i = 0; i < reg->count; i++) {
        mago_component_t* c = &reg->components[i];
        if (c->event && c->initialized) {
            c->event(c, evt);
        }
    }
}

mago_component_t* mago_components_find(mago_component_registry_t* reg, const char* name) {
    if (!reg || !name) return NULL;
    for (uint8_t i = 0; i < reg->count; i++) {
        if (strcmp(reg->components[i].name, name) == 0) {
            return &reg->components[i];
        }
    }
    return NULL;
}

mago_component_t* mago_components_find_by_type(mago_component_registry_t* reg, uint8_t type) {
    // Not implemented - would need type field
    return NULL;
}

/* ============================================================================
 * GLOBAL ACCESS
 * ============================================================================ */

mago_component_registry_t* mago_components_global(void) {
    return &g_registry;
}