/**
 * @file mago_gps.c
 * @brief Mago41 - GPS Module Implementation (u-blox UBX Parser)
 */

#include "mago_gps.h"
#include "mago_hal.h"
#include "mago_eventbus.h"
#include "mago_config.h"
#include <string.h>

/* ============================================================================
 * STATIC DATA
 * ============================================================================ */

static mago_gps_data_t g_gps;

/* ============================================================================
 * UBX CHECKSUM
 * ============================================================================ */

static void ubx_checksum(const uint8_t* data, size_t len, uint8_t* ck_a, uint8_t* ck_b) {
    uint8_t a = 0, b = 0;
    for (size_t i = 0; i < len; i++) {
        a += data[i];
        b += a;
    }
    *ck_a = a;
    *ck_b = b;
}

static bool ubx_verify_checksum(const uint8_t* data, size_t len, uint8_t ck_a, uint8_t ck_b) {
    uint8_t ca, cb;
    ubx_checksum(data, len, &ca, &cb);
    return (ca == ck_a && cb == ck_b);
}

/* ============================================================================
 * UBX MESSAGE SENDING
 * ============================================================================ */

static mago_result_t ubx_send_msg(uint8_t class_id, uint8_t msg_id, const uint8_t* payload, uint16_t len) {
    uint8_t header[6] = { UBX_SYNC_1, UBX_SYNC_2, class_id, msg_id, len & 0xFF, (len >> 8) & 0xFF };
    uint8_t ck_a, ck_b;
    ubx_checksum(&header[2], 4 + len, &ck_a, &ck_b);

    MAGO_HAL_GET()->uart->write(1, header, 6);
    if (payload && len > 0) {
        MAGO_HAL_GET()->uart->write(1, payload, len);
    }
    uint8_t cks[2] = { ck_a, ck_b };
    MAGO_HAL_GET()->uart->write(1, cks, 2);
    return MAGO_OK;
}

/* ============================================================================
 * UBX CONFIGURATION MESSAGES
 * ============================================================================ */

static void gps_configure_port(void) {
    // CFG-PRT: Set UART1 to 38400 8N1, UBX in/out
    uint8_t payload[20] = {0};
    payload[0] = 1; // Port ID: UART1
    payload[2] = 0x08; // TX ready
    payload[3] = 0x00;
    // Mode: 8N1
    payload[6] = 0x00;
    payload[7] = 0x08 | 0x00; // 8 bits, no parity
    // Baud rate
    uint32_t baud = g_gps.config.baud_operational;
    payload[8] = baud & 0xFF;
    payload[9] = (baud >> 8) & 0xFF;
    payload[10] = (baud >> 16) & 0xFF;
    payload[11] = (baud >> 24) & 0xFF;
    // In/out protocol mask: UBX only
    payload[12] = 0x01;
    payload[13] = 0x00;
    payload[14] = 0x01;
    payload[15] = 0x00;
    // Flags
    payload[16] = 0x00;
    payload[17] = 0x00;
    payload[18] = 0x00;
    payload[19] = 0x00;
    ubx_send_msg(UBX_CLASS_CFG, UBX_CFG_PRT, payload, 20);
}

static void gps_configure_messages(void) {
    // Enable NAV-PVT (periodic)
    uint8_t payload[3] = { UBX_CLASS_NAV, UBX_NAV_PVT, 1 };
    ubx_send_msg(UBX_CLASS_CFG, UBX_CFG_MSG, payload, 3);

    // Enable NAV-STATUS
    payload[1] = UBX_NAV_STATUS;
    ubx_send_msg(UBX_CLASS_CFG, UBX_CFG_MSG, payload, 3);

    // Enable NAV-TIMEUTC
    payload[1] = UBX_NAV_TIMEUTC;
    ubx_send_msg(UBX_CLASS_CFG, UBX_CFG_MSG, payload, 3);

    // Disable NMEA
    for (uint8_t i = 0; i < 0xF0; i++) {
        payload[0] = 0xF0;
        payload[1] = i;
        payload[2] = 0;
        ubx_send_msg(UBX_CLASS_CFG, UBX_CFG_MSG, payload, 3);
    }
}

