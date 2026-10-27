/**
 * @file mago_radio.c
 * @brief Mago41 - Radio Module Implementation (SI4032)
 */

#include "mago_radio.h"
#include "mago_hal.h"
#include "mago_eventbus.h"
#include <string.h>

/* ============================================================================
 * STATIC DATA
 * ============================================================================ */

static mago_radio_data_t g_radio;

/* ============================================================================
 * SI4032 REGISTER ACCESS
 * ============================================================================ */

static mago_result_t si4032_write_reg(uint8_t reg, uint8_t val) {
    uint8_t tx[2] = { reg & 0x7F, val };
    MAGO_GPIO_WRITE(&g_mago_pins.radio_cs, false);
    mago_result_t res = MAGO_HAL_GET()->spi->write(tx, 2);
    MAGO_GPIO_WRITE(&g_mago_pins.radio_cs, true);
    return res;
}

static mago_result_t si4032_read_reg(uint8_t reg, uint8_t* val) {
    if (!val) return MAGO_ERR_INVAL;
    uint8_t tx[2] = { reg | 0x80, 0xFF };
    uint8_t rx[2];
    MAGO_GPIO_WRITE(&g_mago_pins.radio_cs, false);
    mago_result_t res = MAGO_HAL_GET()->spi->transfer(tx, rx, 2);
    MAGO_GPIO_WRITE(&g_mago_pins.radio_cs, true);
    *val = rx[1];
    return res;
}

static mago_result_t si4032_write_burst(uint8_t reg, const uint8_t* data, size_t len) {
    uint8_t cmd = reg & 0x3F; // Burst write uses lower 6 bits
    MAGO_GPIO_WRITE(&g_mago_pins.radio_cs, false);
    mago_result_t res = MAGO_HAL_GET()->spi->write(&cmd, 1);
    if (MAGO_SUCCEEDED(res)) {
        res = MAGO_HAL_GET()->spi->write(data, len);
    }
    MAGO_GPIO_WRITE(&g_mago_pins.radio_cs, true);
    return res;
}

static mago_result_t si4032_read_burst(uint8_t reg, uint8_t* data, size_t len) {
    if (!data) return MAGO_ERR_INVAL;
    uint8_t cmd = reg | 0xC0; // Burst read
    MAGO_GPIO_WRITE(&g_mago_pins.radio_cs, false);
    mago_result_t res = MAGO_HAL_GET()->spi->write(&cmd, 1);
    if (MAGO_SUCCEEDED(res)) {
        res = MAGO_HAL_GET()->spi->read(data, len);
    }
    MAGO_GPIO_WRITE(&g_mago_pins.radio_cs, true);
    return res;
}

/* ============================================================================
 * SI4032 INITIALIZATION SEQUENCE
 * ============================================================================ */

static mago_result_t si4032_reset(void) {
    MAGO_GPIO_WRITE(&g_mago_pins.radio_sdn, true);
    MAGO_DELAY_MS(10);
    MAGO_GPIO_WRITE(&g_mago_pins.radio_sdn, false);
    MAGO_DELAY_MS(10);
    return MAGO_OK;
}

static mago_result_t si4032_check_device(void) {
    uint8_t type, version;
    mago_result_t res = si4032_read_reg(SI4032_REG_DEVICE_TYPE, &type);
    if (MAGO_FAILED(res)) return res;
    res = si4032_read_reg(SI4032_REG_DEVICE_VERSION, &version);
    if (MAGO_FAILED(res)) return res;

    g_radio.device_type = type;
    g_radio.device_version = version;

    // SI4032 = 0x04, SI4432 = 0x04 (same type, diff version)
    if (type != 0x04) {
        return MAGO_ERR_NODEV;
    }
    return MAGO_OK;
}

