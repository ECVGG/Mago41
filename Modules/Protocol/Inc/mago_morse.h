/**
 * @file mago_morse.h
 * @brief Mago41 - Morse CW Protocol
 *
 * Diferencia radical vs RS41HUP_V2:
 * - Plugin registrado en runtime
 * - Pipeline: encode -> OOK mapper -> TX
 * - Configurable WPM, Farnsworth, tone
 * - Modo CW continuo o por intervalos
 */

#ifndef MAGO_MORSE_H
#define MAGO_MORSE_H

#include <stdint.h>
#include <stdbool.h>
#include "mago_types.h"
#include "mago_protocol.h"

/* ============================================================================
 * MORSE PARAMS - Defined in mago_protocol.h as mago_proto_morse_params_t
 * ============================================================================ */

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

mago_protocol_t* mago_morse_create(const mago_proto_morse_params_t* params);

#endif // MAGO_MORSE_H