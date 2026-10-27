/**
 * @file mago_app.h
 * @brief Mago41 - Aplicación Principal Event-Driven
 *
 * Diferencias radicales vs RS41HUP_V2:
 * - NO hay superloop con polling
 * - Arquitectura event-driven pura (message bus)
 * - Componentes independientes comunican via eventos
 * - Protocolos son plugins hot-swappable
 * - Power management integrado en event loop
 * - Configuración en runtime, no compile-time
 * - Watchdog y error handling centralizados
 */

#ifndef MAGO_APP_H
#define MAGO_APP_H

#include <stdint.h>
#include <stdbool.h>
#include "mago_types.h"
#include "mago_config.h"
#include "mago_component.h"
#include "mago_eventbus.h"

/* ============================================================================
 * APPLICATION STATE MACHINE
 * ============================================================================ */

typedef enum {
    MAGO_APP_STATE_BOOT = 0,
    MAGO_APP_STATE_INIT,
    MAGO_APP_STATE_RUNNING,
    MAGO_APP_STATE_SLEEP_PREP,
    MAGO_APP_STATE_SLEEPING,
    MAGO_APP_STATE_WAKING,
    MAGO_APP_STATE_ERROR,
    MAGO_APP_STATE_SHUTDOWN,
} mago_app_state_t;

/* ============================================================================
 * MAIN APPLICATION CONTEXT
 * ============================================================================ */

typedef struct {
    // Config & State
    mago_system_config_t config;
    mago_app_state_t state;
    mago_app_state_t prev_state;

    // Core subsystems
    mago_component_registry_t components;
    mago_eventbus_t eventbus;

    // Timing
    mago_time_us_t boot_time;
    mago_time_us_t last_tick;
    uint32_t tick_count;
    uint32_t loop_count;

    // Statistics
    uint32_t events_processed;
    uint32_t tx_attempts;
    uint32_t tx_success;
    uint32_t gps_fixes;
    uint32_t errors;

    // Power management
    bool sleep_requested;
    uint32_t sleep_deadline;
    uint16_t sleep_minutes;

    // Error handling
    uint32_t last_error_code;
    mago_time_us_t last_error_time;
    uint8_t error_count;
    bool fatal_error;

    // Debug
    uint8_t log_level;
    bool debug_tx;
} mago_app_t;

/* ============================================================================
 * APPLICATION CALLBACKS (implement in board file)
 * ============================================================================ */

// Called on fatal error - implement board-specific behavior (LED patterns, etc)
void mago_app_on_fatal_error(uint32_t error_code, const char* msg);

// Called on state change
void mago_app_on_state_change(mago_app_state_t old_state, mago_app_state_t new_state);

// Called periodically for status reporting
void mago_app_on_status_report(const mago_app_t* app);

// Called before deep sleep
void mago_app_on_pre_sleep(uint16_t minutes);

// Called after wake
void mago_app_on_wake(void);

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

// Initialize application (parses config, registers components)
bool mago_app_init(mago_app_t* app, const mago_system_config_t* cfg);

// Main event loop - runs forever, returns only on fatal error
void mago_app_run(mago_app_t* app);

// Single iteration - for RTOS task integration
mago_result_t mago_app_tick(mago_app_t* app, mago_time_us_t now);

// Request graceful shutdown
void mago_app_request_shutdown(mago_app_t* app);

// Request sleep
void mago_app_request_sleep(mago_app_t* app, uint16_t minutes);

// Handle button press (called from ISR)
void mago_app_handle_button_isr(bool long_press);

// Get current state
mago_app_state_t mago_app_get_state(const mago_app_t* app);

// Get runtime stats
void mago_app_get_stats(const mago_app_t* app,
                        uint32_t* uptime_s, uint32_t* tx_count,
                        uint32_t* gps_fixes, uint32_t* errors);

#endif // MAGO_APP_H