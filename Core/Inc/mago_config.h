/**
 * @file mago_config.h
 * @brief Mago41 - Configuración declarativa basada en datos
 *
 * Filosofía: Configuration as Data - toda config es structs serializables
 * Sin #defines mágicos, todo validable en runtime
 */

#ifndef MAGO_CONFIG_H
#define MAGO_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ============================================================================
 * VERSION & METADATA
 * ============================================================================ */

#define MAGO_FIRMWARE_VERSION     "1.0.0"
#define MAGO_PROTOCOL_VERSION     41
#define MAGO_BUILD_TIMESTAMP      __DATE__ " " __TIME__

/* ============================================================================
 * HARDWARE ABSTRACTION - Pin mapping declarativo
 * ============================================================================ */

typedef struct {
    char port;      // 'A'..'H'
    uint8_t pin;    // 0..15
    uint8_t mode;   // GPIO_MODE_*
    uint8_t pull;   // GPIO_PULL_*
    uint8_t speed;  // GPIO_SPEED_*
    uint8_t af;     // Alternate function (0 = none)
} mago_pin_t;

typedef struct {
    mago_pin_t led_status;
    mago_pin_t led_error;
    mago_pin_t radio_cs;
    mago_pin_t radio_sck;
    mago_pin_t radio_mosi;
    mago_pin_t radio_miso;
    mago_pin_t radio_sdn;
    mago_pin_t radio_nirq;
    mago_pin_t gps_tx;
    mago_pin_t gps_rx;
    mago_pin_t gps_rst;
    mago_pin_t debug_tx;
    mago_pin_t debug_rx;
    mago_pin_t adc_batt;
    mago_pin_t adc_temp;
    mago_pin_t btn_user;
} mago_board_pins_t;

/* ============================================================================
 * RADIO CONFIG - SI4032/Si4432
 * ============================================================================ */

typedef enum {
    MAGO_RADIO_CHIP_AUTO = 0,
    MAGO_RADIO_CHIP_SI4032 = 0x40,
    MAGO_RADIO_CHIP_SI4432 = 0x44,
    MAGO_RADIO_CHIP_SI4463 = 0x63,
} mago_radio_chip_t;

typedef struct {
    float freq_mhz;
    int16_t freq_offset_ppm;      // Corrección cristal
    uint8_t power_dbm;            // -10..+20 dBm
    mago_radio_chip_t chip_type;
    bool tx_only;                 // No RX path
    uint8_t xtal_cap;             // 0..15 pF
} mago_radio_cfg_t;

/* ============================================================================
 * GPS CONFIG - u-blox UBX
 * ============================================================================ */

typedef enum {
    MAGO_GPS_MODEL_AUTO = 0,
    MAGO_GPS_MODEL_MAX6 = 6,
    MAGO_GPS_MODEL_MAX7 = 7,
    MAGO_GPS_MODEL_MAX8 = 8,
} mago_gps_model_t;

typedef enum {
    MAGO_GPS_DYN_PORTABLE = 0,
    MAGO_GPS_DYN_STATIONARY = 2,
    MAGO_GPS_DYN_PEDESTRIAN = 3,
    MAGO_GPS_DYN_AUTOMOTIVE = 4,
    MAGO_GPS_DYN_SEA = 5,
    MAGO_GPS_DYN_AIRBORNE_1G = 6,
    MAGO_GPS_DYN_AIRBORNE_2G = 7,
    MAGO_GPS_DYN_AIRBORNE_4G = 8,
} mago_gps_dynamic_t;

typedef struct {
    uint32_t baud_init;
    uint32_t baud_operational;
    mago_gps_model_t model;
    mago_gps_dynamic_t dynamic_model;
    uint8_t fix_mode;             // 1=2D, 3=3D
    uint16_t min_elev_deg;        // 0..90
    uint16_t pdop_mask;           // *100
    uint8_t powersave_mode;       // 0=none, 1=cyclic, 2=eco
    bool auto_restart;
    uint16_t restart_timeout_s;
    bool enable_glonass;
    bool enable_galileo;
} mago_gps_cfg_t;

/* ============================================================================
 * PROTOCOL CONFIG - Unificado para todos
 * ============================================================================ */

typedef enum {
    MAGO_PROTO_NONE = 0,
    MAGO_PROTO_RTTY,
    MAGO_PROTO_MFSK,
    MAGO_PROTO_HORUS_V1,
    MAGO_PROTO_HORUS_V2,
    MAGO_PROTO_APRS,
    MAGO_PROTO_MORSE,
    MAGO_PROTO_CUSTOM,
} mago_protocol_id_t;

typedef struct {
    mago_protocol_id_t id;
    char name[16];
    bool enabled;
    uint8_t priority;             // 0=highest
    uint32_t interval_ms;         // 0 = event-driven
    uint32_t offset_ms;           // Phase offset
    void* params;                 // Protocol-specific params
} mago_protocol_cfg_t;

