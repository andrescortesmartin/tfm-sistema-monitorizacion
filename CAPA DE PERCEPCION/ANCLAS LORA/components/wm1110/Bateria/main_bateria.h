/**
 * @file main_bateria.h
 * @brief Driver para medición del nivel de batería
 *
 * Este módulo proporciona funciones para medir y reportar el nivel de batería
 * del dispositivo, añadiendo los datos al payload de transmisión.
 */

#ifndef MAIN_BATERIA_H
#define MAIN_BATERIA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief Mide el nivel de batería y añade el dato al payload
 * @details Lee el nivel de batería del sistema y lo añade al payload
 *          de transmisión para ser enviado por LoRaWAN
 */
void medir_bateria(void);

#ifdef __cplusplus
}
#endif
#endif // MAIN_BATERIA_H
