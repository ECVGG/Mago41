/**
 * @file mago_power.c
 * @brief Mago41 - Power Module Implementation (Li-SOCl2 Battery Management)
 */

#include "mago_power.h"
#include "mago_hal.h"
#include "mago_eventbus.h"
#include "mago_config.h"
#include <math.h>

/* ============================================================================
 * STATIC DATA
 * ============================================================================ */

static mago_power_data_t g_power;

/* ============================================================================
 * BATTERY MODELS
 * ============================================================================ */

static const mago_battery_model_t battery_models[] = {
    [MAGO_PWR_BATT_LI_SOCL2_1S] = { 2.5f, 3.6f, 3.6f, 2400, 0.5f, 4.3f },
    [MAGO_PWR_BATT_LI_SOCL2_2S] = { 3.0f, 3.9f, 3.6f, 2400, 1.0f, 8.6f },
    [MAGO_PWR_BATT_LI_ION_1S]   = { 3.0f, 4.2f, 3.7f, 3000, 0.1f, 5.0f },
    [MAGO_PWR_BATT_LI_ION_2S]   = { 6.0f, 8.4f, 7.4f, 3000, 0.2f, 10.0f },
    [MAGO_PWR_BATT_NIMH_2S]     = { 2.0f, 2.8f, 2.4f, 2000, 0.3f, 3.0f },
    [MAGO_PWR_BATT_NIMH_3S]     = { 3.0f, 4.2f, 3.6f, 2000, 0.4f, 4.5f },
    [MAGO_PWR_BATT_ALK_2S]      = { 1.8f, 3.0f, 3.0f, 2500, 0.4f, 3.5f },
    [MAGO_PWR_BATT_ALK_3S]      = { 2.7f, 4.5f, 4.5f, 2500, 0.5f, 5.0f },
    [MAGO_PWR_BATT_CUSTOM]      = { 0, 0, 0, 0, 0, 0 },
};

/* ============================================================================
 * ADC TO VOLTAGE CONVERSION
 * ============================================================================ */

static uint16_t adc_to_mv(uint16_t adc_raw, float divider_ratio, uint16_t vref_mv) {
    // ADC is 12-bit (0-4095), VREF is measured internal reference
    // Actual VDDA = VREF * 4095 / ADC_VREF
    // Battery voltage = ADC_BATT * VDDA / 4095 * divider_ratio
    return (uint16_t)((float)adc_raw * vref_mv / 4095.0f * divider_ratio);
}

static int16_t adc_to_temp(uint16_t adc_raw, uint16_t vref_mv) {
    // STM32 internal temp sensor: V25 = 1.43V, Avg_Slope = 4.3mV/°C
    float v_sense = (float)adc_raw * vref_mv / 4095.0f;
    return (int16_t)((1430.0f - v_sense) / 4.3f + 25.0f);
}

/* ============================================================================
 * SOC ESTIMATION (Li-SOCl2 voltage curve)
 * ============================================================================ */

static float estimate_soc_lisocl2(uint16_t mv, int16_t temp_c) {
    // Li-SOCl2 has very flat discharge curve
    // Temperature compensation
    float temp_comp = (float)(temp_c - 25) * g_power.model.temp_coeff_mv_per_c;
    float v_comp = (float)mv - temp_comp;

    if (v_comp >= g_power.model.v_max) return 100.0f;
    if (v_comp <= g_power.model.v_min) return 0.0f;

    // Approximate curve for Li-SOCl2
    float range = g_power.model.v_max - g_power.model.v_min;
    float soc = (v_comp - g_power.model.v_min) / range * 100.0f;

    // Clamp and apply non-linear correction
    if (soc > 90) soc = 90 + (soc - 90) * 0.5f; // Flatten top
    if (soc < 10) soc = soc * 0.5f;              // Flatten bottom

    return fmaxf(0, fminf(100, soc));
}

static float estimate_soc_generic(uint16_t mv) {
    if (mv >= g_power.model.v_max) return 100.0f;
    if (mv <= g_power.model.v_min) return 0.0f;
    return (float)(mv - g_power.model.v_min) * 100.0f / (g_power.model.v_max - g_power.model.v_min);
}

/* ============================================================================
 * CURRENT ESTIMATION PER MODE
 * ============================================================================ */

static uint32_t mode_current_ua(mago_pwr_mode_t mode) {
    switch (mode) {
        case MAGO_PWR_MODE_ACTIVE:       return 15000;  // 15mA (GPS + MCU + Radio idle)
        case MAGO_PWR_MODE_TX_ONLY:      return 120000; // 120mA (Radio TX)
        case MAGO_PWR_MODE_GPS_ONLY:     return 25000;  // 25mA (GPS + MCU)
        case MAGO_PWR_MODE_STANDBY:      return 500;    // 500uA (Sleep + RTC)
        case MAGO_PWR_MODE_STOP:         return 50;     // 50uA (Stop mode)
        case MAGO_PWR_MODE_STANDBY_DEEP: return 10;     // 10uA (Standby)
        default:                         return 500;
    }
}

