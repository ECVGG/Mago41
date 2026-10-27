/**
 * @file mago_protocol.c
 * @brief Mago41 - Protocol Framework Implementation
 */

#include "mago_protocol.h"
#include "mago_hal.h"
#include <string.h>
#include <stdlib.h>

/* ============================================================================
 * GLOBAL REGISTRY
 * ============================================================================ */

static mago_protocol_registry_t g_proto_reg;

/* ============================================================================
 * GOLAY (24,12) FEC - Corrects up to 3 errors
 * ============================================================================ */

static const uint16_t golay_gen = 0xC75;  // x^11 + x^10 + x^6 + x^5 + x^4 + x^2 + 1

static uint16_t golay_syndrome(uint32_t codeword) {
    uint32_t rem = codeword & 0xFFFFFF;
    for (int i = 23; i >= 12; i--) {
        if (rem & (1u << i)) {
            rem ^= (uint32_t)golay_gen << (i - 11);
        }
    }
    return rem & 0xFFF;
}

static uint32_t golay_encode_12(uint16_t data) {
    uint32_t rem = ((uint32_t)data) << 12;
    for (int i = 23; i >= 12; i--) {
        if (rem & (1u << i)) {
            rem ^= (uint32_t)golay_gen << (i - 11);
        }
    }
    return ((uint32_t)data << 12) | (rem & 0xFFF);
}

static uint16_t golay_decode_24(uint32_t codeword) {
    uint16_t syn = golay_syndrome(codeword);
    if (syn == 0) return (uint16_t)(codeword >> 12);

    // Try single error correction
    for (int i = 0; i < 24; i++) {
        uint32_t test = codeword ^ (1u << i);
        if (golay_syndrome(test) == 0) {
            return (uint16_t)(test >> 12);
        }
    }
    // Try double error
    for (int i = 0; i < 24; i++) {
        for (int j = i+1; j < 24; j++) {
            uint32_t test = codeword ^ (1u << i) ^ (1u << j);
            if (golay_syndrome(test) == 0) {
                return (uint16_t)(test >> 12);
            }
        }
    }
    return 0xFFFF;  // Uncorrectable
}

static mago_result_t fec_golay_encode(const uint8_t* in, size_t in_bits, uint8_t* out, size_t* out_bits) {
    if (!in || !out || !out_bits) return MAGO_ERR_INVAL;
    if (in_bits % 12 != 0) return MAGO_ERR_INVAL;

    size_t blocks = in_bits / 12;
    *out_bits = blocks * 24;

    for (size_t b = 0; b < blocks; b++) {
        uint16_t data = 0;
        for (int i = 0; i < 12; i++) {
            size_t bit_idx = b * 12 + i;
            if (in[bit_idx / 8] & (1 << (7 - (bit_idx % 8)))) {
                data |= (1 << (11 - i));
            }
        }
        uint32_t encoded = golay_encode_12(data);
        for (int i = 0; i < 24; i++) {
            size_t out_idx = b * 24 + i;
            if (encoded & (1u << (23 - i))) {
                out[out_idx / 8] |= (1 << (7 - (out_idx % 8)));
            }
        }
    }
    return MAGO_OK;
}

static mago_result_t fec_golay_decode(const uint8_t* in, size_t in_bits, uint8_t* out, size_t* out_bits, uint8_t* errors) {
    if (!in || !out || !out_bits) return MAGO_ERR_INVAL;
    if (in_bits % 24 != 0) return MAGO_ERR_INVAL;

    size_t blocks = in_bits / 24;
    *out_bits = blocks * 12;
    uint8_t err_count = 0;

    for (size_t b = 0; b < blocks; b++) {
        uint32_t codeword = 0;
        for (int i = 0; i < 24; i++) {
            size_t bit_idx = b * 24 + i;
            if (in[bit_idx / 8] & (1 << (7 - (bit_idx % 8)))) {
                codeword |= (1u << (23 - i));
            }
        }
        uint16_t decoded = golay_decode_24(codeword);
        if (decoded == 0xFFFF) err_count++;
        for (int i = 0; i < 12; i++) {
            size_t out_idx = b * 12 + i;
            if (decoded & (1 << (11 - i))) {
                out[out_idx / 8] |= (1 << (7 - (out_idx % 8)));
            }
        }
    }
    if (errors) *errors = err_count;
    return err_count > 0 ? MAGO_ERR_CRC : MAGO_OK;
}

static float fec_golay_overhead(void) { return 2.0f; }