static void gps_configure_rate(void) {
    // CFG-RATE: 1Hz navigation rate
    uint8_t payload[6] = { 0xC8, 0x00, 0x01, 0x00, 0x01, 0x00 }; // 200ms, 1 cycle, GPS time
    ubx_send_msg(UBX_CLASS_CFG, UBX_CFG_RATE, payload, 6);
}

static void gps_configure_nav5(void) {
    // CFG-NAV5: Dynamic model
    uint8_t payload[36] = {0};
    payload[0] = 0x01; // Mask: dyn model
    payload[2] = g_gps.config.dynamic_model; // Airborne 1G
    payload[4] = g_gps.config.fix_mode; // 3D only
    ubx_send_msg(UBX_CLASS_CFG, UBX_CFG_NAV5, payload, 36);
}

static void gps_configure_gnss(void) {
    // CFG-GNSS: Enable GPS + GLONASS + Galileo
    uint8_t payload[44] = {0};
    payload[0] = 0; // Version
    payload[1] = 3; // 3 blocks (GPS, GLO, GAL)
    // GPS
    payload[4] = 0; // GPS
    payload[5] = 1; // Enabled
    payload[6] = 8; // Max channels
    payload[7] = 0;
    // GLONASS
    payload[8] = 6; // GLO
    payload[9] = g_gps.config.enable_glonass ? 1 : 0;
    payload[10] = 8;
    payload[11] = 0;
    // Galileo
    payload[12] = 2; // GAL
    payload[13] = g_gps.config.enable_galileo ? 1 : 0;
    payload[14] = 4;
    payload[15] = 0;
    ubx_send_msg(UBX_CLASS_CFG, UBX_CFG_GNSS, payload, 44);
}

static void gps_configure_pm2(void) {
    // CFG-PM2: Power management
    uint8_t payload[44] = {0};
    payload[0] = 1; // Version
    payload[2] = g_gps.config.powersave_mode; // Power save mode
    ubx_send_msg(UBX_CLASS_CFG, UBX_CFG_PM2, payload, 44);
}

/* ============================================================================
 * UBX PARSER - NAV-PVT (0x01 0x07)
 * ============================================================================ */