static mago_result_t si4032_config_defaults(void) {
    // Power on reset state
    si4032_write_reg(SI4032_REG_OP_FUNC_CTRL_1, 0x01); // SW reset
    MAGO_DELAY_MS(10);

    // Crystal load capacitance
    si4032_write_reg(SI4032_REG_CRYSTAL_LOAD, 0x7F | (g_radio.config.xtal_cap & 0x7F));

    // GPIO configuration: GPIO0 = TX_DATA, GPIO1 = RX_DATA
    si4032_write_reg(SI4032_REG_GPIO_0_CONF, 0x12); // TX data
    si4032_write_reg(SI4032_REG_GPIO_1_CONF, 0x15); // RX data
    si4032_write_reg(SI4032_REG_GPIO_2_CONF, 0x18); // Clock out

    // Interrupt enables: PKT_SENT, PKT_RX, CRC_ERROR, FIFO_OVF
    si4032_write_reg(SI4032_REG_INT_ENABLE_1, 0x07); // Packet sent, CRC error, FIFO overflow
    si4032_write_reg(SI4032_REG_INT_ENABLE_2, 0x03); // Packet received, Preamble detect

    // Packet handler: CRC-16(IBM), no Manchester, Preamble 0xAA
    si4032_write_reg(SI4032_REG_PKT_CTRL_1, 0x04 | 0x02 | 0x01); // CRC-16, preamble 4 bytes
    si4032_write_reg(SI4032_REG_PKT_CTRL_2, 0x40 | 0x10); // Preamble detection, sync word

    // Sync word (default 0x2DD4)
    si4032_write_reg(SI4032_REG_SYNC_WORD_3, 0x2D);
    si4032_write_reg(SI4032_REG_SYNC_WORD_2, 0xD4);
    si4032_write_reg(SI4032_REG_SYNC_WORD_1, 0x00);
    si4032_write_reg(SI4032_REG_SYNC_WORD_0, 0x00);

    // Preamble length
    si4032_write_reg(SI4032_REG_PREAMBLE_LEN, 8); // 8 bytes

    // Frequency band
    uint8_t band;
    if (g_radio.config.freq_mhz >= 430 && g_radio.config.freq_mhz < 440) {
        band = 0x51; // 430-440 MHz
    } else if (g_radio.config.freq_mhz >= 860 && g_radio.config.freq_mhz < 880) {
        band = 0x56; // 860-880 MHz
    } else if (g_radio.config.freq_mhz >= 900 && g_radio.config.freq_mhz < 930) {
        band = 0x5C; // 900-930 MHz
    } else {
        band = 0x51; // Default 430-440
    }
    si4032_write_reg(SI4032_REG_FREQ_BAND_SEL, band);

    // Nominal carrier frequency: Fc = (Fband + Fcar) * 62500
    // Fcar = (Fc/62500) - Fband
    uint32_t fcar = (uint32_t)((g_radio.config.freq_mhz * 1e6) / 62500) - (band & 0x7F);
    si4032_write_reg(SI4032_REG_NOM_CARRIER_FREQ_1, (fcar >> 8) & 0xFF);
    si4032_write_reg(SI4032_REG_NOM_CARRIER_FREQ_0, fcar & 0xFF);

    // Frequency deviation
    uint16_t fdev = (uint16_t)(g_radio.config.deviation_hz / 625);
    si4032_write_reg(SI4032_REG_TX_FREQ_DEV, fdev & 0xFF);

    // TX power
    int8_t power = g_radio.config.power_dbm;
    if (power > 13) power = 13;
    if (power < -30) power = -30;
    // PA power: 0x00 = -30dBm, 0x1F = +13dBm (approx)
    uint8_t pa = (power + 30) / 2;
    si4032_write_reg(SI4032_REG_TX_PWR, pa & 0x1F);

    // Data rate (default 4.8kbps for 2FSK)
    si4032_write_reg(SI4032_REG_TX_DATA_RATE_1, 0x01);
    si4032_write_reg(SI4032_REG_TX_DATA_RATE_0, 0x8A);

    // Modulation: 2FSK, no Manchester
    si4032_write_reg(SI4032_REG_MOD_MODE_CTRL_1, 0x00); // 2FSK
    si4032_write_reg(SI4032_REG_MOD_MODE_CTRL_2, 0x00);

    // IF filter bandwidth
    si4032_write_reg(SI4032_REG_IF_FILTER_BW, 0x02); // ~100kHz

    // AFC
    si4032_write_reg(SI4032_REG_AFC_TIMING_CTRL, 0x00);
    si4032_write_reg(SI4032_REG_AFC_LIMITER, 0x00);

    // Clock recovery
    si4032_write_reg(SI4032_REG_CLOCK_RECOVERY_GS, 0x1C);
    si4032_write_reg(SI4032_REG_CLOCK_RECOVERY_OV, 0x0D);
    si4032_write_reg(SI4032_REG_CLOCK_RECOVERY_LP, 0x28);

    // FIFO thresholds
    si4032_write_reg(SI4032_REG_TX_FIFO_CTRL_1, 0x20); // 32 bytes
    si4032_write_reg(SI4032_REG_TX_FIFO_CTRL_2, 0x0A);
    si4032_write_reg(SI4032_REG_RX_FIFO_CTRL, 0x20);

    return MAGO_OK;
}

