/*!
 * @file      main_lorawan.h
 *
 * @brief     LoRa Basics Modem Class A/C device application configuration
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
#include "smtc_modem_api.h"
#include "app_ble_all.h"
#include "main_ranging.h"
#include "main_configuracion.h"
#include <stddef.h>
#include <stdint.h>

/** @brief Puerto de aplicación LoRaWAN */
#define LORAWAN_APP_PORT 2

/** @brief Habilita el modo de sueño parcial (permite ahorrar energía) */
#define APP_PARTIAL_SLEEP true

/** @brief Contadores de tiempo transcurrido por módulo de medida, en segundos */
typedef struct {
  uint32_t ambiente;       /**< Contador de tiempo para el módulo de ambiente */
  uint32_t angulo;         /**< Contador de tiempo para el módulo de ángulo */
  uint32_t gps;             /**< Contador de tiempo para el módulo de GPS */
  uint32_t ranging;        /**< Contador de tiempo para el módulo de ranging */
  uint32_t beacons;        /**< Contador de tiempo para el módulo de escaneo de beacons BLE */
  uint32_t envio;          /**< Contador de tiempo para el envío LoRaWAN */
  uint32_t imu_periodico;  /**< Contador de tiempo para el disparo periódico de IMU */
} tick_counters_t;

/** @brief Últimas medidas de todos los sensores/módulos, usadas para construir los payloads */
typedef struct {
    // Ambiente
    int16_t  presion;      /**< Última presión atmosférica medida, en hPa */
    int8_t   temperatura;  /**< Última temperatura medida, en grados Celsius */

    // Angulo
    int16_t  pitch;  /**< Último ángulo de pitch medido por el IMU, en grados */
    int16_t  roll;   /**< Último ángulo de roll medido por el IMU, en grados */

    // Bateria
    uint16_t bateria_mv;  /**< Última tensión de batería medida, en milivoltios */

    // Ranging
    uint16_t ranging[MAX_ANCLAS];       /**< Distancia media medida a cada ancla, en metros */
    uint32_t ranging_addr[MAX_ANCLAS];  /**< Dirección de cada ancla con medida válida */
    int8_t   ranging_rssi[MAX_ANCLAS];  /**< RSSI medio de cada ancla, en dBm */
    uint8_t  ranging_len;               /**< Número de anclas con medida válida en el ciclo */

    uint8_t  ranging_muestras_n[MAX_ANCLAS];                                /**< Número de muestras válidas por ancla */
    int16_t  ranging_muestras_dist[MAX_ANCLAS][MAX_MEDIDAS_ANCLA];          /**< Muestras individuales de distancia por ancla, en metros */
    int8_t   ranging_muestras_rssi[MAX_ANCLAS][MAX_MEDIDAS_ANCLA];          /**< Muestras individuales de RSSI por ancla, en dBm */

    // BLE Scan
    uint8_t ble_logger_count;                       /**< Número de Loggers HOWL detectados en el escaneo */
    uint8_t ble_logger_ids[MAX_LOGGERS_SAVED];       /**< Identificador (YY) de cada Logger detectado */
    int8_t  ble_logger_rssi[MAX_LOGGERS_SAVED];      /**< RSSI de cada Logger detectado, en dBm */
    uint8_t ble_logger_obs[MAX_LOGGERS_SAVED];       /**< Observaciones/contador de avistamientos de cada Logger */

    uint8_t ble_beacon_count;                                  /**< Número de Beacons HOWL detectados en el escaneo */
    uint8_t ble_beacon_ids[MAX_BEACONS_SAVED];                 /**< Identificador (YY) de cada Beacon detectado */
    int8_t  ble_beacon_rssi[MAX_BEACONS_SAVED];                /**< RSSI de cada Beacon detectado, en dBm */
    uint8_t ble_beacon_obs[MAX_BEACONS_SAVED];                 /**< Observaciones/contador de avistamientos de cada Beacon */
    uint8_t ble_beacon_data[MAX_BEACONS_SAVED][MAX_BEACON_SIZE]; /**< Payload de advertising bruto de cada Beacon detectado */

    // GPS
    int32_t gps_lat;  /**< Última latitud GPS */
    int32_t gps_lon;  /**< Última longitud GPS */

    // Diagnostico
    uint16_t reset_count;         /**< Número de reinicios del módem LoRaWAN */
    uint8_t ttff_gps;             /**< Time-to-first-fix del GPS del último ciclo, en segundos */
    uint32_t flash_dir_lora;      /**< Dirección de escritura actual en la zona de flash de LoRaWAN */
    uint32_t flash_dir_imu;       /**< Dirección de escritura actual en la zona de flash de IMU */
    uint16_t actividad_eventos;   /**< Contador de eventos de actividad/movimiento desde el último envío */
    uint8_t flash_descarte;       /**< Indica si se ha descartado algún paquete por falta de espacio en flash */

    // Timestamps
    uint32_t ts_ambiente;  /**< Timestamp Unix de la última medida de ambiente */
    uint32_t ts_angulo;    /**< Timestamp Unix de la última medida de ángulo */
    uint32_t ts_bateria;   /**< Timestamp Unix de la última medida de batería */
    uint32_t ts_ranging;   /**< Timestamp Unix de la última medida de ranging */
    uint32_t ts_ble_scan;  /**< Timestamp Unix del último escaneo de beacons BLE */
    uint32_t ts_gps;       /**< Timestamp Unix del último fix de GPS */
} ultimas_medidas_t;

/** @brief Últimas medidas de todos los sensores/módulos, usadas para construir los payloads */
extern ultimas_medidas_t ultimas_medidas;

/**
 * @brief Agrega datos a un buffer de payload, respetando su tamaño máximo
 * @param buf Buffer de destino
 * @param len Puntero a la longitud actual del buffer, se incrementa en @c n bytes si cabe
 * @param max Tamaño máximo del buffer
 * @param data Puntero a los datos a agregar
 * @param n Longitud de los datos a agregar, en bytes
 * @return true si los datos cupieron y se agregaron, false si se excedía @c max
 */
bool payload_add(uint8_t* buf, uint16_t *len, uint16_t max, const uint8_t* data, size_t n);

/**
 * @brief Suspende la actividad del módem para uso directo del radio
 * @details Deshabilita interrupciones del módem antes de operaciones de radio
 * manuales
 */
void modem_suspender_actividad(void);

/**
 * @brief Reanuda la actividad del módem tras uso directo del radio
 * @details Reactiva interrupciones del módem después de operaciones de radio
 * manuales
 */
void modem_reanudar_actividad(void);

/**
 * @brief Entra en modo BLE (pausa de sensores y módem)
 * @details Detiene IMU, cancela alarmas y suspende actividad del módem
 */
void entrar_modo_ble(void);

/**
 * @brief Sale del modo BLE (reactivación de operación normal)
 * @details Reactiva IMU, reanuda módem y programa siguiente envío
 */
void salir_modo_ble(void);

/**
 * @brief Sincroniza el timestamp con el tiempo absoluto
 * @param absolute_unix_timestamp_s Timestamp absoluto en segundos
 */
void sync_timestamp(uint32_t absolute_unix_timestamp_s);

/**
 * @brief Obtiene el timestamp actual
 * @return Timestamp actual en segundos
 */
uint32_t get_timestamp(void);

#ifdef __cplusplus
}
#endif

#endif