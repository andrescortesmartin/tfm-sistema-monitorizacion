/**
 * @file app_ble_all.h
 * @brief Interfaz del módulo BLE completo (advertising, NUS, escaneo)
 *
 * Proporciona funciones para inicializar y gestionar advertising BLE,
 * servicio Nordic UART (NUS), y escaneo de beacons.
 */

#ifndef __APP_BLE_ALL_H__
#define __APP_BLE_ALL_H__

#include "ble_scan.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_BEACONS_SAVED 10  /*!< Maximum number of storable beacons */
#define MAX_BEACON_SIZE   22  /*!< Maximum size of each beacon packet */

/**
 * @brief Transmite datos por el servicio NUS (Nordic UART Service)
 * @param buffer Puntero al buffer de datos a transmitir
 * @param tamaño_datos Tamaño total de los datos en bytes
 * @details Divide automáticamente en paquetes si excede el MTU
 */
void transmision_NUS(uint8_t *buffer, uint16_t tamaño_datos);

/**
 * @brief Inicializa el módulo BLE completo
 * @details Configura stack BLE, GAP, GATT, servicios, advertising y escaneo
 */
void app_ble_all_init(void);

/**
 * @brief Inicia el advertising BLE
 */
void app_ble_advertising_start(void);

/**
 * @brief Detiene el advertising BLE
 */
void app_ble_advertising_stop(void);

/**
 * @brief Obtiene el estado de conexión BLE
 * @return true si está desconectado, false si está conectado
 */
bool app_ble_is_disconnected(void);

/**
 * @brief Desconecta la conexión BLE activa
 * @details Intenta desconectar hasta 10 veces con delays de 100μs
 */
void app_ble_disconnect(void);

/**
 * @brief Configura los parámetros de conexión BLE
 * @param fast true para parámetros rápidos (descarga NUS), false para lentos (ahorro batería)
 */
void ble_conn_params_set(bool fast);

/**
 * @brief Envía datos por el servicio NUS (Nordic UART Service)
 * @param p_data Puntero al buffer de datos a enviar
 * @param length Longitud de los datos a enviar
 * @details Solo intenta enviar si hay un dispositivo conectado
 */
void app_enviar_datos_nus(uint8_t * p_data, uint16_t length);

bool esperar_ack_sector(void);


/**
 * @brief Imprime trazas de depuración BLE (formato printf)
 * @param fmt Cadena de formato
 * @param ... Argumentos variables
 */
void app_ble_trace_print(const char *fmt, ...);

void get_ble_mac(uint8_t *mac_out);

void procesar_comandos_ble(void);


#ifdef __cplusplus
}
#endif

#endif