/* ============================================================================
 * RADIO STATE MACHINE
 * ============================================================================ */

static mago_result_t radio_set_state(mago_radio_state_t new_state) {
    g_radio.state = new_state;
    return MAGO_OK;
}

static void radio_start_tx(void) {
    // Clear TX FIFO
    si4032_write_reg(SI4032_REG_OP_FUNC_CTRL_2, 0x02); // FIFO reset
    MAGO_DELAY_MS(1);

    // Write packet to FIFO
    si4032_write_burst(SI4032_REG_FIFO_ACCESS, g_radio.tx_buffer, g_radio.tx_len);

    // Start TX
    si4032_write_reg(SI4032_REG_OP_FUNC_CTRL_1, 0x09); // TX on
    radio_set_state(MAGO_RADIO_STATE_TX_ACTIVE);
}

static void radio_start_rx(void) {
    // Clear RX FIFO
    si4032_write_reg(SI4032_REG_OP_FUNC_CTRL_2, 0x01); // FIFO reset
    MAGO_DELAY_MS(1);

    // Start RX
    si4032_write_reg(SI4032_REG_OP_FUNC_CTRL_1, 0x05); // RX on
    radio_set_state(MAGO_RADIO_STATE_RX_ACTIVE);
}

static void radio_go_idle(void) {
    si4032_write_reg(SI4032_REG_OP_FUNC_CTRL_1, 0x01); // Idle
    radio_set_state(MAGO_RADIO_STATE_IDLE);
}

/* ============================================================================
 * COMPONENT CALLBACKS
 * ============================================================================ */

mago_result_t mago_radio_init(void* ctx, const mago_radio_cfg_t* cfg) {
    if (!cfg) return MAGO_ERR_INVAL;

    memset(&g_radio, 0, sizeof(g_radio));
    g_radio.config = *cfg;

    // Initialize HAL
    mago_result_t res = si4032_reset();
    if (MAGO_FAILED(res)) return res;

    res = si4032_check_device();
    if (MAGO_FAILED(res)) return res;

    res = si4032_config_defaults();
    if (MAGO_FAILED(res)) return res;

    radio_set_state(MAGO_RADIO_STATE_IDLE);
    return MAGO_OK;
}

mago_result_t mago_radio_start(void* ctx) {
    radio_set_state(MAGO_RADIO_STATE_IDLE);
    return MAGO_OK;
}

mago_result_t mago_radio_tick(void* ctx) {
    // Check for interrupts via GPIO
    if (!MAGO_GPIO_READ(&g_mago_pins.radio_nirq)) {
        uint8_t int1, int2;
        si4032_read_reg(SI4032_REG_INT_STATUS_1, &int1);
        si4032_read_reg(SI4032_REG_INT_STATUS_2, &int2);

        if (int1 & 0x04) { // PKT_SENT
            g_radio.tx_count++;
            radio_go_idle();
            mago_event_t evt = { .id = MAGO_EVT_RADIO_TX_DONE, .timestamp_us = MAGO_TIMER_GET_US() };
            mago_eventbus_post(&evt);
        }
        if (int2 & 0x01) { // PKT_RX
            g_radio.rx_count++;
            // Read packet length
            uint8_t len;
            si4032_read_reg(SI4032_REG_RX_PKT_LEN, &len);
            if (len > 0 && len <= sizeof(g_radio.rx_buffer)) {
                si4032_read_burst(SI4032_REG_FIFO_ACCESS, g_radio.rx_buffer, len);
                g_radio.rx_len = len;

                // Read RSSI
                uint8_t rssi_raw;
                si4032_read_reg(SI4032_REG_RSSI, &rssi_raw);
                g_radio.last_rssi = (int16_t)rssi_raw / 2 - 73;

                mago_event_t evt = {
                    .id = MAGO_EVT_RADIO_RX_PACKET,
                    .timestamp_us = MAGO_TIMER_GET_US(),
                    .data.radio_rx = { .rssi_dbm = g_radio.last_rssi, .snr_db = 0, .freq_offset_hz = 0 }
                };
                mago_eventbus_post(&evt);
            }
            radio_go_idle();
        }
        if (int1 & 0x02) { // CRC_ERROR
            g_radio.error_count++;
            radio_go_idle();
        }
    }
    return MAGO_OK;
}

