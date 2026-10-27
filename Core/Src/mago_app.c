/**
 * @file mago_app.c
 * @brief Mago41 - Main Application Implementation
 *
 * Event-driven architecture - NO polling loop like RS41HUP_V2
 * Everything communicates via typed message bus
 */

#include "mago_app.h"
#include "mago_hal.h"
#include "mago_component.h"
#include "mago_eventbus.h"
#include "mago_config.h"
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * INTERNAL STATE
 * ============================================================================ */

static mago_app_t g_app;

/* ============================================================================
 * BOARD PINS (weak - override in board.c)
 * ============================================================================ */

__attribute__((weak)) mago_board_pins_t g_mago_pins = {
    .led_status   = {.port = 'C', .pin = 13, .mode = MAGO_GPIO_OUTPUT_PP, .speed = MAGO_GPIO_SPEED_LOW},
    .led_error    = {.port = 'C', .pin = 14, .mode = MAGO_GPIO_OUTPUT_PP, .speed = MAGO_GPIO_SPEED_LOW},
    .radio_cs     = {.port = 'B', .pin = 12, .mode = MAGO_GPIO_OUTPUT_PP, .speed = MAGO_GPIO_SPEED_HIGH},
    .radio_sck    = {.port = 'B', .pin = 13, .mode = MAGO_GPIO_AF_PP, .speed = MAGO_GPIO_SPEED_HIGH},
    .radio_mosi   = {.port = 'B', .pin = 15, .mode = MAGO_GPIO_AF_PP, .speed = MAGO_GPIO_SPEED_HIGH},
    .radio_miso   = {.port = 'B', .pin = 14, .mode = MAGO_GPIO_INPUT, .speed = MAGO_GPIO_SPEED_HIGH},
    .radio_sdn    = {.port = 'B', .pin = 0,  .mode = MAGO_GPIO_OUTPUT_PP, .speed = MAGO_GPIO_SPEED_LOW},
    .radio_nirq   = {.port = 'B', .pin = 1,  .mode = MAGO_GPIO_IT_BOTH, .speed = MAGO_GPIO_SPEED_HIGH},
    .gps_tx       = {.port = 'A', .pin = 9,  .mode = MAGO_GPIO_AF_PP, .speed = MAGO_GPIO_SPEED_HIGH},
    .gps_rx       = {.port = 'A', .pin = 10, .mode = MAGO_GPIO_INPUT, .speed = MAGO_GPIO_SPEED_HIGH},
    .gps_rst      = {.port = 'A', .pin = 15, .mode = MAGO_GPIO_OUTPUT_PP, .speed = MAGO_GPIO_SPEED_LOW},
    .debug_tx     = {.port = 'A', .pin = 2,  .mode = MAGO_GPIO_AF_PP, .speed = MAGO_GPIO_SPEED_HIGH},
    .debug_rx     = {.port = 'A', .pin = 3,  .mode = MAGO_GPIO_INPUT, .speed = MAGO_GPIO_SPEED_HIGH},
    .adc_batt     = {.port = 'A', .pin = 0,  .mode = MAGO_GPIO_ANALOG},
    .adc_temp     = {.port = 'A', .pin = 1,  .mode = MAGO_GPIO_ANALOG},
    .btn_user     = {.port = 'C', .pin = 13, .mode = MAGO_GPIO_IT_BOTH, .speed = MAGO_GPIO_SPEED_LOW},
};

/* ============================================================================
 * ERROR HANDLING
 * ============================================================================ */

static void app_fatal_error(mago_app_t* app, uint32_t code, const char* msg) {
    app->state = MAGO_APP_STATE_ERROR;
    app->fatal_error = true;
    app->last_error_code = code;
    app->last_error_time = MAGO_TIMER_GET_US();
    app->error_count++;

    mago_event_t evt = mago_event_make_error(code, msg);
    mago_eventbus_post_isr(&app->eventbus, &evt);

    mago_app_on_fatal_error(code, msg);

    // Blink error LED forever
    while (1) {
        MAGO_GPIO_TOGGLE(&g_mago_pins.led_error);
        MAGO_DELAY_MS(200);
    }
}

