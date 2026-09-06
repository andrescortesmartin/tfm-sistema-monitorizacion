/*!
 * @file      main_lorawan.h
 *
 * @brief     Configuración de la aplicación de la ancla LoRa: parámetros del
 *            heartbeat LoRaWAN y del subordinado RTToF
 *
 * @copyright
 * The Clear BSD License
 * Copyright Semtech Corporation 2021. All rights reserved.
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

#ifndef MAIN_LORAWAN_H
#define MAIN_LORAWAN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define LORAWAN_APP_PORT          2
#define LORAWAN_CONFIRMED_MSG_ON  false

/** @brief Configuración de la ancla, persistida en flash y actualizable por downlink. */
typedef struct {
  uint32_t heartbeat_interval_s;        /**< Periodo del uplink de heartbeat en segundos (default: 60). */
  uint32_t activation_threshold_ms;     /**< Tiempo libre mínimo del modem para abrir una ventana RTToF (default: 2000). */
  uint32_t margen_ms;                   /**< Margen reservado al final de la ventana antes de devolver la radio al modem (default: 1000). */
  uint8_t  lora_sf;                     /**< Spreading factor LoRa (@ref lr11xx_radio_lora_sf_t). */
  uint8_t  lora_bw;                     /**< Ancho de banda LoRa (@ref lr11xx_radio_lora_bw_t). */
  uint8_t  _pad[2];                     /**< Relleno de alineación a 4 bytes. */
} anchor_config_t;

/** @brief Instancia global con la configuración vigente de la ancla. */
extern anchor_config_t anchor_config;

/** @brief Dirección del ancla. El logger cambia su propia dirección para coincidir con este valor. */
#ifndef RTTOF_ADDRESS
#define RTTOF_ADDRESS  UINT32_C(0x00000001)
#endif

/** @brief Bytes de la dirección que el hardware compara al recibir una solicitud */
#define RTTOF_SUBORDINATE_CHECK_LENGTH_BYTES  (4)

/** @brief Símbolos de respuesta RTToF */
#ifndef RESPONSE_SYMBOLS_COUNT
#define RESPONSE_SYMBOLS_COUNT  UINT8_C(15)
#endif

/** @brief Longitud en bytes del resultado RTToF */
#define LR11XX_RTTOF_RESULT_LENGTH  (4)

/** @brief Máscara estricta: SOLO interrupciones de Ranging y Errores (Nada de CAD) */
#define RTTOF_SUBORDINATE_IRQ_MASK ( LR11XX_SYSTEM_IRQ_RTTOF_REQ_DISCARDED | LR11XX_SYSTEM_IRQ_RTTOF_RESP_DONE | LR11XX_SYSTEM_IRQ_RTTOF_REQ_VALID)

#ifdef __cplusplus
}
#endif

#endif  // MAIN_LORAWAN_H
