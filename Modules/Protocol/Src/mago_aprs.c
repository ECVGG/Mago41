/**
 * @file mago_aprs.c
 * @brief Mago41 - APRS Protocol Implementation (1200 baud AFSK AX.25)
 */

#include "mago_aprs.h"
#include "mago_protocol.h"
#include "mago_hal.h"
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * AX.25 ADDRESS ENCODING
 * ============================================================================ */

static void ax25_encode_address(uint8_t* out, const char* callsign, uint8_t ssid, bool last) {
    // Callsign: 6 chars, shifted left 1, padded with space (0x20)
    // SSID: bits 7-1 = 0x60 | (ssid << 1) | (last ? 1 : 0)
    for (int i = 0; i < 6; i++) {
        char c = (i < (int)strlen(callsign)) ? callsign[i] : ' ';
        if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
        out[i] = (uint8_t)c << 1;
    }
    out[6] = 0x60 | ((ssid & 0x0F) << 1) | (last ? 1 : 0);
}

/* ============================================================================
 * CRC-16 CCITT
 * ============================================================================ */

static uint16_t crc16_ccitt(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
        }
    }
    return ~crc;
}

/* ============================================================================
 * NRZI ENCODING
 * ============================================================================ */

static void nrzi_encode(uint8_t* data, size_t len) {
    uint8_t prev = 1; // Start with mark (1)
    for (size_t i = 0; i < len; i++) {
        uint8_t byte = data[i];
        for (int b = 0; b < 8; b++) {
            uint8_t bit = (byte >> (7 - b)) & 1;
            if (bit == 0) {
                prev ^= 1; // Transition on 0
            }
            // Store NRZI bit
            if (prev) byte |= (1 << (7 - b));
            else byte &= ~(1 << (7 - b));
        }
        data[i] = byte;
    }
}

/* ============================================================================
 * BIT STUFFING (AX.25 HDLC)
 * ============================================================================ */

static size_t bit_stuff(const uint8_t* in, size_t in_len, uint8_t* out) {
    size_t out_idx = 0;
    uint8_t ones_count = 0;

    for (size_t i = 0; i < in_len; i++) {
        for (int b = 7; b >= 0; b--) {
            uint8_t bit = (in[i] >> b) & 1;
            if (out_idx / 8 >= 256) return out_idx; // Buffer limit
            if (bit) {
                out[out_idx / 8] |= (1 << (7 - (out_idx % 8)));
                ones_count++;
                if (ones_count == 5) {
                    // Insert 0 bit
                    out_idx++;
                    ones_count = 0;
                    if (out_idx / 8 >= 256) return out_idx;
                    out[out_idx / 8] &= ~(1 << (7 - (out_idx % 8)));
                }
            } else {
                out[out_idx / 8] &= ~(1 << (7 - (out_idx % 8)));
                ones_count = 0;
            }
            out_idx++;
        }
    }
    return out_idx;
}

/* ============================================================================
 * APRS PROTOCOL DATA
 * ============================================================================ */

typedef struct {
    mago_proto_aprs_params_t params;
    uint16_t seq;
} mago_aprs_data_t;

static mago_aprs_data_t g_aprs_data;

/* ============================================================================
 * APRS ENCODE - POSITION FRAME
 * ============================================================================ */