static void parse_nav_pvt(const uint8_t* payload, uint16_t len) {
    if (len < 92) return;

    g_gps.nav.time_us = MAGO_TIMER_GET_US();
    g_gps.nav.valid = true;

    // iTOW (ms since GPS epoch)
    g_gps.nav.itow = payload[0] | (payload[1]<<8) | (payload[2]<<16) | (payload[3]<<24);

    // Year, month, day
    g_gps.nav.year = payload[4] | (payload[5]<<8);
    g_gps.nav.month = payload[6];
    g_gps.nav.day = payload[7];
    g_gps.nav.hour = payload[8];
    g_gps.nav.min = payload[9];
    g_gps.nav.sec = payload[10];

    // Validity flags
    uint8_t valid = payload[11];
    g_gps.nav.valid_date = valid & 0x01;
    g_gps.nav.valid_time = valid & 0x02;
    g_gps.nav.fully_resolved = valid & 0x04;
    g_gps.nav.valid_mag = valid & 0x08;

    // TAcc, Nano
    g_gps.nav.t_acc = payload[12] | (payload[13]<<8) | (payload[14]<<16) | (payload[15]<<24);
    g_gps.nav.nano = (int32_t)(payload[16] | (payload[17]<<8) | (payload[18]<<16) | (payload[19]<<24));

    // Fix type
    g_gps.nav.fix_type = payload[20];
    g_gps.nav.flags = payload[21];
    g_gps.nav.flags2 = payload[22];
    g_gps.nav.num_sv = payload[23];

    // Position (deg * 1e-7)
    g_gps.nav.lon = (int32_t)(payload[24] | (payload[25]<<8) | (payload[26]<<16) | (payload[27]<<24));
    g_gps.nav.lat = (int32_t)(payload[28] | (payload[29]<<8) | (payload[30]<<16) | (payload[31]<<24));
    g_gps.nav.height = (int32_t)(payload[32] | (payload[33]<<8) | (payload[34]<<16) | (payload[35]<<24));
    g_gps.nav.h_msl = (int32_t)(payload[36] | (payload[37]<<8) | (payload[38]<<16) | (payload[39]<<24));

    // Accuracy (mm)
    g_gps.nav.h_acc = payload[40] | (payload[41]<<8) | (payload[42]<<16) | (payload[43]<<24);
    g_gps.nav.v_acc = payload[44] | (payload[45]<<8) | (payload[46]<<16) | (payload[47]<<24);

    // Velocity (mm/s)
    g_gps.nav.vel_n = (int32_t)(payload[48] | (payload[49]<<8) | (payload[50]<<16) | (payload[51]<<24));
    g_gps.nav.vel_e = (int32_t)(payload[52] | (payload[53]<<8) | (payload[54]<<16) | (payload[55]<<24));
    g_gps.nav.vel_d = (int32_t)(payload[56] | (payload[57]<<8) | (payload[58]<<16) | (payload[59]<<24));
    g_gps.nav.g_speed = payload[60] | (payload[61]<<8) | (payload[62]<<16) | (payload[63]<<24);
    g_gps.nav.head_mot = (int32_t)(payload[64] | (payload[65]<<8) | (payload[66]<<16) | (payload[67]<<24));

    // Speed/heading accuracy
    g_gps.nav.s_acc = payload[68] | (payload[69]<<8) | (payload[70]<<16) | (payload[71]<<24);
    g_gps.nav.head_acc = payload[72] | (payload[73]<<8) | (payload[74]<<16) | (payload[75]<<24);

    // PDOP
    g_gps.nav.p_dop = payload[76] | (payload[77]<<8);

    // Flags3
    g_gps.nav.flags3 = payload[78];

    g_gps.has_fix = (g_gps.nav.fix_type >= 3);
    g_gps.has_time = g_gps.nav.valid_time && g_gps.nav.fully_resolved;
    g_gps.last_fix_time = MAGO_TIMER_GET_US() / 1000;

    // Post GPS fix event
    mago_event_t evt = {
        .id = MAGO_EVT_GPS_FIX,
        .timestamp_us = MAGO_TIMER_GET_US(),
        .data.gps_fix = {
            .lat = g_gps.nav.lat,
            .lon = g_gps.nav.lon,
            .alt = g_gps.nav.h_msl,
            .speed = g_gps.nav.g_speed,
            .heading = g_gps.nav.head_mot,
            .sats = g_gps.nav.num_sv,
            .hdop = g_gps.nav.p_dop,
            .fix_type = g_gps.nav.fix_type,
        }
    };
    mago_eventbus_post(&evt);
}

/* ============================================================================
 * UBX PARSER - NAV-STATUS (0x01 0x03)
 * ============================================================================ */

static void parse_nav_status(const uint8_t* payload, uint16_t len) {
    if (len < 16) return;
    g_gps.nav.fix_type = payload[4];
    g_gps.nav.flags = payload[5];
}

/* ============================================================================
 * UBX PARSER - NAV-TIMEUTC (0x01 0x21)
 * ============================================================================ */

static void parse_nav_timeutc(const uint8_t* payload, uint16_t len) {
    if (len < 20) return;
    g_gps.nav.year = payload[4] | (payload[5]<<8);
    g_gps.nav.month = payload[6];
    g_gps.nav.day = payload[7];
    g_gps.nav.hour = payload[8];
    g_gps.nav.min = payload[9];
    g_gps.nav.sec = payload[10];
    g_gps.has_time = true;
}

/* ============================================================================
 * UBX PARSER - ACK/NAK
 * ============================================================================ */

static void parse_ack(const uint8_t* payload, uint16_t len, bool ack) {
    if (len < 2) return;
    uint8_t cls = payload[0];
    uint8_t id = payload[1];
    // Could track config acknowledgment
}

/* ============================================================================
 * UBX PARSER DISPATCH
 * ============================================================================ */

static void ubx_dispatch_message(uint8_t class_id, uint8_t msg_id, const uint8_t* payload, uint16_t len) {
    g_gps.rx_messages++;

    switch (class_id) {
        case UBX_CLASS_NAV:
            switch (msg_id) {
                case UBX_NAV_PVT:
                    parse_nav_pvt(payload, len);
                    break;
                case UBX_NAV_STATUS:
                    parse_nav_status(payload, len);
                    break;
                case UBX_NAV_TIMEUTC:
                    parse_nav_timeutc(payload, len);
                    break;
                case UBX_NAV_POSLLH:
                case UBX_NAV_SOL:
                case UBX_NAV_VELNED:
                case UBX_NAV_SAT:
                case UBX_NAV_SIG:
                    // Could parse these too
                    break;
            }
            break;
        case UBX_CLASS_ACK:
            parse_ack(payload, len, msg_id == UBX_ACK_ACK);
            break;
        case UBX_CLASS_INF:
            // Info messages
            break;
        default:
            break;
    }
}

