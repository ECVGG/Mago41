/**
 * @file mago_morse.c
 * @brief Mago41 - Morse CW Protocol Implementation
 */

#include "mago_morse.h"
#include "mago_protocol.h"
#include "mago_hal.h"
#include <string.h>

/* ============================================================================
 * MORSE CODE TABLE
 * ============================================================================ */

typedef struct {
    char c;
    uint16_t pattern;  // 1=dash, 0=dot, MSB first, length in lower 5 bits
} morse_entry_t;

static const morse_entry_t morse_table[] = {
    {'A', 0x80 | 2},   // .-
    {'B', 0x10 | 4},   // -...
    {'C', 0x50 | 4},   // -.-.
    {'D', 0x40 | 3},   // -..
    {'E', 0x00 | 1},   // .
    {'F', 0x20 | 4},   // ..-.
    {'G', 0x60 | 3},   // --.
    {'H', 0x00 | 4},   // ....
    {'I', 0x00 | 2},   // ..
    {'J', 0x70 | 4},   // .---
    {'K', 0x50 | 3},   // -.-
    {'L', 0x40 | 4},   // .-..
    {'M', 0xC0 | 2},   // --
    {'N', 0x40 | 2},   // -.
    {'O', 0xE0 | 3},   // ---
    {'P', 0x60 | 4},   // .--.
    {'Q', 0xD0 | 4},   // --.-
    {'R', 0x20 | 3},   // .-.
    {'S', 0x00 | 3},   // ...
    {'T', 0x80 | 1},   // -
    {'U', 0x40 | 3},   // ..-
    {'V', 0x80 | 4},   // ...-
    {'W', 0x60 | 3},   // .--
    {'X', 0x90 | 4},   // -..-
    {'Y', 0xB0 | 4},   // -.--
    {'Z', 0xC0 | 4},   // --..
    {'0', 0xF8 | 5},   // -----
    {'1', 0x78 | 5},   // .----
    {'2', 0x38 | 5},   // ..---
    {'3', 0x18 | 5},   // ...--
    {'4', 0x08 | 5},   // ....-
    {'5', 0x00 | 5},   // .....
    {'6', 0x80 | 5},   // -....
    {'7', 0xC0 | 5},   // --...
    {'8', 0xE0 | 5},   // ---..
    {'9', 0xF0 | 5},   // ----.
    {'.', 0x55 | 6},   // .-.-.-
    {',', 0xCC | 6},   // --..--
    {'?', 0x36 | 6},   // ..--..
    {'/', 0x90 | 5},   // -..-.
    {'@', 0x66 | 6},   // .--.-.
    {'=', 0x50 | 5},   // -...-
    {' ', 0x00 | 0},   // word space (7 units)
};

/* ============================================================================
 * MORSE PROTOCOL DATA
 * ============================================================================ */

typedef struct {
    mago_proto_morse_params_t params;
    uint32_t dit_us;           // Dot duration in microseconds
    uint32_t dah_us;           // Dash duration (3*dit)
    uint32_t intra_us;         // Intra-character space (1*dit)
    uint32_t inter_us;         // Inter-character space (3*dit)
    uint32_t word_us;          // Word space (7*dit)
    uint32_t farnsworth_dit_us; // Farnsworth timing
} mago_morse_data_t;

static mago_morse_data_t g_morse_data;

/* ============================================================================
 * MORSE TIMING CALCULATION
 * ============================================================================ */

static void morse_calc_timing(mago_morse_data_t* d) {
    // Standard: 50 dots per word = 1200 dot units per minute
    // WPM = 1200 / (dot_units_per_word)
    // For standard spacing: dot = 1, dash = 3, intra = 1, inter = 3, word = 7
    // PARIS = 50 units, so 50 * WPM dots per minute
    d->dit_us = 60000000 / (50 * d->params.wpm); // microseconds per dot

    if (d->params.farnsworth_wpm > 0 && d->params.farnsworth_wpm < d->params.wpm) {
        // Farnsworth: characters at wpm, spacing at farnsworth_wpm
        d->farnsworth_dit_us = 60000000 / (50 * d->params.farnsworth_wpm);
        d->dah_us = 3 * d->dit_us;
        d->intra_us = d->dit_us;
        d->inter_us = 3 * d->farnsworth_dit_us;
        d->word_us = 7 * d->farnsworth_dit_us;
    } else {
        d->farnsworth_dit_us = d->dit_us;
        d->dah_us = 3 * d->dit_us;
        d->intra_us = d->dit_us;
        d->inter_us = 3 * d->dit_us;
        d->word_us = 7 * d->dit_us;
    }
}

