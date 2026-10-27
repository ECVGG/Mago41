/**
 * @file mago_rtty.c
 * @brief Mago41 - RTTY Protocol Implementation (ITA2/Baudot)
 */

#include "mago_rtty.h"
#include "mago_protocol.h"
#include "mago_hal.h"
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * ITA2 (BAUDOT) ENCODING
 * ============================================================================ */

// ITA2 letters set
static const uint8_t ita2_letters[32] = {
    0x00, 0x1F, 0x0C, 0x1B, 0x0F, 0x12, 0x17, 0x08,
    0x16, 0x04, 0x13, 0x0D, 0x1A, 0x03, 0x0E, 0x14,
    0x0A, 0x09, 0x06, 0x15, 0x02, 0x19, 0x0B, 0x18,
    0x05, 0x11, 0x1E, 0x07, 0x1D, 0x1C, 0x10, 0x01
};

// ITA2 figures set
static const uint8_t ita2_figures[32] = {
    0x00, 0x1F, 0x0E, 0x1D, 0x16, 0x12, 0x17, 0x08,
    0x10, 0x04, 0x0A, 0x1B, 0x06, 0x1E, 0x14, 0x1C,
    0x18, 0x19, 0x0D, 0x03, 0x0C, 0x0F, 0x0B, 0x05,
    0x1A, 0x09, 0x02, 0x13, 0x15, 0x11, 0x07, 0x01
};

/* ============================================================================
 * RTTY PROTOCOL DATA
 * ============================================================================ */

typedef struct {
    mago_proto_rtty_params_t params;
    bool in_figure_shift;
} mago_rtty_data_t;

static mago_rtty_data_t g_rtty_data;

/* ============================================================================
 * ASCII TO ITA2 CONVERSION
 * ============================================================================ */

static uint8_t ascii_to_ita2(char c, bool* shift) {
    if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';

    if (c >= 'A' && c <= 'Z') {
        static const char* L = "E\nA SIU\rDRJNFCKTZLWHYPQOBG\"MXV";
        for (int i = 0; i < 26; i++) {
            if (L[i] == c) { *shift = false; return ita2_letters[i+1]; }
        }
    }
    if (c >= '0' && c <= '9') {
        static const char* F = "0123456789";
        for (int i = 0; i < 10; i++) {
            if (F[i] == c) { *shift = true; return ita2_figures[i+1]; }
        }
    }
    switch(c) {
        case ' ': *shift = false; return 0x0F;
        case '\r': *shift = false; return 0x08;
        case '\n': *shift = false; return 0x0C;
        case ':': *shift = true; return 0x1D;
        case ',': *shift = true; return 0x0E;
        case '.': *shift = true; return 0x14;
        case '-': *shift = true; return 0x0D;
        case '/': *shift = true; return 0x1B;
        case '?': *shift = true; return 0x12;
        case '\'': *shift = true; return 0x1F;
        case '(': *shift = true; return 0x16;
        case ')': *shift = true; return 0x10;
        case '=': *shift = true; return 0x0A;
        case '+': *shift = true; return 0x06;
        default: *shift = false; return 0x0F;
    }
}

/* ============================================================================
 * RTTY ENCODE
 * ============================================================================ */