/* ============================================================================
 * UBX PARSER STATE MACHINE
 * ============================================================================ */

static void ubx_parse_byte(uint8_t byte) {
    g_gps.rx_bytes++;

    switch (g_gps.parser_state) {
        case UBX_STATE_SYNC1:
            if (byte == UBX_SYNC_1) g_gps.parser_state = UBX_STATE_SYNC2;
            break;
        case UBX_STATE_SYNC2:
            if (byte == UBX_SYNC_2) g_gps.parser_state = UBX_STATE_CLASS;
            else g_gps.parser_state = UBX_STATE_SYNC1;
            break;
        case UBX_STATE_CLASS:
            g_gps.msg_class = byte;
            g_gps.parser_state = UBX_STATE_ID;
            break;
        case UBX_STATE_ID:
            g_gps.msg_id = byte;
            g_gps.parser_state = UBX_STATE_LEN_L;
            break;
        case UBX_STATE_LEN_L:
            g_gps.msg_len = byte;
            g_gps.parser_state = UBX_STATE_LEN_H;
            break;
        case UBX_STATE_LEN_H:
            g_gps.msg_len |= (uint16_t)byte << 8;
            g_gps.payload_idx = 0;
            if (g_gps.msg_len > sizeof(g_gps.payload_buffer)) {
                g_gps.parser_state = UBX_STATE_SYNC1;
                g_gps.parse_errors++;
            } else if (g_gps.msg_len == 0) {
                g_gps.parser_state = UBX_STATE_CK_A;
            } else {
                g_gps.parser_state = UBX_STATE_PAYLOAD;
            }
            break;
        case UBX_STATE_PAYLOAD:
            g_gps.payload_buffer[g_gps.payload_idx++] = byte;
            if (g_gps.payload_idx >= g_gps.msg_len) {
                g_gps.parser_state = UBX_STATE_CK_A;
            }
            break;
        case UBX_STATE_CK_A:
            g_gps.ck_a = byte;
            g_gps.parser_state = UBX_STATE_CK_B;
            break;
        case UBX_STATE_CK_B:
            g_gps.ck_b = byte;
            if (ubx_verify_checksum(g_gps.payload_buffer, g_gps.msg_len, g_gps.ck_a, g_gps.ck_b)) {
                ubx_dispatch_message(g_gps.msg_class, g_gps.msg_id, g_gps.payload_buffer, g_gps.msg_len);
            } else {
                g_gps.crc_errors++;
            }
            g_gps.parser_state = UBX_STATE_SYNC1;
            break;
    }
}

/* ============================================================================
 * GPS CONFIG STATE MACHINE
 * ============================================================================ */

static void gps_config_step(void) {
    uint32_t now = MAGO_TIMER_GET_US() / 1000;
    if (now - g_gps.config_timeout < 100) return; // 100ms between steps

    switch (g_gps.config_state) {
        case 0:
            // Reset GPS
            ubx_send_msg(UBX_CLASS_CFG, UBX_CFG_RST, (uint8_t[]){0x01, 0x00, 0x00, 0x00}, 4);
            g_gps.config_state = 1;
            break;
        case 1:
            gps_configure_port();
            g_gps.config_state = 2;
            break;
        case 2:
            gps_configure_messages();
            g_gps.config_state = 3;
            break;
        case 3:
            gps_configure_rate();
            g_gps.config_state = 4;
            break;
        case 4:
            gps_configure_nav5();
            g_gps.config_state = 5;
            break;
        case 5:
            gps_configure_gnss();
            g_gps.config_state = 6;
            break;
        case 6:
            gps_configure_pm2();
            g_gps.config_state = 7;
            break;
        case 7:
            g_gps.config_state = 255; // Done
            break;
        default:
            break;
    }
    g_gps.config_timeout = now;
}

