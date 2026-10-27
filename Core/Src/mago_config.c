/**
 * @file mago_config.c
 * @brief Mago41 - Config Implementation
 */

#include "mago_config.h"
#include "mago_hal.h"
#include "mago_types.h"
#include <string.h>

/* Flash address for config storage */
#define MAGO_CONFIG_FLASH_ADDR  0x0801F800  // Last page of 128KB flash

void mago_config_defaults(mago_system_config_t* cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(mago_system_config_t));

    strncpy(cfg->device_name, "MAGO41", sizeof(cfg->device_name)-1);
    cfg->device_id = 0x4D47;  // 'MG'
    cfg->serial_number = 0;

    // Default pins (RS41)
    cfg->pins = (mago_board_pins_t){
        .led_status = {'C', 13, MAGO_GPIO_OUTPUT_PP, MAGO_GPIO_PULL_NONE, MAGO_GPIO_SPEED_LOW, 0},
        .led_error  = {'C', 14, MAGO_GPIO_OUTPUT_PP, MAGO_GPIO_PULL_NONE, MAGO_GPIO_SPEED_LOW, 0},
        .radio_cs   = {'B', 12, MAGO_GPIO_OUTPUT_PP, MAGO_GPIO_PULL_UP, MAGO_GPIO_SPEED_HIGH, 0},
        .radio_sck  = {'B', 13, MAGO_GPIO_AF_PP, MAGO_GPIO_PULL_NONE, MAGO_GPIO_SPEED_HIGH, 5},
        .radio_mosi = {'B', 15, MAGO_GPIO_AF_PP, MAGO_GPIO_PULL_NONE, MAGO_GPIO_SPEED_HIGH, 5},
        .radio_miso = {'B', 14, MAGO_GPIO_INPUT, MAGO_GPIO_PULL_NONE, MAGO_GPIO_SPEED_HIGH, 5},
        .radio_sdn  = {'B', 0,  MAGO_GPIO_OUTPUT_PP, MAGO_GPIO_PULL_NONE, MAGO_GPIO_SPEED_LOW, 0},
        .radio_nirq = {'B', 1,  MAGO_GPIO_IT_BOTH, MAGO_GPIO_PULL_UP, MAGO_GPIO_SPEED_HIGH, 0},
        .gps_tx     = {'A', 9,  MAGO_GPIO_AF_PP, MAGO_GPIO_PULL_UP, MAGO_GPIO_SPEED_HIGH, 7},
        .gps_rx     = {'A', 10, MAGO_GPIO_INPUT, MAGO_GPIO_PULL_UP, MAGO_GPIO_SPEED_HIGH, 7},
        .gps_rst    = {'A', 15, MAGO_GPIO_OUTPUT_PP, MAGO_GPIO_PULL_NONE, MAGO_GPIO_SPEED_LOW, 0},
        .debug_tx   = {'A', 2,  MAGO_GPIO_AF_PP, MAGO_GPIO_PULL_NONE, MAGO_GPIO_SPEED_HIGH, 7},
        .debug_rx   = {'A', 3,  MAGO_GPIO_INPUT, MAGO_GPIO_PULL_NONE, MAGO_GPIO_SPEED_HIGH, 7},
        .adc_batt   = {'A', 0,  MAGO_GPIO_ANALOG, MAGO_GPIO_PULL_NONE, MAGO_GPIO_SPEED_LOW, 0},
        .adc_temp   = {'A', 1,  MAGO_GPIO_ANALOG, MAGO_GPIO_PULL_NONE, MAGO_GPIO_SPEED_LOW, 0},
        .btn_user   = {'C', 13, MAGO_GPIO_IT_BOTH, MAGO_GPIO_PULL_UP, MAGO_GPIO_SPEED_LOW, 0},
    };

    // Radio defaults
    cfg->radio = (mago_radio_cfg_t){
        .freq_mhz = 434.714f,
        .freq_offset_ppm = 0,
        .power_dbm = 13,
        .chip_type = MAGO_RADIO_CHIP_AUTO,
        .tx_only = true,
        .xtal_cap = 8,
    };

    // GPS defaults
    cfg->gps = (mago_gps_cfg_t){
        .baud_init = 9600,
        .baud_operational = 38400,
        .model = MAGO_GPS_MODEL_AUTO,
        .dynamic_model = MAGO_GPS_DYN_AIRBORNE_1G,
        .fix_mode = 3,
        .min_elev_deg = 5,
        .pdop_mask = 100,
        .powersave_mode = 1,
        .auto_restart = true,
        .restart_timeout_s = 60,
        .enable_glonass = true,
        .enable_galileo = false,
    };

    // Power defaults (2x Li-SOCl2 AA)
    cfg->power = (mago_power_cfg_t){
        .type = MAGO_PWR_BATT_LI_SOCL2_2S,
        .v_min_mv = 3000,
        .v_max_mv = 3900,
        .v_warn_mv = 3200,
        .v_critical_mv = 3000,
        .divider_ratio = 2.0f,
        .temp_coeff = 4.3f,
        .sleep_current_ua = 50,
        .tx_current_ma = 120,
    };

    // Protocols - RTTY + MFSK (Horus V2) by default
    cfg->protocol_count = 2;
    cfg->protocols[0] = (mago_protocol_cfg_t){
        .id = MAGO_PROTO_RTTY,
        .name = "rtty",
        .enabled = true,
        .priority = 10,
        .interval_ms = 30000,
        .offset_ms = 0,
        .params = NULL,
    };
    static mago_proto_rtty_params_t rtty_params = {
        .baud = 50, .shift_hz = 340, .bits = 7, .stop_bits = 2, .extended_ascii = false,
        .callsign = "MAGO41"
    };
    cfg->protocols[0].params = &rtty_params;

    cfg->protocols[1] = (mago_protocol_cfg_t){
        .id = MAGO_PROTO_HORUS_V2,
        .name = "horus_v2",
        .enabled = true,
        .priority = 5,
        .interval_ms = 60000,
        .offset_ms = 10000,
        .params = NULL,
    };
    static mago_proto_mfsk_params_t mfsk_params = {
        .baud = 100, .modulation = 4, .deviation_hz = 600,
        .fec_enabled = true, .interleaving = 2, .scrambling = true,
        .payload_id = 0x0200, .preamble_symbols = 16, .sync_symbols = 16,
        .horus_v2 = true,
    };
    cfg->protocols[1].params = &mfsk_params;

    // Behavior
    cfg->main_loop_period_ms = 10;
    cfg->log_level = 3;
    cfg->watchdog_enabled = true;
    cfg->watchdog_timeout_ms = 8000;
    cfg->deep_sleep_enabled = false;
    cfg->deep_sleep_minutes = 0;

    // Security
    cfg->encrypt_telemetry = false;
    memset(cfg->aes_key, 0, 16);

    cfg->config_crc = mago_config_crc32(cfg, offsetof(mago_system_config_t, config_crc));
}

