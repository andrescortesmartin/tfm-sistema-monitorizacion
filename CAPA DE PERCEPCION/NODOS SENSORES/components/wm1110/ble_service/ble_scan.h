/**
 * @file ble_scan.h
 * @brief Interfaz del módulo de escaneo BLE para beacons
 *
 * Define funciones para inicializar, iniciar y detener el escaneo BLE.
 * El escaneo detecta iBeacons y almacena su información (UUID, Major, Minor,
 * RSSI).
 */

#ifndef __PERIPHERAL_BLE_SCAN_H__
#define __PERIPHERAL_BLE_SCAN_H__

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inicializa el módulo de escaneo BLE
 * @details Configura los parámetros de escaneo y el handler de eventos
 */
void ble_scan_init(void);

/**
 * @brief Inicia el escaneo BLE
 * @details Comienza a buscar beacons en modo pasivo
 */
void ble_scan_start(void);

/**
 * @brief Detiene el escaneo BLE
 * @details Para el escaneo, vuelca beacons al payload y reinicia el buffer
 */
void ble_scan_stop(void);

void empaquetar_beacons(void);

#ifdef __cplusplus
}
#endif

#endif
