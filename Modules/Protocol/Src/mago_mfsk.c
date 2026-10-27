/**
 * @file mago_mfsk.c
 * @brief Mago41 - MFSK Protocol Implementation (Horus V1/V2)
 */

#include "mago_mfsk.h"
#include "mago_protocol.h"
#include "mago_hal.h"
#include <string.h>
#include <stdint.h>

/* ============================================================================
 * HORUS FRAME STRUCTURE
 * ============================================================================ */

// Horus V1 frame: 16 sync + payload (Golay encoded)
// Horus V2 frame: 16 sync + payload (Golay + scramble) + CRC

typedef struct {
    mago_proto_mfsk_params_t params;
    uint32_t scramble_seed;
    uint16_t frame_counter;
} mago_mfsk_data_t;

static mago_mfsk_data_t g_mfsk_data;

/* ============================================================================
 * HORUS PAYLOAD ENCODING
 * ============================================================================ */

static mago_result_t mfsk_encode_horus_v1(const mago_gps_nav_t* gps,
                                         const mago_telemetry_frame_t* telem,
                                         uint8_t* out_bits, size_t* out_bit_len) {
    // Horus V1 payload (before FEC): 64 bits
    // [16 bits payload_id][8 bits seq][24 bits lat][24 bits lon][12 bits alt]
    uint8_t payload[13] = {0}; // 104 bits -> will be Golay encoded to 208 bits

    // Payload ID (0x0100 for V1)
    payload[0] = 0x01;
    payload[1] = 0x00;

    // Sequence (8 bits)
    payload[2] = telem->seq & 0xFF;

    // Latitude (24 bits, signed, 1e-7 deg)
    int32_t lat = telem->lat;
    payload[3] = (lat >> 16) & 0xFF;
    payload[4] = (lat >> 8) & 0xFF;
    payload[5] = lat & 0xFF;

    // Longitude (24 bits, signed, 1e-7 deg)
    int32_t lon = telem->lon;
    payload[6] = (lon >> 16) & 0xFF;
    payload[7] = (lon >> 8) & 0xFF;
    payload[8] = lon & 0xFF;

    // Altitude (12 bits, meters/2, offset -500m)
    int16_t alt = (int16_t)((telem->alt + 500) / 2);
    payload[9] = (alt >> 4) & 0xFF;
    payload[10] = (alt << 4) & 0xF0;

    // Speed (6 bits, knots) + Heading (8 bits, deg/2) + Sats (4 bits)
    payload[10] |= (telem->speed / 2) & 0x0F;
    payload[11] = (telem->heading / 2) & 0xFF;
    payload[12] = (telem->sats & 0x0F) | ((telem->batt_mv > 3600) ? 0x80 : 0x00);

    // Now apply FEC (Golay 24,12) to each 12-bit block
    // 104 bits = 8.67 blocks -> pad to 9 blocks = 108 bits
    uint8_t fec_in[108/8] = {0};
    memcpy(fec_in, payload, 13);
    size_t fec_in_bits = 108;
    size_t fec_out_bits = 0;

    mago_result_t res = mago_fec_golay2412.encode(fec_in, fec_in_bits, out_bits, &fec_out_bits);
    if (MAGO_FAILED(res)) return res;

    // Interleave (depth 2 for V1)
    uint8_t interleaved[208/8];
    res = mago_ilv_block_2.interleave(out_bits, interleaved, fec_out_bits/8);
    if (MAGO_FAILED(res)) return res;
    memcpy(out_bits, interleaved, fec_out_bits/8);

    *out_bit_len = fec_out_bits;
    return MAGO_OK;
}

static mago_result_t mfsk_encode_horus_v2(const mago_gps_nav_t* gps,
                                         const mago_telemetry_frame_t* telem,
                                         uint8_t* out_bits, size_t* out_bit_len) {
    // Horus V2 payload (before FEC): 80 bits + 16 bit CRC = 96 bits
    // [16 bits payload_id][8 bits seq][24 bits lat][24 bits lon][12 bits alt][8 bits extra][16 bits CRC]
    uint8_t payload[14] = {0}; // 112 bits

    // Payload ID (0x0200 for V2)
    payload[0] = 0x02;
    payload[1] = 0x00;

    // Sequence (8 bits)
    payload[2] = telem->seq & 0xFF;

    // Latitude (24 bits)
    int32_t lat = telem->lat;
    payload[3] = (lat >> 16) & 0xFF;
    payload[4] = (lat >> 8) & 0xFF;
    payload[5] = lat & 0xFF;

    // Longitude (24 bits)
    int32_t lon = telem->lon;
    payload[6] = (lon >> 16) & 0xFF;
    payload[7] = (lon >> 8) & 0xFF;
    payload[8] = lon & 0xFF;

    // Altitude (12 bits)
    int16_t alt = (int16_t)((telem->alt + 500) / 2);
    payload[9] = (alt >> 4) & 0xFF;
    payload[10] = (alt << 4) & 0xF0;

    // Extra: speed (6) + heading (8) + sats (4) + batt_flag (1) + temp (7) = 26 bits -> pad
    payload[10] |= (telem->speed / 2) & 0x0F;
    payload[11] = (telem->heading / 2) & 0xFF;
    payload[12] = ((telem->sats & 0x0F) << 4) | ((telem->batt_mv > 3600) ? 0x08 : 0x00) | ((telem->temp_c + 40) >> 1);
    payload[13] = ((telem->temp_c + 40) & 0x01) << 7;

    // CRC-16 (CCITT) on first 12 bytes
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < 12; i++) {
        crc ^= (uint16_t)payload[i] << 8;
        for (int j = 0; j < 8; j++) {
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
        }
    }
    payload[12] = (crc >> 8) & 0xFF;
    payload[13] = crc & 0xFF;

    // Apply FEC (Golay 24,12) - 96 bits = 8 blocks exactly
    size_t fec_in_bits = 96;
    size_t fec_out_bits = 0;
    mago_result_t res = mago_fec_golay2412.encode(payload, fec_in_bits, out_bits, &fec_out_bits);
    if (MAGO_FAILED(res)) return res;

    // Scramble (xorshift)
    uint32_t seed = g_mfsk_data.scramble_seed ^ g_mfsk_data.frame_counter;
    res = mago_scr_xorshift.scramble(out_bits, fec_out_bits/8, &seed);
    if (MAGO_FAILED(res)) return res;

    // Interleave (depth 4 for V2)
    uint8_t interleaved[192/8];
    res = mago_ilv_block_4.interleave(out_bits, interleaved, fec_out_bits/8);
    if (MAGO_FAILED(res)) return res;
    memcpy(out_bits, interleaved, fec_out_bits/8);

    *out_bit_len = fec_out_bits;
    return MAGO_OK;
}