const mago_fec_ops_t mago_fec_golay2412 = {
    .type = MAGO_FEC_GOLAY_24_12,
    .encode = fec_golay_encode,
    .decode = fec_golay_decode,
    .overhead = fec_golay_overhead,
};

/* ============================================================================
 * BLOCK INTERLEAVERS
 * ============================================================================ */

static mago_result_t ilv_block_interleave(const uint8_t* in, uint8_t* out, size_t len, uint8_t depth) {
    if (!in || !out || depth < 2) return MAGO_ERR_INVAL;
    size_t rows = depth;
    size_t cols = (len + depth - 1) / depth;

    for (size_t r = 0; r < rows; r++) {
        for (size_t c = 0; c < cols; c++) {
            size_t in_idx = c * rows + r;
            size_t out_idx = r * cols + c;
            if (in_idx < len) out[out_idx] = in[in_idx];
        }
    }
    return MAGO_OK;
}

static mago_result_t ilv_block_deinterleave(const uint8_t* in, uint8_t* out, size_t len, uint8_t depth) {
    if (!in || !out || depth < 2) return MAGO_ERR_INVAL;
    size_t rows = depth;
    size_t cols = (len + depth - 1) / depth;

    for (size_t r = 0; r < rows; r++) {
        for (size_t c = 0; c < cols; c++) {
            size_t in_idx = r * cols + c;
            size_t out_idx = c * rows + r;
            if (in_idx < len) out[out_idx] = in[in_idx];
        }
    }
    return MAGO_OK;
}

// Depth 2
static mago_result_t ilv2_interleave(const uint8_t* in, uint8_t* out, size_t len) {
    return ilv_block_interleave(in, out, len, 2);
}
static mago_result_t ilv2_deinterleave(const uint8_t* in, uint8_t* out, size_t len) {
    return ilv_block_deinterleave(in, out, len, 2);
}

// Depth 4
static mago_result_t ilv4_interleave(const uint8_t* in, uint8_t* out, size_t len) {
    return ilv_block_interleave(in, out, len, 4);
}
static mago_result_t ilv4_deinterleave(const uint8_t* in, uint8_t* out, size_t len) {
    return ilv_block_deinterleave(in, out, len, 4);
}

// Depth 8
static mago_result_t ilv8_interleave(const uint8_t* in, uint8_t* out, size_t len) {
    return ilv_block_interleave(in, out, len, 8);
}
static mago_result_t ilv8_deinterleave(const uint8_t* in, uint8_t* out, size_t len) {
    return ilv_block_deinterleave(in, out, len, 8);
}

const mago_ilv_ops_t mago_ilv_block_2 = {.type = MAGO_ILV_BLOCK, .depth = 2, .interleave = ilv2_interleave, .deinterleave = ilv2_deinterleave};
const mago_ilv_ops_t mago_ilv_block_4 = {.type = MAGO_ILV_BLOCK, .depth = 4, .interleave = ilv4_interleave, .deinterleave = ilv4_deinterleave};
const mago_ilv_ops_t mago_ilv_block_8 = {.type = MAGO_ILV_BLOCK, .depth = 8, .interleave = ilv8_interleave, .deinterleave = ilv8_deinterleave};

/* ============================================================================
 * SCRAMBLERS
 * ============================================================================ */

static mago_result_t scr_xorshift(uint8_t* data, size_t len, uint32_t* seed) {
    if (!data || !seed) return MAGO_ERR_INVAL;
    uint32_t s = *seed;
    for (size_t i = 0; i < len; i++) {
        for (int b = 7; b >= 0; b--) {
            s ^= s << 13;
            s ^= s >> 17;
            s ^= s << 5;
            data[i] ^= (s & 1) << b;
        }
    }
    *seed = s;
    return MAGO_OK;
}

const mago_scr_ops_t mago_scr_xorshift = {
    .type = MAGO_SCR_XOR_SHIFT,
    .scramble = scr_xorshift,
    .descramble = scr_xorshift,  // Self-inverse
};

/* ============================================================================
 * SYMBOL MAPPERS
 * ============================================================================ */

static mago_result_t map_2fsk_bits_to_sym(const uint8_t* bits, size_t n_bits, uint8_t* sym, size_t* n_sym) {
    if (!bits || !sym || !n_sym) return MAGO_ERR_INVAL;
    *n_sym = n_bits;
    for (size_t i = 0; i < n_bits; i++) {
        sym[i] = (bits[i/8] >> (7 - (i%8))) & 1;
    }
    return MAGO_OK;
}
static mago_result_t map_2fsk_sym_to_bits(const uint8_t* sym, size_t n_sym, uint8_t* bits, size_t* n_bits) {
    *n_bits = n_sym;
    for (size_t i = 0; i < n_sym; i++) {
        if (sym[i] & 1) bits[i/8] |= (1 << (7 - (i%8)));
    }
    return MAGO_OK;
}

