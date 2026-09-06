/**
 * @file main_LPS22DF.h
 * @brief Driver principal para el sensor de presión y temperatura LPS22DF
 *
 * Este módulo proporciona la interfaz para configurar y leer el sensor LPS22DF
 * a través de I2C. Permite realizar mediciones de presión atmosférica y
 * temperatura ambiente en modo one-shot.
 */

#ifndef MAIN_LPS22DF_H
#define MAIN_LPS22DF_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Configura e inicializa el sensor LPS22DF
 * @details Inicializa el contexto del dispositivo I2C, verifica el ID del
 * sensor, realiza un reset, configura el modo de bus (filtro e interfaz), y
 * establece el modo de operación en one-shot con promediado de 4 muestras
 */
bool lps22df_config(void);

/**
 * @brief Realiza una medición de presión y temperatura ambiente
 * @details Dispara una medición one-shot, lee los datos de presión (en hPa) y
 *          temperatura (en °C), y añade los valores al payload de transmisión.
 *          La presión se envía como int16_t y la temperatura como int8_t
 */
void medir_ambiente(void);

#endif // MAIN_LPS22DF_H