static void app_change_state(mago_app_t* app, mago_app_state_t new_state) {
    if (app->state != new_state) {
        app->prev_state = app->state;
        app->state = new_state;
        mago_app_on_state_change(app->prev_state, new_state);

        mago_event_t evt = {
            .id = MAGO_EVT_CONFIG_CHANGED,
            .timestamp = MAGO_TIMER_GET_US(),
            .data_len = 2
        };
        evt.payload.raw[0] = app->prev_state;
        evt.payload.raw[1] = new_state;
        mago_eventbus_post_isr(&app->eventbus, &evt);
    }
}

/* ============================================================================
 * EVENT HANDLERS
 * ============================================================================ */

static void handle_gps_fix(const mago_event_t* evt, void* ctx) {
    mago_app_t* app = (mago_app_t*)ctx;
    app->gps_fixes++;

    // Update telemetry with fresh GPS
    mago_component_t* telem = mago_components_find(&app->components, "telemetry");
    if (telem) {
        mago_telemetry_comp_data_t* td = (mago_telemetry_comp_data_t*)telem->private_data;
        td->dirty = true;
    }
}

static void handle_tx_done(const mago_event_t* evt, void* ctx) {
    mago_app_t* app = (mago_app_t*)ctx;
    if (evt->id == MAGO_EVT_TX_COMPLETE) {
        app->tx_success++;
    }
    app->tx_attempts++;
}

static void handle_battery(const mago_event_t* evt, void* ctx) {
    mago_app_t* app = (mago_app_t*)ctx;
    if (evt->id == MAGO_EVT_CRITICAL_BATTERY) {
        app_change_state(app, MAGO_APP_STATE_ERROR);
    } else if (evt->id == MAGO_EVT_LOW_BATTERY) {
        // Reduce TX power, increase interval
        app->config.radio.power_dbm = MAGO_MAX(app->config.radio.power_dbm - 3, -10);
    }
}

static void handle_button(const mago_event_t* evt, void* ctx) {
    mago_app_t* app = (mago_app_t*)ctx;
    if (evt->id == MAGO_EVT_BUTTON_LONG) {
        if (app->config.allow_button_disable) {
            app_change_state(app, MAGO_APP_STATE_SHUTDOWN);
        }
    } else {
        // Short press - force TX
        mago_eventbus_post_simple(&app->eventbus, MAGO_EVT_TX_REQUEST, NULL, 0, 100);
    }
}

static void handle_sleep_request(const mago_event_t* evt, void* ctx) {
    mago_app_t* app = (mago_app_t*)ctx;
    app->sleep_requested = true;
    app->sleep_minutes = *(uint16_t*)evt->payload.raw;
}

/* ============================================================================
 * STATE MACHINE
 * ============================================================================ */

static mago_result_t state_boot(mago_app_t* app, mago_time_us_t now) {
    // Early hardware init
    if (!MAGO_GPIO_INIT(&g_mago_pins.led_status, MAGO_GPIO_OUTPUT_PP, MAGO_GPIO_PULL_NONE, MAGO_GPIO_SPEED_LOW)) {
        return MAGO_ERR_HW;
    }
    MAGO_GPIO_WRITE(&g_mago_pins.led_status, true);

    app_change_state(app, MAGO_APP_STATE_INIT);
    return MAGO_OK;
}

