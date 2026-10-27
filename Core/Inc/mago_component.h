/**
 * @file mago_component.h
 * @brief Mago41 - Component System (Plugin Architecture)
 *
 * Diferencias vs RS41HUP_V2:
 * - Componentes registrados dinámicamente, no hardcoded
 * - Lifecycle explícito: init->start->tick->stop->deinit
 * - Event-driven communication via message bus
 * - Prioridad de inicialización
 * - Static allocation, zero malloc
 * - Hot-swap capable (en teoría)
 */

#ifndef MAGO_COMPONENT_H
#define MAGO_COMPONENT_H

#include <stdint.h>
#include <stdbool.h>
#include "mago_types.h"
#include "mago_eventbus.h"

/* ============================================================================
 * COMPONENT REGISTRY
 * ============================================================================ */

#define MAGO_MAX_COMPONENTS     16
#define MAGO_COMP_NAME_MAX      16

typedef struct {
    mago_component_t components[MAGO_MAX_COMPONENTS];
    uint8_t count;
    bool initialized;
} mago_component_registry_t;

/* ============================================================================
 * BUILT-IN COMPONENT IDS
 * ============================================================================ */

typedef enum {
    MAGO_COMP_RADIO = 0,
    MAGO_COMP_GPS,
    MAGO_COMP_POWER,
    MAGO_COMP_PROTOCOL_MGR,
    MAGO_COMP_TELEMETRY,
    MAGO_COMP_WATCHDOG,
    MAGO_COMP_CUSTOM_BASE = 128,
} mago_builtin_comp_t;

/* ============================================================================
 * RADIO COMPONENT
 * ============================================================================ */

typedef struct {
    mago_radio_ops_t ops;
    mago_radio_mode_t current_mode;
    float current_freq;
    int8_t current_power;
    uint32_t tx_count;
    uint32_t rx_count;
    uint32_t error_count;
} mago_radio_comp_data_t;

/* ============================================================================
 * GPS COMPONENT
 * ============================================================================ */

typedef struct {
    mago_gps_nav_t last_fix;
    bool has_fix;
    uint32_t fix_age_ms;
    uint8_t sats_tracked;
    uint8_t sats_used;
    uint32_t packets_received;
    uint32_t crc_errors;
    uint32_t last_itow;
} mago_gps_comp_data_t;

/* ============================================================================
 * POWER COMPONENT
 * ============================================================================ */

typedef struct {
    uint16_t battery_mv;
    int16_t temp_c;
    uint16_t solar_mv;
    uint32_t sleep_time_ms;
    uint32_t active_time_ms;
    uint8_t sleep_cycles;
    bool low_batt_warned;
    bool critical_batt;
} mago_power_comp_data_t;

/* ============================================================================
 * PROTOCOL MANAGER COMPONENT
 * ============================================================================ */

#define MAGO_MAX_ACTIVE_PROTOS  4

typedef struct {
    mago_protocol_t* protocols[MAGO_MAX_ACTIVE_PROTOS];
    uint8_t count;
    uint8_t current_tx;
    bool tx_pending;
    uint32_t next_tx_time;
    uint32_t tx_interval_ms;
} mago_proto_mgr_data_t;

/* ============================================================================
 * TELEMETRY COMPONENT
 * ============================================================================ */

typedef struct {
    mago_telemetry_frame_t last_frame;
    uint32_t frame_count;
    uint32_t last_tx_time;
    bool dirty;
} mago_telemetry_comp_data_t;

/* ============================================================================
 * WATCHDOG COMPONENT
 * ============================================================================ */

typedef struct {
    uint32_t timeout_ms;
    uint32_t last_kick;
    uint32_t kick_count;
    bool enabled;
} mago_watchdog_comp_data_t;

/* ============================================================================
 * API FUNCTIONS
 * ============================================================================ */

// Registry
bool mago_components_init(mago_component_registry_t* reg);
bool mago_components_register(mago_component_registry_t* reg, const mago_component_t* comp);
bool mago_components_unregister(mago_component_registry_t* reg, const char* name);

// Lifecycle
mago_result_t mago_components_init_all(mago_component_registry_t* reg, const mago_system_config_t* cfg);
mago_result_t mago_components_start_all(mago_component_registry_t* reg);
mago_result_t mago_components_stop_all(mago_component_registry_t* reg);
void mago_components_deinit_all(mago_component_registry_t* reg);

// Main tick (call from superloop)
mago_result_t mago_components_tick_all(mago_component_registry_t* reg, mago_time_us_t now);

// Event dispatch
void mago_components_dispatch_event(mago_component_registry_t* reg, const mago_event_t* evt);

// Find component
mago_component_t* mago_components_find(mago_component_registry_t* reg, const char* name);
mago_component_t* mago_components_find_by_type(mago_component_registry_t* reg, uint8_t type);

// Built-in component factories
mago_component_t* mago_component_create_radio(const mago_radio_cfg_t* cfg);
mago_component_t* mago_component_create_gps(const mago_gps_cfg_t* cfg);
mago_component_t* mago_component_create_power(const mago_power_cfg_t* cfg);
mago_component_t* mago_component_create_proto_mgr(const mago_system_config_t* cfg);
mago_component_t* mago_component_create_telemetry(void);
mago_component_t* mago_component_create_watchdog(uint32_t timeout_ms);

#endif // MAGO_COMPONENT_H