static mago_result_t aprs_encode_position(const mago_gps_nav_t* gps,
                                          const mago_telemetry_frame_t* telem,
                                          uint8_t* out, size_t* out_len) {
    mago_aprs_data_t* d = (mago_aprs_data_t*)g_aprs_data.params; // Will fix
    uint8_t frame[256];
    size_t pos = 0;

    // AX.25 Header
    // Destination: APZxxx (APRS)
    frame[pos++] = 'A' << 1;
    frame[pos++] = 'P' << 1;
    frame[pos++] = 'Z' << 1;
    frame[pos++] = (d->params.ssid / 10 + '0') << 1;
    frame[pos++] = (d->params.ssid % 10 + '0') << 1;
    frame[pos++] = ' ' << 1;
    frame[pos++] = 0x60 | (0x0E << 1) | 1; // Last address

    // Source: our callsign
    ax25_encode_address(&frame[pos], d->params.callsign, d->params.ssid, true);
    pos += 7;

    // Path (digipeaters)
    if (strlen(d->params.path) > 0) {
        char* token = strtok((char*)d->params.path, ",");
        while (token) {
            ax25_encode_address(&frame[pos], token, 0, false);
            pos += 7;
            token = strtok(NULL, ",");
        }
        // Mark last path element
        frame[pos-1] |= 0x01;
    }

    // Control (UI frame) + PID (no layer 3)
    frame[pos++] = APRS_FRAME_UI;
    frame[pos++] = APRS_PID_NO_LAYER3;

    // Information field: APRS position
    // Format: !DDMM.mmN/DDDMM.mmW>Comment
    char info[128];
    int lat_deg = telem->lat / 10000000;
    int lat_min = (telem->lat % 10000000) * 60 / 10000000;
    int lon_deg = telem->lon / 10000000;
    int lon_min = (telem->lon % 10000000) * 60 / 10000000;

    int len = snprintf(info, sizeof(info),
        "!%02d%02d.%02d%c/%03d%02d.%02d%c%s",
        abs(lat_deg), lat_min/10, lat_min%10, lat_deg >= 0 ? 'N' : 'S',
        abs(lon_deg), lon_min/10, lon_min%10, lon_deg >= 0 ? 'E' : 'W',
        d->params.comment);

    if (len > 0) {
        memcpy(&frame[pos], info, len);
        pos += len;
    }

    // CRC
    uint16_t crc = crc16_ccitt(frame, pos);
    frame[pos++] = crc & 0xFF;
    frame[pos++] = (crc >> 8) & 0xFF;

    // Bit stuffing
    uint8_t stuffed[256];
    size_t stuffed_bits = bit_stuff(frame, pos, stuffed);
    size_t stuffed_bytes = (stuffed_bits + 7) / 8;

    // Add flags
    uint8_t* out_ptr = out;
    *out_ptr++ = APRS_AX25_FLAG;
    memcpy(out_ptr, stuffed, stuffed_bytes);
    out_ptr += stuffed_bytes;
    *out_ptr++ = APRS_AX25_FLAG;

    *out_len = (stuffed_bytes + 2) * 8; // Return in bits
    return MAGO_OK;
}

/* ============================================================================
 * APRS ENCODE MAIN
 * ============================================================================ */

static mago_result_t aprs_encode(const mago_protocol_t* p,
                                 const mago_gps_nav_t* gps,
                                 const mago_telemetry_frame_t* telem,
                                 uint8_t* out, size_t* out_len) {
    return aprs_encode_position(gps, telem, out, out_len);
}

/* ============================================================================
 * APRS TRANSMIT
 * ============================================================================ */

static mago_result_t aprs_transmit(const mago_protocol_t* p, const uint8_t* data, size_t len) {
    // NRZI encode
    uint8_t nrzi_data[256];
    memcpy(nrzi_data, data, len/8 + 1);
    nrzi_encode(nrzi_data, len/8 + 1);

    // Transmit via radio (AFSK would need special handling)
    return mago_radio_transmit(NULL, nrzi_data, len/8 + 1);
}

/* ============================================================================
 * FACTORY
 * ============================================================================ */

mago_protocol_t* mago_aprs_create(const mago_proto_aprs_params_t* params) {
    static mago_protocol_t proto;

    memset(&proto, 0, sizeof(proto));
    memset(&g_aprs_data, 0, sizeof(g_aprs_data));

    strncpy(proto.name, "aprs", sizeof(proto.name)-1);
    proto.id = MAGO_PROTO_APRS;
    proto.priority = 15;
    proto.params = &g_aprs_data;
    proto.params_size = sizeof(g_aprs_data);

    // APRS uses AFSK mapper (1200/2200 Hz)
    proto.fec = NULL;
    proto.interleaver = NULL;
    proto.scrambler = NULL;
    proto.mapper = &mago_map_afsk_1200; // Will need to define this
    proto.encode = aprs_encode;
    proto.transmit = aprs_transmit;
    proto.symbol_period_us = 1000000 / 1200; // 833us per symbol

    if (params) {
        g_aprs_data.params = *params;
    } else {
        strncpy(g_aprs_data.params.callsign, "MAGO41", sizeof(g_aprs_data.params.callsign)-1);
        g_aprs_data.params.ssid = 11;
        strncpy(g_aprs_data.params.path, "WIDE1-1,WIDE2-1", sizeof(g_aprs_data.params.path)-1);
        g_aprs_data.params.symbol_table = APRS_SYMBOL_TABLE_PRIMARY;
        g_aprs_data.params.symbol_code = 'O'; // Balloon
        strncpy(g_aprs_data.params.comment, "Mago41 RS41", sizeof(g_aprs_data.params.comment)-1);
        g_aprs_data.params.dao_enabled = false;
        g_aprs_data.params.beacon_ratio = 1;
        g_aprs_data.params.mic_e = false;
    }
    g_aprs_data.seq = 0;

    return &proto;
}