static mago_result_t state_init(mago_app_t* app, mago_time_us_t now) {
    // Initialize event bus
    if (!mago_eventbus_init(&app->eventbus)) {
        return MAGO_ERR_NOMEM;
    }

    // Initialize components
    if (!mago_components_init(&app->components)) {
        return MAGO_ERR_NOMEM;
    }

    // Register built-in components
    mago_components_register(&app->components, mago_component_create_power(&app->config.power));
    mago_components_register(&app->components, mago_component_create_radio(&app->config.radio));
    mago_components_register(&app->components, mago_component_create_gps(&app->config.gps));
    mago_components_register(&app->components, mago_component_create_proto_mgr(&app->config));
    mago_components_register(&app->components, mago_component_create_telemetry());
    mago_components_register(&app->components, mago_component_create_watchdog(app->config.watchdog_timeout_ms));

    // Subscribe to events
    mago_eventbus_subscribe(&app->eventbus, MAGO_EVT_GPS_FIX, handle_gps_fix, app);
    mago_eventbus_subscribe(&app->eventbus, MAGO_EVT_TX_COMPLETE, handle_tx_done, app);
    mago_eventbus_subscribe(&app->eventbus, MAGO_EVT_RADIO_TX_FAILED, handle_tx_done, app);
    mago_eventbus_subscribe(&app->eventbus, MAGO_EVT_LOW_BATTERY, handle_battery, app);
    mago_eventbus_subscribe(&app->eventbus, MAGO_EVT_CRITICAL_BATTERY, handle_battery, app);
    mago_eventbus_subscribe(&app->eventbus, MAGO_EVT_BUTTON_PRESS, handle_button, app);
    mago_eventbus_subscribe(&app->eventbus, MAGO_EVT_BUTTON_LONG, handle_button, app);
    mago_eventbus_subscribe(&app->eventbus, MAGO_EVT_SLEEP_REQUEST, handle_sleep_request, app);

    // Init all components
    mago_result_t res = mago_components_init_all(&app->components, &app->config);
    if (MAGO_FAILED(res)) return res;

    // Start all components
    res = mago_components_start_all(&app->components);
    if (MAGO_FAILED(res)) return res;

    app->boot_time = now;
    app_change_state(app, MAGO_APP_STATE_RUNNING);
    return MAGO_OK;
}

static mago_result_t state_running(mago_app_t* app, mago_time_us_t now) {
    // Process events
    uint32_t processed = mago_eventbus_process(&app->eventbus, 32);
    app->events_processed += processed;

    // Tick components
    mago_result_t res = mago_components_tick_all(&app->components, now);
    if (MAGO_FAILED(res)) return res;

    // Check sleep request
    if (app->sleep_requested) {
        app_change_state(app, MAGO_APP_STATE_SLEEP_PREP);
    }

    // Status report every 30s
    static mago_time_us_t last_status = 0;
    if (now - last_status >= MAGO_SEC_TO_US(30)) {
        mago_app_on_status_report(app);
        last_status = now;
    }

    // Kick watchdog
    mago_eventbus_post_simple(&app->eventbus, MAGO_EVT_WATCHDOG, NULL, 0, 0);

    return MAGO_OK;
}

static mago_result_t state_sleep_prep(mago_app_t* app, mago_time_us_t now) {
    // Stop components gracefully
    mago_components_stop_all(&app->components);

    // Notify
    mago_app_on_pre_sleep(app->sleep_minutes);

    // Configure wakeup
    // RTC alarm, button, etc.

    app_change_state(app, MAGO_APP_STATE_SLEEPING);
    return MAGO_OK;
}

static mago_result_t state_sleeping(mago_app_t* app, mago_time_us_t now) {
    // Enter sleep mode
    MAGO_DELAY_MS(10);  // Allow UART to flush
    MAGO_DELAY_MS(100);

    // This will block until wakeup
    MAGO_HAL_GET()->pwr->enter_sleep(MAGO_SLEEP_MODE_STOP);

    app_change_state(app, MAGO_APP_STATE_WAKING);
    return MAGO_OK;
}

static mago_result_t state_waking(mago_app_t* app, mago_time_us_t now) {
    // Re-initialize peripherals
    mago_components_start_all(&app->components);

    mago_app_on_wake();

    app->sleep_requested = false;
    app_change_state(app, MAGO_APP_STATE_RUNNING);
    return MAGO_OK;
}

static mago_result_t state_error(mago_app_t* app, mago_time_us_t now) {
    // Blink error pattern
    static mago_time_us_t last_blink = 0;
    if (now - last_blink >= MAGO_MS_TO_US(500)) {
        MAGO_GPIO_TOGGLE(&g_mago_pins.led_error);
        last_blink = now;
    }
    return MAGO_OK;
}

/* ============================================================================
 * MAIN API
 * ============================================================================ */

bool mago_app_init(mago_app_t* app, const mago_system_config_t* cfg) {
    if (!app) app = &g_app;
    memset(app, 0, sizeof(mago_app_t));

    if (cfg) {
        app->config = *cfg;
    } else {
        mago_config_defaults(&app->config);
    }

    // Validate config
    if (!mago_config_validate(&app->config)) {
        return false;
    }

    app->state = MAGO_APP_STATE_BOOT;
    app->log_level = app->config.log_level;

    return true;
}

