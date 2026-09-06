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
 * @brief Envía un paquete LoRa (función no implementada)
 * @param context Contexto del dispositivo LR11XX
 * @param paquete Puntero al buffer con los datos a enviar
 * @param size Tamaño del paquete en bytes
 * @todo Implementar esta función
 */
void enviar_paquete(const void* context, int8_t* paquete, uint8_t size);

#ifdef __cplusplus
}
#endif

#endif  // MAIN_LORA_H