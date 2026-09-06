/**
 * @file GPS.h
 * @brief Driver para el módulo GPS
 *
 * Proporciona funciones para controlar el módulo GPS, incluyendo
 * encendido/apagado, decodificación de mensajes NMEA y gestión de mediciones de
 * posicionamiento.
 */

#ifndef GPS_H
#define GPS_H

#include <stdbool.h>

#define GPS_HDOP_MAX  10.0f

/**
 * @brief Enciende el módulo GPS
 * @details Inicializa la UART y alimenta el módulo GPS para comenzar
 *          la adquisición de señales satelitales
 */
void gps_on(void);

/**
 * @brief Apaga el módulo GPS
 * @details Desactiva la UART y corta la alimentación del módulo GPS
 *          para ahorrar energía
 */
void gps_off(void);

/**
 * @brief Realiza una medición GPS completa
 * @details Lee y decodifica líneas del GPS cuando corresponde muestrear,
 *          añadiendo los datos de posición al payload de transmisión
 */
void medir_GPS(void);

/**
 * @brief Procesa líneas GPS en background y apaga el módulo si el fix es válido
 * @details Llamar desde el main loop mientras el GPS está encendido.
 *          Apaga el GPS en cuanto GGA es válido, sin esperar a COMPROBAR_FIX.
 */
void gps_process_background(void);

#endif // GPS_H
