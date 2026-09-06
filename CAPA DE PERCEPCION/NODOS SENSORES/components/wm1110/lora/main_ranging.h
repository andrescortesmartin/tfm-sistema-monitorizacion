/**
 * @file      main_ranging.h
 *
 * @brief     Ranging implementation for LR1110
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2024. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted (subject to the limitations in the disclaimer
 * below) provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Semtech corporation nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE GRANTED BY
 * THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT
 * NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL SEMTECH CORPORATION BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef MAIN_RANGING_H
#define MAIN_RANGING_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lr11xx_hal_context.h"
#include "lr11xx_radio_types.h"
#include "lr11xx_system.h"
#include "lr11xx_types.h"
#include <stdbool.h>
#include <stdint.h>

/** @brief Resultado de una medida RTToF */
typedef struct rttof_result_s {
  int32_t distance_m;  /**< Distancia medida, en metros */
  int32_t rssi;        /**< RSSI de la recepción, en dBm */
} rttof_result_t;

/** @brief Mascara de interrupciones para el manager */
#define RTTOF_MANAGER_IRQ_MASK (LR11XX_SYSTEM_IRQ_RTTOF_EXCH_VALID | LR11XX_SYSTEM_IRQ_RTTOF_TIMEOUT)

/** @brief Longitud del resultado en bytes */
#define LR11XX_RTTOF_RESULT_LENGTH 4

/**
 * @brief Timeouts consecutivos sin medida válida antes de abandonar el ancla.
 * Se resetea en cada medida válida: protege contra ancla inaccesible, no
 * contra canal intermitente que intercala éxitos y fallos.
 */
#define FALLOS_MAX_POR_ANCLA 20

/** @brief Dirección de ancla RTToF por defecto */
#define RANGING_DEFAULT_ADDR 0x00000000

/** @brief Numero de simbolos en la respuesta RTToF */
#ifndef RESPONSE_SYMBOLS_COUNT
#define RESPONSE_SYMBOLS_COUNT UINT8_C(15)
#endif

/**
 * @brief Timeout del maestro RTToF [ms]. Timeout para recibir una respuesta despues de enviar una solicitud RTToF.
 */
#ifndef MANAGER_TX_RX_TIMEOUT_MS
#define MANAGER_TX_RX_TIMEOUT_MS UINT32_C(3276)
#endif

/** @brief Periodo de reposo del maestro despues del RTToF [ms] */
#ifndef MANAGER_RTTOF_SLEEP_PERIOD
#define MANAGER_RTTOF_SLEEP_PERIOD UINT32_C(1000)
#endif

/**
 * @brief Función principal de ranging
 * @param context Contexto del LR11xx
 */
void ranging(const void* context);

#ifdef __cplusplus
}
#endif

#endif  // MAIN_RTTOF_H
