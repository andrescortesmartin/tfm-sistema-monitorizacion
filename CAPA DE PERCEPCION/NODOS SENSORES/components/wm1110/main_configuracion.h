/**
 * @file main_configuracion.h
 * @brief Global application configuration
 *
 * Defines the system configuration structure, active modules,
 * IMU parameters, BLE scanning, ranging and LoRaWAN protocol constants.
 */

#ifndef MAIN_CONFIGURACION_H
#define MAIN_CONFIGURACION_H

#include "lsm6dsox_reg.h"
#include <stdbool.h>

/** @defgroup Modulos Flags de módulos activos
 * @{ 
 */
#define MODULO_GPS          (1 << 0) /**< GPS module enabled */
#define MODULO_BEACONS      (1 << 1) /**< BLE beacon scanning enabled */
#define MODULO_RANGING      (1 << 2) /**< LoRa ranging enabled */
#define MODULO_AMBIENTE     (1 << 3) /**< Pressure/temperature sensor enabled */
#define MODULO_IMU_XL       (1 << 4) /**< IMU accelerometer enabled */
#define MODULO_IMU_GY       (1 << 5) /**< IMU gyroscope enabled */
#define MODULO_ANGULO       (1 << 6) /**< Pitch/roll calculation enabled */
#define MODULO_LORAWAN      (1 << 7) /**< Activar/desactivar LoRaWAN*/
#define MODULO_IMU_CONTINUO (1 << 8) /**< Muestreo continuo del sensor IMU*/
#define MODULO_IMU_PERIODICO (1 << 9) /**< Muestreo periodico del sensor IMU ademas de por eventos*/
/** @} */

/** @defgroup FlagsEnvio Flags de envío LoRa
 * @{ 
 */
#define ENVIO_GPS      (1 << 0) /**< Send GPS data in LoRa uplink */
#define ENVIO_BEACONS  (1 << 1) /**< Send beacon scan data in LoRa uplink */
#define ENVIO_RANGING  (1 << 2) /**< Send ranging data in LoRa uplink */
#define ENVIO_AMBIENTE (1 << 3) /**< Send ambient sensor data in LoRa uplink */
#define ENVIO_ANGULO   (1 << 4) /**< Send pitch/roll data in LoRa uplink */
/** @} */

/**@brief Packs spreading factor and bandwidth for ranging
 *
 * @param   sf   Spreading factor (bits 0-3)
 * @param   bw   Bandwidth (bits 4-15)
 * @return Packed 16-bit value
 */
#define PACK_RANGING(sf, bw) (uint16_t)(((sf & 0x0F) << 12) | (bw & 0xFFF))

/**@struct logger_config_t
 * @brief Application configuration structure
 *
 * @details Contains all configurable system parameters, including
 * active modules, IMU configuration, LoRaWAN, BLE and ranging settings.
 */
typedef struct __attribute__((packed)) {
  /* Active module flags (OR of MODULO_*) */
  uint16_t  flags;
  /* Active send flags (OR of ENVIO_*) */
  uint8_t  flags_envio;
  /* Periods in seconds */
  uint16_t periodo_medida;   /**< Base tick period (battery always measured) */
  uint16_t periodo_angulo; /**< Ambient and angle measurement period */
  uint16_t periodo_gps;      /**< GPS measurement period */
  uint16_t periodo_ranging;  /**< Ranging measurement period */
  uint16_t periodo_beacons;  /**< BLE beacon scan period */
  uint16_t periodo_envio;    /**< LoRaWAN uplink period */
  uint16_t periodo_ambiente;
  uint16_t periodo_imu_periodico;
  /* Accelerometer configuration */
  uint8_t  imu_odr_xl;
  uint8_t  imu_scale_xl;
  uint8_t  imu_umbral_actividad;
  uint8_t  imu_duracion_inactividad;
  /* Gyroscope configuration */
  uint8_t  imu_odr_gy;
  uint8_t  imu_scale_gy;
  /* FIFO and modes configuration */
  uint8_t  imu_power_mode;
  uint8_t  imu_fifo_xl_odr;
  uint8_t  imu_fifo_gy_odr;
  uint16_t imu_fifo_muestras;
  uint8_t  imu_fifo_mode;
  /* BLE scanning configuration */
  uint8_t  duracion_escaneo;
  uint8_t  duty_escaneo;
  /* Ranging configuration */
  uint16_t sf_bw;
  uint8_t num_anclas; // Anclas desplegadas
  uint8_t media_medidas; // medidas por ancla para una medida robusta
  uint8_t anclas_objetivo; // medidas validas con las que parar
  int8_t tx_power_dbm; // Potencia de transmision de las peticiones
} logger_config_t;

