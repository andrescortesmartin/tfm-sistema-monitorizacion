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

#define HOWL_TYPE_LOGGER 0x4C // 'L'
#define HOWL_TYPE_BEACON 0x42 // 'B'

#define MAX_LOGGERS_SAVED 5
#define MAX_BEACONS_SAVED 10  /*!< Número máximo de beacons almacenables */
#define MAX_BEACON_SIZE   21  /*!< Tamaño máximo de cada paquete de beacon */

/**
 * @brief Transmite datos por el servicio NUS (Nordic UART Service)
 * @param buffer Puntero al buffer de datos a transmitir
 * @param tamaño_datos Tamaño total de los datos en bytes
 * @details Divide automáticamente en paquetes si excede el MTU
 */
bool transmision_NUS(uint8_t *buffer, uint16_t tamaño_datos);

/**
 * @brief Espera a que el SoftDevice transmita todas las notificaciones/indicaciones encoladas
 * @param timeout_ms Tiempo máximo de espera, en milisegundos
 * @return true si se vació la cola antes del timeout, false si se desconectó o se agotó el tiempo
 */
bool app_ble_esperar_tx_vacio(uint32_t timeout_ms);

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
 * @brief Envía datos por el servicio NUS (Nordic UART Service)
 * @param p_data Puntero al buffer de datos a enviar
 * @param length Longitud de los datos a enviar
 * @details Solo intenta enviar si hay un dispositivo conectado
 */
void app_enviar_datos_nus(uint8_t * p_data, uint16_t length);

/**
 * @brief Envía datos como indication NUS y espera la confirmación del central
 * @param p_data Puntero al buffer de datos a enviar
 * @param length Longitud de los datos a enviar
 * @param max_intentos Número máximo de reintentos si no llega confirmación a tiempo
 * @param timeout_s Tiempo máximo de espera de confirmación por intento, en segundos
 * @return true si el central confirmó la indicación, false si se desconectó o se agotaron los intentos sin confirmación
 * @details Reintenta el envío si la cola del SoftDevice está llena; si no llega
 * confirmación dentro de @p timeout_s, reintenta hasta @p max_intentos veces
 */
bool app_enviar_indicacion_confirmada(uint8_t *p_data, uint16_t length, uint8_t max_intentos, uint32_t timeout_s);

/**
 * @brief Imprime trazas de depuración BLE (formato printf)
 * @param fmt Cadena de formato
 * @param ... Argumentos variables
 */
void app_ble_trace_print(const char *fmt, ...);

/**
 * @brief Obtiene la dirección MAC BLE del dispositivo
 * @param mac_out Buffer de 6 bytes donde se copia la dirección MAC
 */
void get_ble_mac(uint8_t *mac_out);

/**
 * @brief Procesa los comandos BLE pendientes mientras hay una conexión activa
 * @details Atiende, si están pendientes: la descarga de datos por NUS, el
 * borrado de flash, y el reinicio del dispositivo (enviando primero un ACK)
 */
void procesar_comandos_ble(void);

#ifdef __cplusplus
}
#endif

#endif
