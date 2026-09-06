/**
 * @file main_lora.h
 * @brief Driver para funciones básicas del transceiver LoRa LR11XX
 *
 * Proporciona funciones para inicialización del sistema LR11XX, control de
 * sleep y envío de paquetes LoRa.
 */

#ifndef MAIN_LORA_H
#define MAIN_LORA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "lr11xx_radio_types.h"

/**
 * @brief Pone el radio LR1110 en modo sleep
 * @param context Contexto del dispositivo LR11XX
 * @details Configura el reloj de baja frecuencia y pone el radio en modo sleep con warm start habilitado
 */
void lr1110_sleep_enter(const void* context);

/**
 * @brief Inicializa el sistema LR11XX
 * @param context Contexto del dispositivo LR11XX
 * @details Realiza reset, configura regulador, switch RF, TCXO, reloj LF y ejecuta calibración completa
 */
void lr11xx_system_init(const void* context);

/**
 * @brief Calcula el LDRO, que indica si se debe habilitar la opción de baja tasa de datos en LoRa
 * @param sf Spreading Factor
 * @param bw Ancho de banda
 * @return LDRO
 */
uint8_t compute_lora_ldro(const lr11xx_radio_lora_sf_t sf, const lr11xx_radio_lora_bw_t bw);

#ifdef __cplusplus
}
#endif

#endif  // MAIN_LORA_H