/**@brief Global instance of the application configuration */
extern logger_config_t configuracion_app;

typedef struct{
  bool m_flag_descargar_datos;
  bool m_flag_borrado_flash;
  bool m_flag_ejecutar_ranging;
  bool m_flag_guardar_config;
  bool m_flag_movimiento;
  bool m_flag_watermark;
  bool m_flag_dispositivo_parado;
  bool m_flag_ble_conectado;
  bool m_flag_fix_valido;
  bool m_flag_reiniciar;
  bool m_flag_maquina_pendiente;
  bool m_flag_inactividad;
  bool m_flag_primer_paquete;
  bool m_flag_imu_disparo_periodico;
  bool m_flag_sector_ack_recibido;
  uint8_t m_sector_ack_status;
  bool m_flag_indication_confirmada;
} logger_flags_t;

extern volatile logger_flags_t flags_globales;

/**@enum Main state machine states
 * @brief Possible application states
 */
enum {
  ENCENDER_GPS = 0,         /**< State: turn on GPS */
  COMPROBAR_FIX,            /**< State: check GPS fix */
  ENCENDER_ESCANER_BEACONS, /**< State: start BLE scanning */
  APAGAR_ESCANER_BEACONS,   /**< State: stop BLE scanning */
  LECTURA_SENSORES,         /**< State: read sensors */
  TRANSMISION,              /**< State: transmit data */
  RANGING,                  /**< State: perform ranging */
  GUARDAR_FLASH             /**< State: save payload in flash */
};

typedef enum{
  CONFIG_OK = 0,
  CONFIG_ERR_PERIODO_MIN, // periodo 0 con un modulo activo
  CONFIG_ERR_ESCANEO,     // duracion de escaneo > periodo_beacons
  CONFIG_ERR_IMU,         // algun parametro invalido en la configuracion de la IMU
  CONFIG_ERR_RANGING      // algun parametro ranging incorrecto
} config_error_t;

bool validar_configuracion(const logger_config_t *cfg, config_error_t *error);

/** @defgroup Protocol constants
 * @{ 
 */
#define PAYLOAD_MAX_LORAWAN 222      /**< Tamaño maximo por envio lorawan segun parametros regionales */
#define PAYLOAD_MAX_FLASH 1024      /**< Peor caso real: 719 bytes con todos los modulos activos y anclas/beacons al maximo 
                                    (MAX_ANCLAS, MAX_MEDIDAS_ANCLA, MAX_LOGGERS_SAVED, MAX_BEACONS_SAVED).  */
#define MAX_ANCLAS 8
#define MAX_MEDIDAS_ANCLA 16

#define HEADER_LORA      0xA1     /**< Header for normal LoRa packets */
#define HEADER_IMU_XL    0xA2     /**< Header for IMU data */
#define HEADER_IMU_GY    0xA3     /**< Header for IMU data */
#define HEADER_IMU_XL_GY 0xA4     /**< Header for IMU data */

#define SECTOR_SIZE      4096     /**< Flash sector size */
/** @} */

#endif // MAIN_CONFIGURACION_H