/* ============================================================================
 * COMPONENT CALLBACKS
 * ============================================================================ */

mago_result_t mago_gps_init(void* ctx, const mago_gps_cfg_t* cfg) {
    if (!cfg) return MAGO_ERR_INVAL;

    memset(&g_gps, 0, sizeof(g_gps));
    g_gps.config = *cfg;
    g_gps.parser_state = UBX_STATE_SYNC1;
    g_gps.config_state = 0;
    g_gps.config_timeout = MAGO_TIMER_GET_US() / 1000;

    // Initialize UART at init baud
    mago_uart_cfg_t uart_cfg = {
        .baudrate = cfg->baud_init,
        .databits = 8,
        .stopbits = 1,
        .parity = false,
    };
    MAGO_HAL_GET()->uart->init(1, &uart_cfg);

    // Reset GPS
    MAGO_GPIO_WRITE(&g_mago_pins.gps_rst, false);
    MAGO_DELAY_MS(100);
    MAGO_GPIO_WRITE(&g_mago_pins.gps_rst, true);
    MAGO_DELAY_MS(500);

    return MAGO_OK;
}

mago_result_t mago_gps_start(void* ctx) {
    g_gps.config_state = 0;
    return MAGO_OK;
}

mago_result_t mago_gps_tick(void* ctx) {
    // Process incoming UART data
    while (MAGO_HAL_GET()->uart->rx_ready(1)) {
        uint8_t byte;
        MAGO_HAL_GET()->uart->read(1, &byte, 1, 0);
        ubx_parse_byte(byte);
    }

    // Run config state machine
    if (g_gps.config_state < 255) {
        gps_config_step();
    }

    // Check for GPS restart
    if (g_gps.config.auto_restart && g_gps.has_fix == false) {
        uint32_t now = MAGO_TIMER_GET_US() / 1000;
        if (now - g_gps.last_fix_time > g_gps.config.restart_timeout_s * 1000) {
            // Restart GPS
            MAGO_GPIO_WRITE(&g_mago_pins.gps_rst, false);
            MAGO_DELAY_MS(100);
            MAGO_GPIO_WRITE(&g_mago_pins.gps_rst, true);
            g_gps.config_state = 0;
            g_gps.last_fix_time = now;
        }
    }

    return MAGO_OK;
}

void mago_gps_handle_event(void* ctx, const mago_event_t* evt) {
    // GPS doesn't handle external events currently
    (void)ctx; (void)evt;
}

mago_result_t mago_gps_stop(void* ctx) {
    return MAGO_OK;
}

mago_result_t mago_gps_deinit(void* ctx) {
    MAGO_HAL_GET()->uart->deinit(1);
    return MAGO_OK;
}

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

mago_result_t mago_gps_feed_byte(void* ctx, uint8_t byte) {
    ubx_parse_byte(byte);
    return MAGO_OK;
}

const mago_gps_nav_t* mago_gps_get_nav(void* ctx) {
    return &g_gps.nav;
}

bool mago_gps_has_fix(void* ctx) {
    return g_gps.has_fix;
}

bool mago_gps_has_time(void* ctx) {
    return g_gps.has_time;
}

void mago_gps_get_stats(void* ctx, uint32_t* rx_bytes, uint32_t* rx_msgs, uint32_t* crc_err, uint32_t* parse_err) {
    if (rx_bytes) *rx_bytes = g_gps.rx_bytes;
    if (rx_msgs) *rx_msgs = g_gps.rx_messages;
    if (crc_err) *crc_err = g_gps.crc_errors;
    if (parse_err) *parse_err = g_gps.parse_errors;
}

/* ============================================================================
 * FACTORY
 * ============================================================================ */

const mago_component_factory_t* mago_gps_factory(void) {
    static const mago_component_factory_t factory = {
        .id = MAGO_COMP_GPS,
        .name = "gps",
        .priority = 20,
        .init = mago_gps_init,
        .start = mago_gps_start,
        .tick = mago_gps_tick,
        .handle_event = mago_gps_handle_event,
        .stop = mago_gps_stop,
        .deinit = mago_gps_deinit,
        .create_data = NULL,
        .data_size = sizeof(mago_gps_data_t),
    };
    return &factory;
}