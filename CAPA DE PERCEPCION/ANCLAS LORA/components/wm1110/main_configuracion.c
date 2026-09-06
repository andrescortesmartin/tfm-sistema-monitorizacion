/**
 * @file main_configuracion.c
 * @brief Global application configuration implementation
 *
 * Defines default configuration values and provides functions
 * to print the current configuration over the debug port.
 */

#include "smtc_hal.h"

/**@brief Default application configuration
 *
 * @details All modules disabled at startup, with a 30 second period
 */
logger_config_t configuracion_app = {
  .flags = MODULO_AMBIENTE | MODULO_ANGULO | MODULO_IMU_XL | MODULO_LORAWAN,
  .flags_envio = ENVIO_AMBIENTE | ENVIO_ANGULO | ENVIO_GPS,
  .periodo_medida = 30,             // 
  .periodo_angulo = 30,             // 
  .periodo_gps = 300,               // 
  .periodo_ranging = 300,           // 
  .periodo_beacons = 120,           // 
  .periodo_envio  = 60,            // 
  .periodo_ambiente = 30,
  .imu_odr_xl = 0x04,               // Off
  .imu_scale_xl = 0x02,             // 4g
  .imu_umbral_actividad = 0x01,     // 9.62 ms
  .imu_duracion_inactividad = 0x01, // 4.92 s
  .imu_odr_gy = 0x00,               // off
  .imu_scale_gy = 0x02,             // 250 dps
  .imu_power_mode = 0x02,           // ultra low power mode
  .imu_fifo_xl_odr = 0x00,          // not batched
  .imu_fifo_gy_odr = 0x00,          // not batched
  .imu_fifo_muestras = 0x1E,        // 30 muestras a guardar
  .imu_fifo_mode = 0x00,            // modo bypass
  .duracion_escaneo = 0x0A,         // 10 segundos duracion
  .duty_escaneo = 50,               // duty 50%
  .sf_bw = 0x90FA                   // sf 9, bw 250
};

/**@brief Global flags structure for state and event management
 *
 * @details Used to coordinate actions between the main loop, interrupts and callbacks
 */
logger_flags_t volatile flags_globales = {
  .m_flag_descargar_datos = false,
  .m_flag_borrado_flash = false,
  .m_flag_ejecutar_ranging = false,
  .m_flag_guardar_config = false,
  .m_flag_movimiento = false,
  .m_flag_watermark = false,
  .m_flag_dispositivo_parado = false,
  .m_flag_ble_conectado = false,
  .m_flag_fix_valido = false,
  .m_flag_reiniciar = false,
  .m_flag_maquina_pendiente = false,
  .m_flag_inactividad = false,
};