/* ============================================================================
 * MODE TRANSITIONS
 * ============================================================================ */

static mago_result_t power_enter_mode(mago_pwr_mode_t mode) {
    g_power.current_mode = mode;

    switch (mode) {
        case MAGO_PWR_MODE_ACTIVE:
            // Wake GPS, enable radio
            MAGO_GPIO_WRITE(&g_mago_pins.gps_rst, true);
            MAGO_GPIO_WRITE(&g_mago_pins.radio_sdn, false);
            break;
        case MAGO_PWR_MODE_TX_ONLY:
            MAGO_GPIO_WRITE(&g_mago_pins.gps_rst, false); // GPS off
            MAGO_GPIO_WRITE(&g_mago_pins.radio_sdn, false);
            break;
        case MAGO_PWR_MODE_GPS_ONLY:
            MAGO_GPIO_WRITE(&g_mago_pins.gps_rst, true);
            MAGO_GPIO_WRITE(&g_mago_pins.radio_sdn, true); // Radio off
            break;
        case MAGO_PWR_MODE_STANDBY:
            MAGO_GPIO_WRITE(&g_mago_pins.gps_rst, false);
            MAGO_GPIO_WRITE(&g_mago_pins.radio_sdn, true);
            MAGO_HAL_GET()->pwr->sleep();
            break;
        case MAGO_PWR_MODE_STOP:
            MAGO_GPIO_WRITE(&g_mago_pins.gps_rst, false);
            MAGO_GPIO_WRITE(&g_mago_pins.radio_sdn, true);
            MAGO_HAL_GET()->pwr->stop();
            break;
        case MAGO_PWR_MODE_STANDBY_DEEP:
            MAGO_GPIO_WRITE(&g_mago_pins.gps_rst, false);
            MAGO_GPIO_WRITE(&g_mago_pins.radio_sdn, true);
            MAGO_HAL_GET()->pwr->standby();
            break;
    }
    g_power.mode_changes++;
    return MAGO_OK;
}

/* ============================================================================
 * COMPONENT CALLBACKS
 * ============================================================================ */

mago_result_t mago_power_init(void* ctx, const mago_power_cfg_t* cfg) {
    if (!cfg) return MAGO_ERR_INVAL;

    memset(&g_power, 0, sizeof(g_power));
    g_power.config = *cfg;
    g_power.current_mode = MAGO_PWR_MODE_ACTIVE;
    g_power.requested_mode = MAGO_PWR_MODE_ACTIVE;

    // Select battery model
    if (cfg->type < MAGO_PWR_BATT_MAX) {
        g_power.model = battery_models[cfg->type];
    }
    g_power.model.v_min = cfg->v_min_mv / 1000.0f;
    g_power.model.v_max = cfg->v_max_mv / 1000.0f;
    g_power.model.temp_coeff_mv_per_c = cfg->temp_coeff;

    g_power.warn_threshold_mv = cfg->v_warn_mv;
    g_power.critical_threshold_mv = cfg->v_critical_mv;

    // Initialize ADC
    mago_adc_cfg_t adc_cfg = { .channel = 0, .sample_time = ADC_SMPR_SMP_239DOT5CYC };
    MAGO_HAL_GET()->adc->init(&adc_cfg);

    // Initial measurement
    mago_power_tick(ctx);

    return MAGO_OK;
}

mago_result_t mago_power_start(void* ctx) {
    g_power.requested_mode = MAGO_PWR_MODE_ACTIVE;
    return power_enter_mode(MAGO_PWR_MODE_ACTIVE);
}