static mago_result_t map_4fsk_bits_to_sym(const uint8_t* bits, size_t n_bits, uint8_t* sym, size_t* n_sym) {
    if (n_bits % 2) return MAGO_ERR_INVAL;
    *n_sym = n_bits / 2;
    for (size_t i = 0; i < *n_sym; i++) {
        uint8_t b1 = (bits[2*i/8] >> (7 - (2*i%8))) & 1;
        uint8_t b2 = (bits[(2*i+1)/8] >> (7 - ((2*i+1)%8))) & 1;
        sym[i] = (b1 << 1) | b2;
    }
    return MAGO_OK;
}
static mago_result_t map_4fsk_sym_to_bits(const uint8_t* sym, size_t n_sym, uint8_t* bits, size_t* n_bits) {
    *n_bits = n_sym * 2;
    for (size_t i = 0; i < n_sym; i++) {
        if (sym[i] & 2) bits[2*i/8] |= (1 << (7 - (2*i%8)));
        if (sym[i] & 1) bits[(2*i+1)/8] |= (1 << (7 - ((2*i+1)%8)));
    }
    return MAGO_OK;
}

static mago_result_t map_16fsk_bits_to_sym(const uint8_t* bits, size_t n_bits, uint8_t* sym, size_t* n_sym) {
    if (n_bits % 4) return MAGO_ERR_INVAL;
    *n_sym = n_bits / 4;
    for (size_t i = 0; i < *n_sym; i++) {
        uint8_t v = 0;
        for (int b = 0; b < 4; b++) {
            size_t bit_idx = i*4 + b;
            if (bits[bit_idx/8] & (1 << (7 - (bit_idx%8)))) v |= (1 << (3-b));
        }
        sym[i] = v;
    }
    return MAGO_OK;
}
static mago_result_t map_16fsk_sym_to_bits(const uint8_t* sym, size_t n_sym, uint8_t* bits, size_t* n_bits) {
    *n_bits = n_sym * 4;
    for (size_t i = 0; i < n_sym; i++) {
        for (int b = 0; b < 4; b++) {
            if (sym[i] & (1 << (3-b))) {
                size_t bit_idx = i*4 + b;
                bits[bit_idx/8] |= (1 << (7 - (bit_idx%8)));
            }
        }
    }
    return MAGO_OK;
}

const mago_mapper_ops_t mago_map_2fsk = {.type = MAGO_MAP_QPSK, .bits_per_symbol = 1, .bits_to_symbols = map_2fsk_bits_to_sym, .symbols_to_bits = map_2fsk_sym_to_bits};
const mago_mapper_ops_t mago_map_4fsk = {.type = MAGO_MAP_QPSK, .bits_per_symbol = 2, .bits_to_symbols = map_4fsk_bits_to_sym, .symbols_to_bits = map_4fsk_sym_to_bits};
const mago_mapper_ops_t mago_map_16fsk = {.type = MAGO_MAP_16QAM, .bits_per_symbol = 4, .bits_to_symbols = map_16fsk_bits_to_sym, .symbols_to_bits = map_16fsk_sym_to_bits};

/* ============================================================================
 * RTTY PROTOCOL (ITA2/Baudot)
 * ============================================================================ */

static const uint8_t ita2_letters[32] = {
    0x00, 0x1F, 0x0C, 0x1B, 0x0F, 0x12, 0x17, 0x08,
    0x16, 0x04, 0x13, 0x0D, 0x1A, 0x03, 0x0E, 0x14,
    0x0A, 0x09, 0x06, 0x15, 0x02, 0x19, 0x0B, 0x18,
    0x05, 0x11, 0x1E, 0x07, 0x1D, 0x1C, 0x10, 0x01
};

static uint8_t ascii_to_ita2(char c, bool* shift) {
    if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
    if (c >= 'A' && c <= 'Z') {
        static const char* L = "E\nA SIU\rDRJNFCKTZLWHYPQOBG\"MXV";
        for (int i=0;i<26;i++) if (L[i]==c) { *shift=false; return ita2_letters[i+1]; }
    }
    if (c >= '0' && c <= '9') {
        static const char* F = "0123456789";
        for (int i=0;i<10;i++) if (F[i]==c) { *shift=true; return ita2_letters[i+1]; }
    }
    switch(c) {
        case ' ': *shift=false; return 0x0F;
        case '\r': *shift=false; return 0x08;
        case '\n': *shift=false; return 0x0C;
        default: *shift=false; return 0x0F;
    }
}

