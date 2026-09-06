/*!
 * @file      apps_configuration.h
 *
 * @brief     Common configuration
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2022. All rights reserved.
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

#ifndef APPS_CONFIGURATION_H
#define APPS_CONFIGURATION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lr11xx_radio_types.h"
#include <stdint.h>

/*!
 * @brief No se usa en la demo de ranging
 */
#ifndef PACKET_TYPE
#define PACKET_TYPE LR11XX_RADIO_PKT_TYPE_LORA
#endif

/*!
 * @brief Esta frecuencia se usa para la inicialización del ranging en tipo LoRa
 */
#ifndef RF_FREQ_IN_HZ
#define RF_FREQ_IN_HZ 868000000U
#endif
#ifndef PA_RAMP_TIME
#define PA_RAMP_TIME LR11XX_RADIO_RAMP_48_US
#endif
#ifndef FALLBACK_MODE
#define FALLBACK_MODE LR11XX_RADIO_FALLBACK_STDBY_RC
#endif
#ifndef ENABLE_RX_BOOST_MODE
#define ENABLE_RX_BOOST_MODE false
#endif

/*!
 * @brief Esta longitud de payload se usa para la inicialización del ranging en tipo LoRa
 */
#ifndef PAYLOAD_LENGTH
#define PAYLOAD_LENGTH 7
#endif

/*!
 * @brief Parámetros de modulación para paquetes LoRa
 */
#ifndef LORA_SPREADING_FACTOR
#define LORA_SPREADING_FACTOR LR11XX_RADIO_LORA_SF10
#endif
#ifndef LORA_BANDWIDTH
#define LORA_BANDWIDTH LR11XX_RADIO_LORA_BW_500
#endif
#ifndef LORA_CODING_RATE
#define LORA_CODING_RATE LR11XX_RADIO_LORA_CR_4_5
#endif

/*!
 * @brief Mantener la longitud de preámbulo en 12, ya que está relacionada con
 * el timing del proceso de ranging.
 */
#ifndef LORA_PREAMBLE_LENGTH
#define LORA_PREAMBLE_LENGTH 12
#endif
#ifndef LORA_PKT_LEN_MODE
#define LORA_PKT_LEN_MODE LR11XX_RADIO_LORA_PKT_EXPLICIT
#endif
/*!
 * @brief Mantener IQ estándar, ya que todas las tablas de calibración
 * disponibles están basadas en esto.
 */
#ifndef LORA_IQ
#define LORA_IQ LR11XX_RADIO_LORA_IQ_STANDARD
#endif
#ifndef LORA_CRC
#define LORA_CRC LR11XX_RADIO_LORA_CRC_ON
#endif

/*!
 * @brief Palabra de sincronización LoRa
 */
#ifndef LORA_SYNCWORD
#define LORA_SYNCWORD 0x12  // 0x12 Red privada, 0x34 Red pública
#endif

#ifdef __cplusplus
}
#endif

#endif  // APPS_CONFIGURATION_H