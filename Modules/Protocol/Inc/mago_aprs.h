/**
 * @file mago_aprs.h
 * @brief Mago41 - APRS Protocol (1200 baud AFSK AX.25)
 *
 * Diferencia radical vs RS41HUP_V2:
 * - Plugin registrado en runtime
 * - Pipeline: encode -> NRZI -> AFSK 1200/2200 Hz -> TX
 * - Soporta posición, telemetría, mensaje, Mic-E
 * - Config declarativa (callsign, path, symbol, comment)
 */

#ifndef MAGO_APRS_H
#define MAGO_APRS_H

#include <stdint.h>
#include <stdbool.h>
#include "mago_types.h"
#include "mago_protocol.h"

/* ============================================================================
 * APRS PARAMS - Defined in mago_protocol.h as mago_proto_aprs_params_t
 * ============================================================================ */

// APRS AX.25 constants
#define APRS_AX25_FLAG        0x7E
#define APRS_AX25_ESCAPE      0x7D
#define APRS_AX25_ESCAPE_XOR  0x20

// AX.25 frame types
#define APRS_FRAME_UI         0x03
#define APRS_PID_NO_LAYER3    0xF0

// Symbol tables
#define APRS_SYMBOL_TABLE_PRIMARY   '/'
#define APRS_SYMBOL_TABLE_SECONDARY '\\'

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

mago_protocol_t* mago_aprs_create(const mago_proto_aprs_params_t* params);

#endif // MAGO_APRS_H