void mago_radio_handle_event(void* ctx, const mago_event_t* evt) {
    if (!evt) return;

    switch (evt->id) {
        case MAGO_EVT_GPS_FIX:
            // Could adjust frequency based on temperature
            break;
        case MAGO_EVT_RADIO_TX_DONE:
            break;
        default:
            break;
    }
}

mago_result_t mago_radio_stop(void* ctx) {
    radio_go_idle();
    return MAGO_OK;
}

mago_result_t mago_radio_deinit(void* ctx) {
    MAGO_GPIO_WRITE(&g_mago_pins.radio_sdn, true);
    return MAGO_OK;
}

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

mago_result_t mago_radio_transmit(void* ctx, const uint8_t* data, size_t len) {
    if (!data || len == 0 || len > sizeof(g_radio.tx_buffer)) return MAGO_ERR_INVAL;
    if (g_radio.state != MAGO_RADIO_STATE_IDLE) return MAGO_ERR_BUSY;

    memcpy(g_radio.tx_buffer, data, len);
    g_radio.tx_len = len;

    radio_start_tx();
    return MAGO_OK;
}

mago_result_t mago_radio_set_freq(void* ctx, float freq_mhz) {
    if (freq_mhz < 400 || freq_mhz > 950) return MAGO_ERR_INVAL;
    g_radio.config.freq_mhz = freq_mhz;
    // Reconfigure frequency registers
    uint32_t fcar = (uint32_t)((freq_mhz * 1e6) / 62500);
    uint8_t band = si4032_read_reg(SI4032_REG_FREQ_BAND_SEL, &band) == MAGO_OK ? band : 0x51;
    fcar -= (band & 0x7F);
    si4032_write_reg(SI4032_REG_NOM_CARRIER_FREQ_1, (fcar >> 8) & 0xFF);
    si4032_write_reg(SI4032_REG_NOM_CARRIER_FREQ_0, fcar & 0xFF);
    return MAGO_OK;
}

mago_result_t mago_radio_set_power(void* ctx, int8_t power_dbm) {
    if (power_dbm > 13) power_dbm = 13;
    if (power_dbm < -30) power_dbm = -30;
    g_radio.config.power_dbm = power_dbm;
    uint8_t pa = (power_dbm + 30) / 2;
    si4032_write_reg(SI4032_REG_TX_PWR, pa & 0x1F);
    return MAGO_OK;
}

mago_result_t mago_radio_set_modulation(void* ctx, uint8_t modulation) {
    // 0=2FSK, 1=4FSK, 2=OOK
    g_radio.config.modulation = modulation;
    uint8_t val = (modulation == 0) ? 0x00 : (modulation == 1 ? 0x20 : 0x80);
    si4032_write_reg(SI4032_REG_MOD_MODE_CTRL_1, val);
    return MAGO_OK;
}

int16_t mago_radio_get_rssi(void* ctx) {
    return g_radio.last_rssi;
}

mago_radio_state_t mago_radio_get_state(void* ctx) {
    return g_radio.state;
}

void mago_radio_get_stats(void* ctx, uint32_t* tx_count, uint32_t* rx_count, uint32_t* errors) {
    if (tx_count) *tx_count = g_radio.tx_count;
    if (rx_count) *rx_count = g_radio.rx_count;
    if (errors) *errors = g_radio.error_count;
}

/* ============================================================================
 * FACTORY
 * ============================================================================ */

const mago_component_factory_t* mago_radio_factory(void) {
    static const mago_component_factory_t factory = {
        .id = MAGO_COMP_RADIO,
        .name = "radio",
        .priority = 10,
        .init = mago_radio_init,
        .start = mago_radio_start,
        .tick = mago_radio_tick,
        .handle_event = mago_radio_handle_event,
        .stop = mago_radio_stop,
        .deinit = mago_radio_deinit,
        .create_data = NULL,
        .data_size = sizeof(mago_radio_data_t),
    };
    return &factory;
}