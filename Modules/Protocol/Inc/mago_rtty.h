/**
 * @file mago_rtty.h
 * @brief Mago41 - RTTY Protocol (ITA2/Baudot)
 *
 * Diferencia radical vs RS41HUP_V2:
 * - Plugin registrado en runtime
 * - Pipeline: encode -> (no FEC) -> 2FSK mapper -> TX
 * - Config declarativa (baud, shift, callsign)
 * - Sin dependencias Arduino
 */

#ifndef MAGO_RTTY_H
#define MAGO_RTTY_H

#include <stdint.h>
#include <stdbool.h>
#include "mago_types.h"
#include "mago_protocol.h"

/* ============================================================================
 * RTTY PARAMS
 * ============================================================================ */

// Defined in mago_protocol.h as mago_proto_rtty_params_t

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

mago_protocol_t* mago_rtty_create(const mago_proto_rtty_params_t* params);

#endif // MAGO_RTTY_H