void mago_app_run(mago_app_t* app) {
    if (!app) app = &g_app;

    while (!app->fatal_error) {
        mago_time_us_t now = MAGO_TIMER_GET_US();
        mago_result_t res = mago_app_tick(app, now);

        if (MAGO_FAILED(res) && res != MAGO_ERR_TIMEOUT) {
            app_fatal_error(app, -res, "Tick failed");
        }

        // Small delay to prevent tight loop
        MAGO_DELAY_MS(1);
    }
}

mago_result_t mago_app_tick(mago_app_t* app, mago_time_us_t now) {
    if (!app) app = &g_app;

    app->last_tick = now;
    app->tick_count++;
    app->loop_count++;

    mago_result_t res = MAGO_OK;

    switch (app->state) {
        case MAGO_APP_STATE_BOOT:
            res = state_boot(app, now);
            break;
        case MAGO_APP_STATE_INIT:
            res = state_init(app, now);
            break;
        case MAGO_APP_STATE_RUNNING:
            res = state_running(app, now);
            break;
        case MAGO_APP_STATE_SLEEP_PREP:
            res = state_sleep_prep(app, now);
            break;
        case MAGO_APP_STATE_SLEEPING:
            res = state_sleeping(app, now);
            break;
        case MAGO_APP_STATE_WAKING:
            res = state_waking(app, now);
            break;
        case MAGO_APP_STATE_ERROR:
            res = state_error(app, now);
            break;
        case MAGO_APP_STATE_SHUTDOWN:
            mago_components_stop_all(&app->components);
            mago_components_deinit_all(&app->components);
            return MAGO_OK;  // Exit loop
    }

    return res;
}

void mago_app_request_shutdown(mago_app_t* app) {
    if (!app) app = &g_app;
    app_change_state(app, MAGO_APP_STATE_SHUTDOWN);
}

void mago_app_request_sleep(mago_app_t* app, uint16_t minutes) {
    if (!app) app = &g_app;
    app->sleep_minutes = minutes;
    mago_event_t evt = {
        .id = MAGO_EVT_SLEEP_REQUEST,
        .timestamp = MAGO_TIMER_GET_US(),
        .data_len = 2
    };
    evt.payload.raw[0] = minutes & 0xFF;
    evt.payload.raw[1] = (minutes >> 8) & 0xFF;
    mago_eventbus_post_isr(&app->eventbus, &evt);
}

void mago_app_handle_button_isr(bool long_press) {
    mago_event_t evt = mago_event_make_button(long_press);
    mago_eventbus_post_isr(&g_app.eventbus, &evt);
}

mago_app_state_t mago_app_get_state(const mago_app_t* app) {
    return app ? app->state : MAGO_APP_STATE_ERROR;
}

void mago_app_get_stats(const mago_app_t* app,
                        uint32_t* uptime_s, uint32_t* tx_count,
                        uint32_t* gps_fixes, uint32_t* errors) {
    if (!app) return;
    if (uptime_s) *uptime_s = (uint32_t)((MAGO_TIMER_GET_US() - app->boot_time) / 1000000);
    if (tx_count) *tx_count = app->tx_success;
    if (gps_fixes) *gps_fixes = app->gps_fixes;
    if (errors) *errors = app->errors;
}

/* ============================================================================
 * WEAK CALLBACK IMPLEMENTATIONS
 * ============================================================================ */

__attribute__((weak)) void mago_app_on_fatal_error(uint32_t error_code, const char* msg) {
    (void)error_code; (void)msg;
}

__attribute__((weak)) void mago_app_on_state_change(mago_app_state_t old_state, mago_app_state_t new_state) {
    (void)old_state; (void)new_state;
}

__attribute__((weak)) void mago_app_on_status_report(const mago_app_t* app) {
    (void)app;
}

__attribute__((weak)) void mago_app_on_pre_sleep(uint16_t minutes) {
    (void)minutes;
}

__attribute__((weak)) void mago_app_on_wake(void) {}

/* ============================================================================
 * GLOBAL ACCESS
 * ============================================================================ */

mago_app_t* mago_app_global(void) {
    return &g_app;
}