mago_result_t mago_power_tick(void* ctx) {
    // Read ADC channels
    uint16_t adc_batt, adc_temp, adc_vref;
    MAGO_HAL_GET()->adc->read_channel(0, &adc_batt);   // PA0 - Battery
    MAGO_HAL_GET()->adc->read_channel(1, &adc_temp);   // PA1 - Temp
    MAGO_HAL_GET()->adc->read_channel(17, &adc_vref);  // VREFINT

    g_power.vref_mv = (uint16_t)(1210.0f * 4095.0f / adc_vref); // 1.21V nominal
    g_power.batt_mv = adc_to_mv(adc_batt, g_power.config.divider_ratio, g_power.vref_mv);
    g_power.temp_c = adc_to_temp(adc_temp, g_power.vref_mv);
    g_power.adc_samples++;

    // Estimate SOC
    if (g_power.config.type == MAGO_PWR_BATT_LI_SOCL2_1S || g_power.config.type == MAGO_PWR_BATT_LI_SOCL2_2S) {
        g_power.soc_percent = estimate_soc_lisocl2(g_power.batt_mv, g_power.temp_c);
    } else {
        g_power.soc_percent = estimate_soc_generic(g_power.batt_mv);
    }

    // Estimate remaining time
    uint32_t current_ua = mode_current_ua(g_power.current_mode);
    if (current_ua > 0) {
        float remaining_mah = g_power.model.capacity_mah * g_power.soc_percent / 100.0f;
        g_power.remaining_sec = (uint32_t)(remaining_mah * 3600.0f / (current_ua / 1000.0f));
    } else {
        g_power.remaining_sec = 0xFFFFFFFF;
    }

    // Check thresholds
    if (!g_power.warned_low && g_power.batt_mv <= g_power.warn_threshold_mv) {
        g_power.warned_low = true;
        mago_event_t evt = { .id = MAGO_EVT_BATTERY_LOW, .timestamp_us = MAGO_TIMER_GET_US(),
                            .data.battery = { .voltage_mv = g_power.batt_mv, .percent = (uint8_t)g_power.soc_percent }};
        mago_eventbus_post(&evt);
    }

    if (!g_power.warned_critical && g_power.batt_mv <= g_power.critical_threshold_mv) {
        g_power.warned_critical = true;
        mago_event_t evt = { .id = MAGO_EVT_BATTERY_CRITICAL, .timestamp_us = MAGO_TIMER_GET_US(),
                            .data.battery = { .voltage_mv = g_power.batt_mv, .percent = (uint8_t)g_power.soc_percent }};
        mago_eventbus_post(&evt);
    }

    // Handle mode transition requests
    if (g_power.requested_mode != g_power.current_mode) {
        power_enter_mode(g_power.requested_mode);
    }

    return MAGO_OK;
}

void mago_power_handle_event(void* ctx, const mago_event_t* evt) {
    if (!evt) return;

    switch (evt->id) {
        case MAGO_EVT_RADIO_TX_DONE:
            g_power.total_tx_ms += 100; // Approximate
            g_power.total_mah_consumed += (120 * 100) / 3600000; // 120mA * 100ms
            // Return to previous mode after TX
            if (g_power.current_mode == MAGO_PWR_MODE_TX_ONLY) {
                g_power.requested_mode = MAGO_PWR_MODE_STANDBY;
            }
            break;
        case MAGO_EVT_GPS_FIX:
            g_power.total_gps_ms += 200; // Approximate
            break;
        case MAGO_EVT_ENTER_SLEEP:
            g_power.requested_mode = (mago_pwr_mode_t)evt->data.sleep.minutes > 0 ?
                MAGO_PWR_MODE_STANDBY_DEEP : MAGO_PWR_MODE_STOP;
            break;
        case MAGO_EVT_BUTTON_PRESS:
            // Button press could wake from sleep
            if (g_power.current_mode >= MAGO_PWR_MODE_STANDBY) {
                g_power.requested_mode = MAGO_PWR_MODE_ACTIVE;
            }
            break;
    }
}

mago_result_t mago_power_stop(void* ctx) {
    g_power.requested_mode = MAGO_PWR_MODE_STOP;
    return MAGO_OK;
}

mago_result_t mago_power_deinit(void* ctx) {
    MAGO_HAL_GET()->adc->deinit();
    return MAGO_OK;
}

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

mago_result_t mago_power_set_mode(void* ctx, mago_pwr_mode_t mode) {
    if (mode >= MAGO_PWR_MODE_MAX) return MAGO_ERR_INVAL;
    g_power.requested_mode = mode;
    return MAGO_OK;
}

mago_pwr_mode_t mago_power_get_mode(void* ctx) {
    return g_power.current_mode;
}

uint16_t mago_power_get_batt_mv(void* ctx) {
    return g_power.batt_mv;
}

int16_t mago_power_get_temp_c(void* ctx) {
    return g_power.temp_c;
}

float mago_power_get_soc(void* ctx) {
    return g_power.soc_percent;
}

uint32_t mago_power_get_remaining_sec(void* ctx) {
    return g_power.remaining_sec;
}

bool mago_power_is_low(void* ctx) {
    return g_power.warned_low;
}

bool mago_power_is_critical(void* ctx) {
    return g_power.warned_critical;
}

void mago_power_get_stats(void* ctx, uint32_t* total_tx_ms, uint32_t* total_sleep_ms, uint32_t* mah_consumed) {
    if (total_tx_ms) *total_tx_ms = g_power.total_tx_ms;
    if (total_sleep_ms) *total_sleep_ms = g_power.total_sleep_ms;
    if (mah_consumed) *mah_consumed = g_power.total_mah_consumed;
}

/* ============================================================================
 * FACTORY
 * ============================================================================ */

const mago_component_factory_t* mago_power_factory(void) {
    static const mago_component_factory_t factory = {
        .id = MAGO_COMP_POWER,
        .name = "power",
        .priority = 30,
        .init = mago_power_init,
        .start = mago_power_start,
        .tick = mago_power_tick,
        .handle_event = mago_power_handle_event,
        .stop = mago_power_stop,
        .deinit = mago_power_deinit,
        .create_data = NULL,
        .data_size = sizeof(mago_power_data_t),
    };
    return &factory;
}