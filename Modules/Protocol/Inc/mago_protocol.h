/**
 * @file mago_protocol.h
 * @brief Mago41 - Protocol Framework (Pluggable)
 *
 * Diferencia radical vs RS41HUP_V2:
 * - NO hay protocolo "principal" hardcoded
 * - Protocolos se registran como plugins en runtime
 * - Cada protocolo: encode/decode/tx/rx independientes
 * - Scheduler agnóstico decide cuál transmitir
 * - FEC, interleaving, scrambling son módulos separados
 * - Fácil añadir nuevos (LoRa, AX.25 custom, etc)
 */

#ifndef MAGO_PROTOCOL_H
#define MAGO_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "mago_types.h"

/* ============================================================================
 * PROTOCOL REGISTRY
 * ============================================================================ */

#define MAGO_MAX_PROTOCOLS      8
#define MAGO_PROTO_NAME_MAX     16

typedef struct {
    mago_protocol_t protocols[MAGO_MAX_PROTOCOLS];
    uint8_t count;
} mago_protocol_registry_t;

/* ============================================================================
 * PROTOCOL PARAMS - Each protocol has its own config struct
 * ============================================================================ */

// RTTY (ITA2/Baudot)
typedef struct {
    uint16_t baud;
    uint16_t shift_hz;
    uint8_t bits;           // 7 or 8
    uint8_t stop_bits;      // 1 or 2
    bool extended_ascii;
    char callsign[16];
} mago_proto_rtty_params_t;

// MFSK family (Horus V1/V2, custom)
typedef struct {
    uint16_t baud;
    uint8_t modulation;     // 2, 4, 16 FSK
    uint16_t deviation_hz;
    bool fec_enabled;       // Golay(24,12)
    uint8_t interleaving;   // depth
    bool scrambling;
    uint16_t payload_id;
    uint8_t preamble_symbols;
    uint8_t sync_symbols;
    bool horus_v2;          // V2 adds CRC + scrambling
} mago_proto_mfsk_params_t;

// APRS 1200baud AFSK
typedef struct {
    char callsign[10];
    uint8_t ssid;
    char path[32];
    char symbol_table;
    char symbol_code;
    char comment[64];
    bool dao_enabled;
    uint8_t beacon_ratio;
    bool mic_e;             // Mic-E compression
} mago_proto_aprs_params_t;

// Morse CW
typedef struct {
    char message[128];
    uint16_t wpm;
    uint8_t farnsworth_wpm;
    uint16_t tone_hz;
    uint8_t interval_cycles;
} mago_proto_morse_params_t;

/* ============================================================================
 * FEC MODULES - Separated from protocols
 * ============================================================================ */

typedef enum {
    MAGO_FEC_NONE = 0,
    MAGO_FEC_GOLAY_24_12,
    MAGO_FEC_HAMMING_7_4,
    MAGO_FEC_HAMMING_8_4,
    MAGO_FEC_REED_SOLOMON,
    MAGO_FEC_CUSTOM,
} mago_fec_type_t;

typedef struct {
    mago_fec_type_t type;
    // Encode: input bits -> output bits
    mago_result_t (*encode)(const uint8_t* in, size_t in_bits, uint8_t* out, size_t* out_bits);
    // Decode: input bits -> output bits (corrected)
    mago_result_t (*decode)(const uint8_t* in, size_t in_bits, uint8_t* out, size_t* out_bits, uint8_t* errors_corrected);
    // Get overhead
    float (*overhead)(void);
} mago_fec_ops_t;

/* ============================================================================
 * INTERLEAVING MODULE
 * ============================================================================ */

typedef enum {
    MAGO_ILV_NONE = 0,
    MAGO_ILV_BLOCK,
    MAGO_ILV_CONVOLUTIONAL,
    MAGO_ILV_CUSTOM,
} mago_ilv_type_t;

typedef struct {
    mago_ilv_type_t type;
    uint8_t depth;
    mago_result_t (*interleave)(const uint8_t* in, uint8_t* out, size_t len);
    mago_result_t (*deinterleave)(const uint8_t* in, uint8_t* out, size_t len);
} mago_ilv_ops_t;

/* ============================================================================
 * SCRAMBLING MODULE
 * ============================================================================ */

typedef enum {
    MAGO_SCR_NONE = 0,
    MAGO_SCR_XOR_SHIFT,
    MAGO_SCR_LFSR_7,
    MAGO_SCR_LFSR_15,
    MAGO_SCR_CUSTOM,
} mago_scr_type_t;

typedef struct {
    mago_scr_type_t type;
    uint32_t seed;
    mago_result_t (*scramble)(uint8_t* data, size_t len, uint32_t* seed);
    mago_result_t (*descramble)(uint8_t* data, size_t len, uint32_t* seed);
} mago_scr_ops_t;

/* ============================================================================
 * SYMBOL MAPPING - Modulation agnostic
 * ============================================================================ */

