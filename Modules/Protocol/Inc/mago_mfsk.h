/**
 * @file mago_mfsk.h
 * @brief Mago41 - MFSK Protocol (Horus V1/V2, Custom)
 *
 * Diferencia radical vs RS41HUP_V2:
 * - Plugin registrado en runtime
 * - Pipeline: encode -> FEC (Golay) -> Interleave -> Scramble -> MFSK mapper -> TX
 * - Horus V1: 4FSK, Golay(24,12), block interleave, no scramble
 * - Horus V2: 4FSK, Golay(24,12), block interleave, xorshift scramble
 * - Config declarativa (baud, modulation, FEC, interleaving, scrambling)
 */

#ifndef MAGO_MFSK_H
#define MAGO_MFSK_H

#include <stdint.h>
#include <stdbool.h>
#include "mago_types.h"
#include "mago_protocol.h"

/* ============================================================================
 * MFSK PARAMS - Defined in mago_protocol.h as mago_proto_mfsk_params_t
 * ============================================================================ */

// Payload IDs
#define MAGO_MFSK_PAYLOAD_HORUS_V1  0x0100
#define MAGO_MFSK_PAYLOAD_HORUS_V2  0x0200
#define MAGO_MFSK_PAYLOAD_CUSTOM    0xFFFF

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

mago_protocol_t* mago_mfsk_create(const mago_proto_mfsk_params_t* params);

#endif // MAGO_MFSK_H