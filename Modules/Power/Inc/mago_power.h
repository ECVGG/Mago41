/**
 * @file mago_power.h
 * @brief Mago41 - Power Module (Li-SOCl2 Battery Management)
 *
 * Diferencia radical vs RS41HUP_V2:
 * - Modelo de batería configurable (tipo, voltajes, capacidad)
 * - 6 modos de potencia con transiciones explícitas
 * - Estimación de tiempo restante basada en perfil de misión
 * - Watchdog independiente
 * - Temperatura con compensación
 * - Event-driven: battery low/critical via message bus
 */

#ifndef MAGO_POWER_H
#define MAGO_POWER_H

#include <stdint.h>
#include <stdbool.h>
#include "mago_types.h"
#include "mago_config.h"

/* ============================================================================
 * POWER MODES
 * ============================================================================ */

typedef enum {
    MAGO_PWR_MODE_ACTIVE      = 0,  // Full operation, GPS + Radio TX
    MAGO_PWR_MODE_TX_ONLY     = 1,  // Radio TX only, GPS off
    MAGO_PWR_MODE_GPS_ONLY    = 2,  // GPS on, Radio idle
    MAGO_PWR_MODE_STANDBY     = 3,  // MCU sleep, RTC + RAM retention
    MAGO_PWR_MODE_STOP        = 4,  // Stop mode, wake on RTC/EXTI
    MAGO_PWR_MODE_STANDBY_DEEP = 5, // Standby, wake on WKUP pin
} mago_pwr_mode_t;

/* ============================================================================
 * BATTERY MODEL
 * ============================================================================ */

typedef struct {
    float v_min;
    float v_max;
    float v_nominal;
    float capacity_mah;
    float internal_resistance_ohm;
    float temp_coeff_mv_per_c;  // mV/°C
} mago_battery_model_t;

/* ============================================================================
 * POWER DATA STRUCT
 * ============================================================================ */

typedef struct {
    mago_power_cfg_t config;
    mago_battery_model_t model;
    mago_pwr_mode_t current_mode;
    mago_pwr_mode_t requested_mode;

    // Measurements
    uint16_t batt_mv;
    int16_t temp_c;
    uint16_t vref_mv;

    // Derived
    float soc_percent;        // State of Charge 0-100%
    float soh_percent;        // State of Health 0-100%
    uint32_t remaining_sec;   // Estimated remaining time at current draw

    // Accumulators
    uint32_t total_tx_ms;
    uint32_t total_gps_ms;
    uint32_t total_sleep_ms;
    uint32_t total_mah_consumed;

    // Thresholds
    uint16_t warn_threshold_mv;
    uint16_t critical_threshold_mv;
    bool warned_low;
    bool warned_critical;

    // Stats
    uint32_t adc_samples;
    uint32_t mode_changes;
    uint32_t watchdog_resets;
} mago_power_data_t;

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

mago_result_t mago_power_init(void* ctx, const mago_power_cfg_t* cfg);
mago_result_t mago_power_start(void* ctx);
mago_result_t mago_power_tick(void* ctx);
void mago_power_handle_event(void* ctx, const mago_event_t* evt);
mago_result_t mago_power_stop(void* ctx);
mago_result_t mago_power_deinit(void* ctx);

mago_result_t mago_power_set_mode(void* ctx, mago_pwr_mode_t mode);
mago_pwr_mode_t mago_power_get_mode(void* ctx);
uint16_t mago_power_get_batt_mv(void* ctx);
int16_t mago_power_get_temp_c(void* ctx);
float mago_power_get_soc(void* ctx);
uint32_t mago_power_get_remaining_sec(void* ctx);
bool mago_power_is_low(void* ctx);
bool mago_power_is_critical(void* ctx);
void mago_power_get_stats(void* ctx, uint32_t* total_tx_ms, uint32_t* total_sleep_ms, uint32_t* mah_consumed);

const mago_component_factory_t* mago_power_factory(void);

#endif // MAGO_POWER_H