typedef enum {
    MAGO_MAP_BPSK = 0,      // 1 bit/sym
    MAGO_MAP_QPSK = 1,      // 2 bits/sym (2FSK/4FSK)
    MAGO_MAP_8PSK = 2,      // 3 bits/sym
    MAGO_MAP_16QAM = 3,     // 4 bits/sym (16FSK)
    MAGO_MAP_AFSK = 4,      // AFSK tones
    MAGO_MAP_OOK = 5,       // On-Off Keying (CW)
    MAGO_MAP_CUSTOM = 6,
} mago_mapping_t;

typedef struct {
    mago_mapping_t type;
    uint8_t bits_per_symbol;
    mago_result_t (*bits_to_symbols)(const uint8_t* bits, size_t n_bits, uint8_t* symbols, size_t* n_sym);
    mago_result_t (*symbols_to_bits)(const uint8_t* symbols, size_t n_sym, uint8_t* bits, size_t* n_bits);
} mago_mapper_ops_t;

/* ============================================================================
 * PROTOCOL INSTANCE
 * ============================================================================ */

struct mago_protocol {
    char name[MAGO_PROTO_NAME_MAX];
    mago_protocol_id_t id;
    uint8_t priority;

    // Config
    const void* params;       // Protocol-specific params
    size_t params_size;

    // Pipeline modules
    const mago_fec_ops_t* fec;
    const mago_ilv_ops_t* interleaver;
    const mago_scr_ops_t* scrambler;
    const mago_mapper_ops_t* mapper;

    // Core callbacks
    mago_result_t (*encode)(const mago_protocol_t* p,
        const mago_gps_nav_t* gps, const mago_telemetry_frame_t* telem,
        uint8_t* out, size_t* out_len);

    mago_result_t (*decode)(const mago_protocol_t* p,
        const uint8_t* data, size_t len, mago_telemetry_frame_t* out);

    // Radio interface
    mago_result_t (*transmit)(const mago_protocol_t* p, const uint8_t* data, size_t len);
    mago_result_t (*receive_start)(const mago_protocol_t* p);
    void (*receive_stop)(const mago_protocol_t* p);
    void (*receive_symbol)(const mago_protocol_t* p, uint8_t symbol);

    // Timing
    uint32_t symbol_period_us;
    uint32_t preamble_us;
    uint32_t guard_us;

    // State
    bool tx_active;
    uint32_t tx_bytes_remaining;
    uint32_t rx_bytes_received;
};

/* ============================================================================
 * BUILT-IN FEC IMPLEMENTATIONS
 * ============================================================================ */

// Golay (24,12) - corrects up to 3 errors
extern const mago_fec_ops_t mago_fec_golay2412;

// Hamming (7,4)
extern const mago_fec_ops_t mago_fec_hamming74;

// Hamming (8,4) extended
extern const mago_fec_ops_t mago_fec_hamming84;

/* ============================================================================
 * BUILT-IN INTERLEAVERS
 * ============================================================================ */

extern const mago_ilv_ops_t mago_ilv_block_2;
extern const mago_ilv_ops_t mago_ilv_block_4;
extern const mago_ilv_ops_t mago_ilv_block_8;

/* ============================================================================
 * BUILT-IN SCRAMBLERS
 * ============================================================================ */

extern const mago_scr_ops_t mago_scr_xorshift;
extern const mago_scr_ops_t mago_scr_lfsr7;
extern const mago_scr_ops_t mago_scr_lfsr15;

/* ============================================================================
 * BUILT-IN MAPPERS
 * ============================================================================ */

extern const mago_mapper_ops_t mago_map_bpsk;
extern const mago_mapper_ops_t mago_map_2fsk;
extern const mago_mapper_ops_t mago_map_4fsk;
extern const mago_mapper_ops_t mago_map_16fsk;
extern const mago_mapper_ops_t mago_map_afsk_1200;
extern const mago_mapper_ops_t mago_map_ook;

/* ============================================================================
 * PROTOCOL FACTORIES
 * ============================================================================ */

mago_protocol_t* mago_protocol_create_rtty(const mago_proto_rtty_params_t* params);
mago_protocol_t* mago_protocol_create_mfsk(const mago_proto_mfsk_params_t* params);
mago_protocol_t* mago_protocol_create_aprs(const mago_proto_aprs_params_t* params);
mago_protocol_t* mago_protocol_create_morse(const mago_proto_morse_params_t* params);

/* ============================================================================
 * REGISTRY API
 * ============================================================================ */

bool mago_protocols_init(mago_protocol_registry_t* reg);
bool mago_protocols_register(mago_protocol_registry_t* reg, mago_protocol_t* proto);
bool mago_protocols_unregister(mago_protocol_registry_t* reg, const char* name);
mago_protocol_t* mago_protocols_find(mago_protocol_registry_t* reg, const char* name);
mago_protocol_t* mago_protocols_find_by_id(mago_protocol_registry_t* reg, mago_protocol_id_t id);

#endif // MAGO_PROTOCOL_H