bool mago_config_validate(const mago_system_config_t* cfg) {
    if (!cfg) return false;
    if (cfg->config_crc != mago_config_crc32(cfg, offsetof(mago_system_config_t, config_crc))) {
        return false;
    }
    if (cfg->radio.freq_mhz < 400 || cfg->radio.freq_mhz > 470) return false;
    if (cfg->radio.power_dbm < -10 || cfg->radio.power_dbm > 20) return false;
    if (cfg->protocol_count == 0 || cfg->protocol_count > MAGO_MAX_PROTOCOLS) return false;
    return true;
}

uint32_t mago_config_crc32(const void* data, size_t len) {
    const uint8_t* p = (const uint8_t*)data;
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return ~crc;
}

bool mago_config_load(mago_system_config_t* cfg) {
    if (!cfg) return false;

    // Try to read from flash
    mago_result_t res = MAGO_HAL_GET()->flash->read(MAGO_CONFIG_FLASH_ADDR, (uint8_t*)cfg, sizeof(mago_system_config_t));
    if (MAGO_SUCCEEDED(res) && mago_config_validate(cfg)) {
        return true;
    }

    // Load defaults
    mago_config_defaults(cfg);
    return false;
}

bool mago_config_save(const mago_system_config_t* cfg) {
    if (!cfg || !mago_config_validate(cfg)) return false;

    mago_system_config_t to_save = *cfg;
    to_save.config_crc = mago_config_crc32(&to_save, offsetof(mago_system_config_t, config_crc));

    // Erase page
    mago_result_t res = MAGO_HAL_GET()->flash->erase(MAGO_CONFIG_FLASH_ADDR, 1024);
    if (MAGO_FAILED(res)) return false;

    // Write
    res = MAGO_HAL_GET()->flash->write(MAGO_CONFIG_FLASH_ADDR, (const uint8_t*)&to_save, sizeof(mago_system_config_t));
    return MAGO_SUCCEEDED(res);
}

void mago_config_print(const mago_system_config_t* cfg) {
    if (!cfg) return;
    // Could implement pretty print
}

const mago_rtty_params_t* mago_config_get_rtty(const mago_protocol_cfg_t* p) {
    return (p && p->params) ? (const mago_rtty_params_t*)p->params : NULL;
}
const mago_mfsk_params_t* mago_config_get_mfsk(const mago_protocol_cfg_t* p) {
    return (p && p->params) ? (const mago_mfsk_params_t*)p->params : NULL;
}
const mago_aprs_params_t* mago_config_get_aprs(const mago_protocol_cfg_t* p) {
    return (p && p->params) ? (const mago_aprs_params_t*)p->params : NULL;
}
const mago_morse_params_t* mago_config_get_morse(const mago_protocol_cfg_t* p) {
    return (p && p->params) ? (const mago_morse_params_t*)p->params : NULL;
}