static mago_result_t rtty_encode(const mago_protocol_t* p,
                                 const mago_gps_nav_t* gps,
                                 const mago_telemetry_frame_t* telem,
                                 uint8_t* out, size_t* out_len) {
    mago_rtty_data_t* d = (mago_rtty_data_t*)p->params;
    char msg[256];

    // Build RTTY message: $$CALLSIGN,SEQ,HHMMSS,LAT,LON,ALT,SPEED,HEADING,SATS,BATT,TEMP
    int len = snprintf(msg, sizeof(msg),
        "$$%s,%lu,%02lu%02lu%02lu,%ld,%ld,%ld,%lu,%lu,%lu,%lu,%lu",
        d->params.callsign, telem->seq,
        telem->hour, telem->min, telem->sec,
        telem->lat, telem->lon, telem->alt,
        telem->speed, telem->heading, telem->sats,
        telem->batt_mv, telem->temp_c + 40);

    if (len <= 0) return MAGO_ERR_INVAL;

    // Convert to ITA2 bits with start/stop bits
    // Each character: 1 start bit (0) + 5 data bits + 1.5 stop bits (1) = 7.5 bits
    // We'll pack as bits
    size_t bit_pos = 0;
    const size_t max_bits = 2048;

    for (int i = 0; i < len && bit_pos < max_bits; i++) {
        bool shift;
        uint8_t code = ascii_to_ita2(msg[i], &shift);

        // Insert shift code if needed
        if (shift != d->in_figure_shift) {
            uint8_t sh = shift ? 0x1B : 0x1F; // Figures/Letters shift
            // Start bit
            if (bit_pos < max_bits) { if (out) out[bit_pos/8] &= ~(1 << (7 - (bit_pos%8))); bit_pos++; }
            // 5 data bits
            for (int b = 4; b >= 0 && bit_pos < max_bits; b--) {
                if (sh & (1 << b)) { if (out) out[bit_pos/8] |= (1 << (7 - (bit_pos%8))); }
                bit_pos++;
            }
            // Stop bits (1.5 -> 2 bits)
            for (int s = 0; s < 2 && bit_pos < max_bits; s++) {
                if (out) out[bit_pos/8] |= (1 << (7 - (bit_pos%8)));
                bit_pos++;
            }
            d->in_figure_shift = shift;
        }

        // Character: start bit (0)
        if (bit_pos < max_bits) { if (out) out[bit_pos/8] &= ~(1 << (7 - (bit_pos%8))); bit_pos++; }
        // 5 data bits (MSB first)
        for (int b = 4; b >= 0 && bit_pos < max_bits; b--) {
            if (code & (1 << b)) { if (out) out[bit_pos/8] |= (1 << (7 - (bit_pos%8))); }
            bit_pos++;
        }
        // Stop bits (1.5 -> 2 bits)
        for (int s = 0; s < 2 && bit_pos < max_bits; s++) {
            if (out) out[bit_pos/8] |= (1 << (7 - (bit_pos%8)));
            bit_pos++;
        }
    }

    *out_len = bit_pos;
    return MAGO_OK;
}

/* ============================================================================
 * RTTY TRANSMIT
 * ============================================================================ */

static mago_result_t rtty_transmit(const mago_protocol_t* p, const uint8_t* data, size_t len) {
    // Direct radio TX - RTTY uses simple 2FSK
    return mago_radio_transmit(NULL, data, (len + 7) / 8);
}

/* ============================================================================
 * FACTORY
 * ============================================================================ */

mago_protocol_t* mago_rtty_create(const mago_proto_rtty_params_t* params) {
    static mago_protocol_t proto;

    memset(&proto, 0, sizeof(proto));
    memset(&g_rtty_data, 0, sizeof(g_rtty_data));

    strncpy(proto.name, "rtty", sizeof(proto.name)-1);
    proto.id = MAGO_PROTO_RTTY;
    proto.priority = 10;
    proto.params = &g_rtty_data;
    proto.params_size = sizeof(g_rtty_data);
    proto.fec = NULL;  // RTTY typically no FEC
    proto.interleaver = NULL;
    proto.scrambler = NULL;
    proto.mapper = &mago_map_2fsk;
    proto.encode = rtty_encode;
    proto.transmit = rtty_transmit;
    proto.symbol_period_us = 1000000 / (params ? params->baud : 50); // e.g., 20ms for 50 baud

    if (params) {
        g_rtty_data.params = *params;
    } else {
        // Defaults
        g_rtty_data.params.baud = 50;
        g_rtty_data.params.shift_hz = 340;
        g_rtty_data.params.bits = 7;
        g_rtty_data.params.stop_bits = 2;
        g_rtty_data.params.extended_ascii = false;
        strncpy(g_rtty_data.params.callsign, "MAGO41", sizeof(g_rtty_data.params.callsign)-1);
    }
    g_rtty_data.in_figure_shift = false;

    return &proto;
}