/* Protocol instance data */
typedef struct {
    mago_proto_rtty_params_t params;
    bool in_figure_shift;
} mago_rtty_proto_data_t;

static mago_rtty_proto_data_t g_rtty_data;

static mago_result_t rtty_encode(const mago_protocol_t* p, const mago_gps_nav_t* gps,
                                 const mago_telemetry_frame_t* telem, uint8_t* out, size_t* out_len) {
    mago_rtty_proto_data_t* d = (mago_rtty_proto_data_t*)p;
    char msg[256];
    int len = snprintf(msg, sizeof(msg),
        "$$%s,%lu,%02lu%02lu%02lu,%ld,%ld,%ld,%lu,%lu,%lu,%lu,%lu",
        d->params.callsign, telem->seq,
        telem->hour, telem->min, telem->sec,
        telem->lat, telem->lon, telem->alt,
        telem->speed, telem->heading, telem->sats,
        telem->batt_mv, telem->temp_c + 40);

    // Convert to ITA2 with shifts
    size_t bit_pos = 0;
    uint8_t bits[512];
    for (int i = 0; i < len; i++) {
        bool shift;
        uint8_t code = ascii_to_ita2(msg[i], &shift);
        if (shift != d->in_figure_shift) {
            // Insert shift code
            uint8_t sh = shift ? 0x1B : 0x1F;
            for (int b = 0; b < 5; b++) bits[bit_pos++] = (sh >> (4-b)) & 1;
            d->in_figure_shift = shift;
        }
        for (int b = 0; b < 5; b++) bits[bit_pos++] = (code >> (4-b)) & 1;
    }
    *out_len = bit_pos;
    if (out) {
        for (size_t i = 0; i < bit_pos; i++) {
            if (bits[i]) out[i/8] |= (1 << (7 - (i%8)));
        }
    }
    return MAGO_OK;
}

mago_protocol_t* mago_protocol_create_rtty(const mago_proto_rtty_params_t* params) {
    static mago_protocol_t proto = {
        .name = "rtty",
        .id = MAGO_PROTO_RTTY,
        .params = &g_rtty_data,
        .params_size = sizeof(g_rtty_data),
        .fec = NULL,
        .interleaver = NULL,
        .scrambler = NULL,
        .mapper = &mago_map_2fsk,
        .encode = rtty_encode,
    };
    if (params) g_rtty_data.params = *params;
    g_rtty_data.in_figure_shift = false;
    return &proto;
}

/* ============================================================================
 * REGISTRY
 * ============================================================================ */

bool mago_protocols_init(mago_protocol_registry_t* reg) {
    if (!reg) reg = &g_proto_reg;
    memset(reg, 0, sizeof(mago_protocol_registry_t));
    return true;
}

bool mago_protocols_register(mago_protocol_registry_t* reg, mago_protocol_t* proto) {
    if (!reg || !proto || reg->count >= MAGO_MAX_PROTOCOLS) return false;
    reg->protocols[reg->count++] = *proto;
    return true;
}

bool mago_protocols_unregister(mago_protocol_registry_t* reg, const char* name) {
    if (!reg || !name) return false;
    for (uint8_t i = 0; i < reg->count; i++) {
        if (strcmp(reg->protocols[i].name, name) == 0) {
            for (uint8_t j = i; j < reg->count - 1; j++) {
                reg->protocols[j] = reg->protocols[j+1];
            }
            reg->count--;
            return true;
        }
    }
    return false;
}

mago_protocol_t* mago_protocols_find(mago_protocol_registry_t* reg, const char* name) {
    if (!reg || !name) return NULL;
    for (uint8_t i = 0; i < reg->count; i++) {
        if (strcmp(reg->protocols[i].name, name) == 0) return &reg->protocols[i];
    }
    return NULL;
}

mago_protocol_t* mago_protocols_find_by_id(mago_protocol_registry_t* reg, mago_protocol_id_t id) {
    if (!reg) return NULL;
    for (uint8_t i = 0; i < reg->count; i++) {
        if (reg->protocols[i].id == id) return &reg->protocols[i];
    }
    return NULL;
}