/* RTTY params */
typedef struct {
    uint16_t baud;
    uint16_t shift_hz;
    uint8_t bits;
    uint8_t stop_bits;
    bool extended_ascii;
} mago_rtty_params_t;

/* MFSK/Horus params */
typedef struct {
    uint16_t baud;
    uint8_t modulation;           // 2=2FSK, 4=4FSK, 16=16FSK
    uint16_t deviation_hz;
    bool fec_enabled;             // Golay(24,12)
    uint8_t interleaving_depth;
    bool scrambling;
    uint16_t payload_id;
    uint8_t preamble_symbols;
    uint8_t sync_symbols;
} mago_mfsk_params_t;

/* APRS params */
typedef struct {
    char callsign[10];
    uint8_t ssid;
    char path[32];                // "WIDE1-1,WIDE2-1"
    char symbol_table;
    char symbol_code;
    char comment[64];
    bool dao_enabled;
    uint8_t beacon_ratio;         // 1 = every tx
} mago_aprs_params_t;

/* Morse params */
typedef struct {
    char message[128];
    uint16_t wpm;
    uint8_t farnsworth_wpm;
    uint16_t tone_hz;
    uint8_t interval_cycles;
} mago_morse_params_t;

/* ============================================================================
 * POWER CONFIG
 * ============================================================================ */

typedef enum {
    MAGO_PWR_BATT_LI_SOCL2_2S = 0,  // 2x AA Li-SOCl2 (RS41 stock)
    MAGO_PWR_BATT_LI_ION_1S = 1,    // 1x Li-Ion
    MAGO_PWR_BATT_LI_FE_2S = 2,     // 2x LiFePO4
    MAGO_PWR_BATT_CUSTOM = 3,
} mago_battery_type_t;

typedef struct {
    mago_battery_type_t type;
    uint16_t v_min_mv;
    uint16_t v_max_mv;
    uint16_t v_warn_mv;
    uint16_t v_critical_mv;
    float divider_ratio;          // ADC divider
    float temp_coeff;             // mV/°C
    uint32_t sleep_current_ua;    // For runtime estimation
    uint32_t tx_current_ma;       // Peak TX current
} mago_power_cfg_t;

/* ============================================================================
 * SYSTEM CONFIG - Agregado principal
 * ============================================================================ */

#define MAGO_MAX_PROTOCOLS    8
#define MAGO_MAX_CALLBACKS    16
#define MAGO_EVENT_QUEUE_SZ   32

typedef struct {
    // Metadata
    char device_name[16];
    uint16_t device_id;
    uint32_t serial_number;

    // Subsystems
    mago_board_pins_t pins;
    mago_radio_cfg_t radio;
    mago_gps_cfg_t gps;
    mago_power_cfg_t power;

    // Protocols (array of configs)
    uint8_t protocol_count;
    mago_protocol_cfg_t protocols[MAGO_MAX_PROTOCOLS];

    // Behavior
    uint32_t main_loop_period_ms;
    uint8_t log_level;            // 0=none, 1=error, 2=warn, 3=info, 4=debug
    bool watchdog_enabled;
    uint32_t watchdog_timeout_ms;
    bool deep_sleep_enabled;
    uint16_t deep_sleep_minutes;

    // Security
    bool encrypt_telemetry;
    uint8_t aes_key[16];

    // CRC of config struct (for validation)
    uint32_t config_crc;
} mago_system_config_t;

/* ============================================================================
 * RUNTIME STATE - Separado de config
 * ============================================================================ */

typedef struct {
    uint32_t boot_count;
    uint32_t uptime_seconds;
    uint32_t tx_count_total;
    uint32_t tx_count_success;
    uint32_t gps_fix_count;
    uint32_t error_count;
    uint16_t last_battery_mv;
    int16_t last_temp_c;
    uint32_t last_error_code;
    uint32_t last_error_time;
} mago_runtime_stats_t;

/* ============================================================================
 * API FUNCTIONS
 * ============================================================================ */

// Config loading/storing
bool mago_config_load(mago_system_config_t* cfg);
bool mago_config_save(const mago_system_config_t* cfg);
void mago_config_defaults(mago_system_config_t* cfg);
bool mago_config_validate(const mago_system_config_t* cfg);
uint32_t mago_config_crc32(const void* data, size_t len);

// Runtime stats
void mago_stats_init(void);
void mago_stats_update_tx(bool success);
void mago_stats_update_gps(bool fix);
void mago_stats_update_power(uint16_t mv, int16_t temp_c);
const mago_runtime_stats_t* mago_stats_get(void);

// Parameter access helpers
const mago_rtty_params_t* mago_config_get_rtty(const mago_protocol_cfg_t* p);
const mago_mfsk_params_t* mago_config_get_mfsk(const mago_protocol_cfg_t* p);
const mago_aprs_params_t* mago_config_get_aprs(const mago_protocol_cfg_t* p);
const mago_morse_params_t* mago_config_get_morse(const mago_protocol_cfg_t* p);

#endif // MAGO_CONFIG_H