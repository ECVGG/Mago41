/**
 * @file mago_gps.h
 * @brief Mago41 - GPS Module (u-blox UBX Protocol)
 *
 * Diferencia radical vs RS41HUP_V2:
 * - Parser state machine puro C
 * - Sin dependencias Arduino
 * - Config declarativa (baud, modelo, dynamic model)
 * - Event-driven: GPS fix via message bus
 * - Soporta NAV-PVT, NAV-POSLLH, NAV-STATUS, NAV-SOL, NAV-VELNED, NAV-TIMEUTC
 * - Auto-detección modelo (MAX-6/7/8, M8, M9)
 */

#ifndef MAGO_GPS_H
#define MAGO_GPS_H

#include <stdint.h>
#include <stdbool.h>
#include "mago_types.h"
#include "mago_config.h"

/* ============================================================================
 * UBX MESSAGE CLASSES/IDS
 * ============================================================================ */

#define UBX_SYNC_1 0xB5
#define UBX_SYNC_2 0x62

#define UBX_CLASS_NAV 0x01
#define UBX_CLASS_RXM 0x02
#define UBX_CLASS_INF 0x04
#define UBX_CLASS_ACK 0x05
#define UBX_CLASS_CFG 0x06
#define UBX_CLASS_UPD 0x09
#define UBX_CLASS_MON 0x0A
#define UBX_CLASS_AID 0x0B
#define UBX_CLASS_TIM 0x0D
#define UBX_CLASS_ESF 0x10
#define UBX_CLASS_MGA 0x13
#define UBX_CLASS_LOG 0x21
#define UBX_CLASS_SEC 0x27
#define UBX_CLASS_HNR 0x28

// NAV messages
#define UBX_NAV_PVT       0x07
#define UBX_NAV_POSLLH    0x02
#define UBX_NAV_STATUS    0x03
#define UBX_NAV_SOL       0x06
#define UBX_NAV_VELNED    0x12
#define UBX_NAV_TIMEUTC   0x21
#define UBX_NAV_SAT       0x35
#define UBX_NAV_SIG       0x43

// ACK messages
#define UBX_ACK_ACK       0x01
#define UBX_ACK_NAK       0x00

// CFG messages
#define UBX_CFG_PRT       0x00
#define UBX_CFG_MSG       0x01
#define UBX_CFG_RATE      0x08
#define UBX_CFG_NAV5      0x24
#define UBX_CFG_RST       0x04
#define UBX_CFG_PM2       0x3B
#define UBX_CFG_GNSS      0x3E

/* ============================================================================
 * UBX PARSER STATE MACHINE
 * ============================================================================ */

typedef enum {
    UBX_STATE_SYNC1 = 0,
    UBX_STATE_SYNC2,
    UBX_STATE_CLASS,
    UBX_STATE_ID,
    UBX_STATE_LEN_L,
    UBX_STATE_LEN_H,
    UBX_STATE_PAYLOAD,
    UBX_STATE_CK_A,
    UBX_STATE_CK_B,
} ubx_parser_state_t;

/* ============================================================================
 * GPS DATA STRUCT
 * ============================================================================ */

typedef struct {
    mago_gps_cfg_t config;
    mago_gps_nav_t nav;
    bool has_fix;
    bool has_time;
    uint8_t parser_state;
    uint8_t msg_class;
    uint8_t msg_id;
    uint16_t msg_len;
    uint16_t payload_idx;
    uint8_t ck_a, ck_b;
    uint8_t payload_buffer[256];
    uint32_t rx_bytes;
    uint32_t rx_messages;
    uint32_t crc_errors;
    uint32_t parse_errors;
    uint32_t last_fix_time;
    // Config state machine
    uint8_t config_state;
    uint32_t config_timeout;
} mago_gps_data_t;

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

mago_result_t mago_gps_init(void* ctx, const mago_gps_cfg_t* cfg);
mago_result_t mago_gps_start(void* ctx);
mago_result_t mago_gps_tick(void* ctx);
void mago_gps_handle_event(void* ctx, const mago_event_t* evt);
mago_result_t mago_gps_stop(void* ctx);
mago_result_t mago_gps_deinit(void* ctx);

mago_result_t mago_gps_feed_byte(void* ctx, uint8_t byte);
const mago_gps_nav_t* mago_gps_get_nav(void* ctx);
bool mago_gps_has_fix(void* ctx);
bool mago_gps_has_time(void* ctx);
void mago_gps_get_stats(void* ctx, uint32_t* rx_bytes, uint32_t* rx_msgs, uint32_t* crc_err, uint32_t* parse_err);

const mago_component_factory_t* mago_gps_factory(void);

#endif // MAGO_GPS_H