/* ============================================================================
 * MORSE ENCODE
 * ============================================================================ */

static mago_result_t morse_encode_char(char c, uint8_t* out_bits, size_t* bit_pos, size_t max_bits) {
    if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';

    for (size_t i = 0; i < sizeof(morse_table)/sizeof(morse_table[0]); i++) {
        if (morse_table[i].c == c) {
            uint8_t len = morse_table[i].pattern & 0x1F;
            uint16_t pat = morse_table[i].pattern >> 5;

            for (int b = len - 1; b >= 0 && *bit_pos < max_bits; b--) {
                bool is_dash = (pat >> b) & 1;
                // Signal on
                size_t signal_len = is_dash ? 3 : 1; // in dot units
                for (size_t s = 0; s < signal_len && *bit_pos < max_bits; s++) {
                    out_bits[*bit_pos / 8] |= (1 << (7 - (*bit_pos % 8)));
                    (*bit_pos)++;
                }
                // Intra-character space
                if (b > 0 && *bit_pos < max_bits) {
                    out_bits[*bit_pos / 8] &= ~(1 << (7 - (*bit_pos % 8)));
                    (*bit_pos)++;
                }
            }
            return MAGO_OK;
        }
    }
    return MAGO_ERR_INVAL;
}

static mago_result_t morse_encode(const mago_protocol_t* p,
                                 const mago_gps_nav_t* gps,
                                 const mago_telemetry_frame_t* telem,
                                 uint8_t* out, size_t* out_len) {
    mago_morse_data_t* d = (mago_morse_data_t*)p->params;
    const char* msg = d->params.message;
    size_t msg_len = strlen(msg);
    size_t bit_pos = 0;
    const size_t max_bits = 2048;

    // Build message with callsign if not in custom message
    char full_msg[256];
    if (msg_len == 0) {
        snprintf(full_msg, sizeof(full_msg), "DE %s", "MAGO41");
        msg = full_msg;
        msg_len = strlen(msg);
    }

    for (size_t i = 0; i < msg_len && bit_pos < max_bits; i++) {
        if (msg[i] == ' ') {
            // Word space
            for (size_t s = 0; s < 7 && bit_pos < max_bits; s++) {
                out[bit_pos / 8] &= ~(1 << (7 - (bit_pos % 8)));
                bit_pos++;
            }
        } else {
            morse_encode_char(msg[i], out, &bit_pos, max_bits);
            // Inter-character space
            for (size_t s = 0; s < 3 && bit_pos < max_bits; s++) {
                out[bit_pos / 8] &= ~(1 << (7 - (bit_pos % 8)));
                bit_pos++;
            }
        }
    }

    *out_len = bit_pos;
    return MAGO_OK;
}

/* ============================================================================
 * MORSE TRANSMIT (OOK)
 * ============================================================================ */

static mago_result_t morse_transmit(const mago_protocol_t* p, const uint8_t* data, size_t len) {
    // CW is OOK - just key the carrier on/off
    // This would need radio driver support for OOK
    return mago_radio_transmit(NULL, data, (len + 7) / 8);
}

/* ============================================================================
 * FACTORY
 * ============================================================================ */

mago_protocol_t* mago_morse_create(const mago_proto_morse_params_t* params) {
    static mago_protocol_t proto;

    memset(&proto, 0, sizeof(proto));
    memset(&g_morse_data, 0, sizeof(g_morse_data));

    strncpy(proto.name, "morse", sizeof(proto.name)-1);
    proto.id = MAGO_PROTO_MORSE;
    proto.priority = 20;
    proto.params = &g_morse_data;
    proto.params_size = sizeof(g_morse_data);

    proto.fec = NULL;
    proto.interleaver = NULL;
    proto.scrambler = NULL;
    proto.mapper = &mago_map_ook;
    proto.encode = morse_encode;
    proto.transmit = morse_transmit;

    if (params) {
        g_morse_data.params = *params;
    } else {
        strncpy(g_morse_data.params.message, "DE MAGO41", sizeof(g_morse_data.params.message)-1);
        g_morse_data.params.wpm = 20;
        g_morse_data.params.farnsworth_wpm = 15;
        g_morse_data.params.tone_hz = 600;
        g_morse_data.params.interval_cycles = 30; // Every 30 cycles
    }

    morse_calc_timing(&g_morse_data);

    // Symbol period for OOK (dit duration)
    proto.symbol_period_us = g_morse_data.dit_us;

    return &proto;
}