/* ============================================================================
 * MFSK ENCODE MAIN
 * ============================================================================ */

static mago_result_t mfsk_encode(const mago_protocol_t* p,
                                 const mago_gps_nav_t* gps,
                                 const mago_telemetry_frame_t* telem,
                                 uint8_t* out, size_t* out_len) {
    mago_mfsk_data_t* d = (mago_mfsk_data_t*)p->params;

    if (d->params.horus_v2) {
        return mfsk_encode_horus_v2(gps, telem, out, out_len);
    } else {
        return mfsk_encode_horus_v1(gps, telem, out, out_len);
    }
}

/* ============================================================================
 * MFSK TRANSMIT
 * ============================================================================ */

static mago_result_t mfsk_transmit(const mago_protocol_t* p, const uint8_t* data, size_t len) {
    // Convert bits to symbols via mapper
    uint8_t symbols[256];
    size_t n_sym;
    mago_result_t res = p->mapper->bits_to_symbols(data, len * 8, symbols, &n_sym);
    if (MAGO_FAILED(res)) return res;

    // Transmit symbols via radio
    // This would need radio driver to support symbol-level TX
    // For now, just pass to radio
    return mago_radio_transmit(NULL, data, len);
}

/* ============================================================================
 * FACTORY
 * ============================================================================ */

mago_protocol_t* mago_mfsk_create(const mago_proto_mfsk_params_t* params) {
    static mago_protocol_t proto;

    memset(&proto, 0, sizeof(proto));
    memset(&g_mfsk_data, 0, sizeof(g_mfsk_data));

    strncpy(proto.name, "horus_v2", sizeof(proto.name)-1);
    proto.id = MAGO_PROTO_HORUS_V2;
    proto.priority = 5;
    proto.params = &g_mfsk_data;
    proto.params_size = sizeof(g_mfsk_data);

    // Pipeline
    proto.fec = &mago_fec_golay2412;
    proto.interleaver = params && params->interleaving >= 4 ? &mago_ilv_block_4 :
                        params && params->interleaving >= 2 ? &mago_ilv_block_2 : NULL;
    proto.scrambler = (params && params->scrambling) ? &mago_scr_xorshift : NULL;

    // Mapper based on modulation
    switch (params ? params->modulation : 4) {
        case 2: proto.mapper = &mago_map_2fsk; break;
        case 4: proto.mapper = &mago_map_4fsk; break;
        case 16: proto.mapper = &mago_map_16fsk; break;
        default: proto.mapper = &mago_map_4fsk; break;
    }

    proto.encode = mfsk_encode;
    proto.transmit = mfsk_transmit;

    // Symbol timing
    uint16_t baud = params ? params->baud : 100;
    proto.symbol_period_us = 1000000 / baud;
    proto.preamble_us = (params ? params->preamble_symbols : 16) * proto.symbol_period_us;
    proto.guard_us = 10000;

    if (params) {
        g_mfsk_data.params = *params;
    } else {
        g_mfsk_data.params.baud = 100;
        g_mfsk_data.params.modulation = 4;
        g_mfsk_data.params.deviation_hz = 600;
        g_mfsk_data.params.fec_enabled = true;
        g_mfsk_data.params.interleaving = 4;
        g_mfsk_data.params.scrambling = true;
        g_mfsk_data.params.payload_id = MAGO_MFSK_PAYLOAD_HORUS_V2;
        g_mfsk_data.params.preamble_symbols = 16;
        g_mfsk_data.params.sync_symbols = 16;
        g_mfsk_data.params.horus_v2 = true;
    }
    g_mfsk_data.scramble_seed = 0xDEADBEEF;
    g_mfsk_data.frame_counter = 0;

    return &proto;
}