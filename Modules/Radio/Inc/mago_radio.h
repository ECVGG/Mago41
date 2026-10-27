/**
 * @file mago_radio.h
 * @brief Mago41 - Radio Module (SI4032/SI4432)
 *
 * Diferencia radical vs RS41HUP_V2:
 * - Driver puro C, sin Arduino
 * - SPI abstracto via HAL vtable
 * - Config declarativa (struct + validación)
 * - Event-driven: TX done via message bus
 * - No hay "radio task" bloqueante
 */

#ifndef MAGO_RADIO_H
#define MAGO_RADIO_H

#include <stdint.h>
#include <stdbool.h>
#include "mago_types.h"
#include "mago_config.h"

/* ============================================================================
 * SI4032 REGISTER MAP
 * ============================================================================ */

#define SI4032_REG_DEVICE_TYPE        0x00
#define SI4032_REG_DEVICE_VERSION     0x01
#define SI4032_REG_DEVICE_STATUS      0x02
#define SI4032_REG_INT_STATUS_1       0x03
#define SI4032_REG_INT_STATUS_2       0x04
#define SI4032_REG_INT_ENABLE_1       0x05
#define SI4032_REG_INT_ENABLE_2       0x06
#define SI4032_REG_OP_FUNC_CTRL_1     0x07
#define SI4032_REG_OP_FUNC_CTRL_2     0x08
#define SI4032_REG_CRYSTAL_LOAD       0x09
#define SI4032_REG_MICRO_CTRL         0x0A
#define SI4032_REG_GPIO_0_CONF        0x0B
#define SI4032_REG_GPIO_1_CONF        0x0C
#define SI4032_REG_GPIO_2_CONF        0x0D
#define SI4032_REG_IO_PORT_CONF       0x0E
#define SI4032_REG_ADC8_CTRL          0x0F
#define SI4032_REG_ADC8               0x10
#define SI4032_REG_TEMP_SENSOR_CTRL   0x11
#define SI4032_REG_TEMP_VALUE         0x12
#define SI4032_REG_WAKEUP_TIMER_LSB   0x13
#define SI4032_REG_WAKEUP_TIMER_MSB   0x14
#define SI4032_REG_LOW_BATT_DETECT    0x15
#define SI4032_REG_AFC_TIMING_CTRL    0x16
#define SI4032_REG_AFC_LIMITER        0x17
#define SI4032_REG_PREAMBLE_DET_CTRL  0x18
#define SI4032_REG_PREAMBLE_LEN       0x19
#define SI4032_REG_SYNC_WORD_3        0x1A
#define SI4032_REG_SYNC_WORD_2        0x1B
#define SI4032_REG_SYNC_WORD_1        0x1C
#define SI4032_REG_SYNC_WORD_0        0x1D
#define SI4032_REG_TX_HEADER_3        0x1E
#define SI4032_REG_TX_HEADER_2        0x1F
#define SI4032_REG_TX_HEADER_1        0x20
#define SI4032_REG_TX_HEADER_0        0x21
#define SI4032_REG_TX_PKT_LEN         0x22
#define SI4032_REG_RX_PKT_LEN         0x22
#define SI4032_REG_PKT_CTRL_1         0x23
#define SI4032_REG_PKT_CTRL_2         0x24
#define SI4032_REG_RX_ALMOST_FULL     0x25
#define SI4032_REG_TX_ALMOST_EMPTY    0x26
#define SI4032_REG_FREQ_BAND_SEL      0x27
#define SI4032_REG_NOM_CARRIER_FREQ_1 0x28
#define SI4032_REG_NOM_CARRIER_FREQ_0 0x29
#define SI4032_REG_FREQ_HOPPING_STEP  0x2A
#define SI4032_REG_TX_FREQ_DEV        0x2B
#define SI4032_REG_TX_PWR             0x2C
#define SI4032_REG_TX_DATA_RATE_1     0x2D
#define SI4032_REG_TX_DATA_RATE_0     0x2E
#define SI4032_REG_MOD_MODE_CTRL_1    0x2F
#define SI4032_REG_MOD_MODE_CTRL_2    0x30
#define SI4032_REG_IF_FILTER_BW       0x31
#define SI4032_REG_AFC_LOOP_GEARSHIFT 0x32
#define SI4032_REG_CLOCK_RECOVERY_GS  0x33
#define SI4032_REG_CLOCK_RECOVERY_OV  0x34
#define SI4032_REG_CLOCK_RECOVERY_LP  0x35
#define SI4032_REG_RSSI               0x36
#define SI4032_REG_LNA_CTRL           0x37
#define SI4032_REG_AFC_CORRECTION     0x38
#define SI4032_REG_OOK_COUNTER_1      0x39
#define SI4032_REG_OOK_COUNTER_0      0x3A
#define SI4032_REG_SLICER_PEAK_HOLD   0x3B
#define SI4032_REG_TX_FIFO_CTRL_1     0x3C
#define SI4032_REG_TX_FIFO_CTRL_2     0x3D
#define SI4032_REG_RX_FIFO_CTRL       0x3E
#define SI4032_REG_FIFO_ACCESS        0x7F

/* ============================================================================
 * SI4032 COMMANDS
 * ============================================================================ */

#define SI4032_CMD_READ       0x80
#define SI4032_CMD_WRITE      0x00
#define SI4032_CMD_BURST_READ 0xC0
#define SI4032_CMD_BURST_WRITE 0x40

/* ============================================================================
 * RADIO STATE MACHINE
 * ============================================================================ */

typedef enum {
    MAGO_RADIO_STATE_UNINIT = 0,
    MAGO_RADIO_STATE_IDLE,
    MAGO_RADIO_STATE_TX_PREP,
    MAGO_RADIO_STATE_TX_ACTIVE,
    MAGO_RADIO_STATE_RX_ACTIVE,
    MAGO_RADIO_STATE_ERROR,
} mago_radio_state_t;

/* ============================================================================
 * RADIO DATA STRUCT
 * ============================================================================ */

typedef struct {
    mago_radio_state_t state;
    mago_radio_cfg_t config;
    uint8_t tx_buffer[256];
    uint16_t tx_len;
    uint8_t rx_buffer[256];
    uint16_t rx_len;
    uint8_t device_type;
    uint8_t device_version;
    int16_t last_rssi;
    uint32_t tx_count;
    uint32_t rx_count;
    uint32_t error_count;
} mago_radio_data_t;

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

mago_result_t mago_radio_init(void* ctx, const mago_radio_cfg_t* cfg);
mago_result_t mago_radio_start(void* ctx);
mago_result_t mago_radio_tick(void* ctx);
void mago_radio_handle_event(void* ctx, const mago_event_t* evt);
mago_result_t mago_radio_stop(void* ctx);
mago_result_t mago_radio_deinit(void* ctx);

mago_result_t mago_radio_transmit(void* ctx, const uint8_t* data, size_t len);
mago_result_t mago_radio_set_freq(void* ctx, float freq_mhz);
mago_result_t mago_radio_set_power(void* ctx, int8_t power_dbm);
mago_result_t mago_radio_set_modulation(void* ctx, uint8_t modulation);
int16_t mago_radio_get_rssi(void* ctx);
mago_radio_state_t mago_radio_get_state(void* ctx);
void mago_radio_get_stats(void* ctx, uint32_t* tx_count, uint32_t* rx_count, uint32_t* errors);

const mago_component_factory_t* mago_radio_factory(void);

#endif // MAGO_RADIO_H