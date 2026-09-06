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
  .flags = MODULO_AMBIENTE | MODULO_ANGULO | MODULO_LORAWAN,
  .flags_envio = ENVIO_AMBIENTE | ENVIO_ANGULO,
  .periodo_medida = 60,             // 
  .periodo_angulo = 20,             // 
  .periodo_gps = 1800,               // 
  .periodo_ranging = 600,           // 
  .periodo_beacons = 600,           // 
  .periodo_envio  = 60,            // 
  .periodo_ambiente = 20,
  .periodo_imu_periodico = 60,
  .imu_odr_xl = 0x04,               // 104 Hz
  .imu_scale_xl = 0x02,             // 4g
  .imu_umbral_actividad = 0x01,     // 9.62 ms
  .imu_duracion_inactividad = 0x01, // 4.92 s
  .imu_odr_gy = 0x00,               // off
  .imu_scale_gy = 0x02,             // 250 dps
  .imu_power_mode = 0x02,           // ultra low power mode
  .imu_fifo_xl_odr = 0x00,          // not batched
  .imu_fifo_gy_odr = 0x00,          // not batched
  .imu_fifo_muestras = 0x4E,        // 78 muestras a guardar
  .imu_fifo_mode = 0x00,            // modo bypass
  .duracion_escaneo = 0x0A,         // 10 segundos duracion
  .duty_escaneo = 50,               // duty 50%
  .sf_bw = 0x90FA,                  // sf 9, bw 250
  .num_anclas = 3,                  // posicionamiento 2D
  .media_medidas = 5,               // 
  .anclas_objetivo = 3,             // Medimos las tres anclas
  .tx_power_dbm = 0                 // 0 dBm
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
  .m_flag_primer_paquete = false,
  .m_flag_imu_disparo_periodico = false,
  .m_flag_sector_ack_recibido = false,
  .m_sector_ack_status = 0,
  .m_flag_indication_confirmada = false
};


bool validar_configuracion(const logger_config_t *cfg, config_error_t *error){
  if((cfg->flags & MODULO_AMBIENTE) && (cfg-> periodo_ambiente == 0)){
    *error = CONFIG_ERR_PERIODO_MIN;
    return false;
  }

  if((cfg->flags & MODULO_ANGULO) && (cfg-> periodo_angulo == 0)){
    *error = CONFIG_ERR_PERIODO_MIN;
    return false;
  }

  if((cfg->flags & MODULO_GPS) && (cfg-> periodo_gps == 0)){
    *error = CONFIG_ERR_PERIODO_MIN;
    return false;
  }

  if((cfg->flags & MODULO_RANGING) && (cfg-> periodo_ranging == 0)){
    *error = CONFIG_ERR_PERIODO_MIN;
    return false;
  }

  if((cfg->flags & MODULO_BEACONS) && (cfg-> periodo_beacons == 0)){
    *error = CONFIG_ERR_PERIODO_MIN;
    return false;
  }

  if((cfg->flags & MODULO_LORAWAN) && (cfg-> periodo_envio == 0)){
    *error = CONFIG_ERR_PERIODO_MIN;
    return false;
  }

  if ((cfg->flags & MODULO_BEACONS) && (cfg->duracion_escaneo >= cfg->periodo_beacons)){
    *error = CONFIG_ERR_ESCANEO;
    return false;
  }

  if ((cfg->flags & MODULO_BEACONS) && (cfg->duty_escaneo < 1 || cfg->duty_escaneo > 100)){
    *error = CONFIG_ERR_ESCANEO;
    return false;
  }

  if(cfg->flags & (MODULO_IMU_XL | MODULO_ANGULO | MODULO_IMU_GY)){
    if (cfg -> imu_odr_xl > 11){
      *error = CONFIG_ERR_IMU;
      return false;
    }

    if (cfg -> imu_scale_xl > 3){
      *error = CONFIG_ERR_IMU;
      return false;
    }

    if (cfg -> imu_odr_gy > 10){
      *error = CONFIG_ERR_IMU;
      return false;
    }

    uint8_t v = cfg->imu_scale_gy;
    if (v != 0 && v != 1 && v != 2 && v != 4 && v != 6){
      *error = CONFIG_ERR_IMU;
      return false;
    }

    if (cfg -> imu_fifo_xl_odr > 11){
      *error = CONFIG_ERR_IMU;
      return false;
    }

    if (cfg -> imu_fifo_gy_odr > 11){
      *error = CONFIG_ERR_IMU;
      return false;
    }

    uint8_t m = cfg->imu_fifo_mode;
    if (m != 0 && m != 1 && m != 3 && m != 4 && m != 6 && m != 7){
      *error = CONFIG_ERR_IMU;
      return false;
    }

    if (cfg -> imu_power_mode > 2){
      *error = CONFIG_ERR_IMU;
      return false;
    }
  }
  
  if((cfg->flags & MODULO_IMU_CONTINUO) && !(cfg-> flags & (MODULO_IMU_GY | MODULO_IMU_XL))){
    *error = CONFIG_ERR_IMU;
    return false;
  }

  if((cfg->flags & MODULO_IMU_CONTINUO) && (cfg-> flags & MODULO_IMU_PERIODICO)){
    *error = CONFIG_ERR_IMU;
    return false;
  }

  if((cfg->flags & MODULO_IMU_PERIODICO) && !(cfg-> flags & (MODULO_IMU_GY | MODULO_IMU_XL))){
    *error = CONFIG_ERR_IMU;
    return false;
  }

  if ((cfg->flags & MODULO_IMU_PERIODICO) && cfg->periodo_imu_periodico == 0) {
    *error = CONFIG_ERR_PERIODO_MIN;
    return false;
  }

  if((cfg->flags & (MODULO_IMU_XL | MODULO_IMU_GY)) && (cfg->imu_fifo_muestras == 0 || cfg -> imu_fifo_muestras > 511)){
    *error = CONFIG_ERR_IMU;
    return false;
  }

  if(cfg-> flags & MODULO_RANGING){
    uint8_t sf = (cfg->sf_bw >> 12) & 0x0F;
    uint16_t bw = cfg->sf_bw & 0xFFF;
    
    if(sf < 7 || sf > 12) {
      *error = CONFIG_ERR_RANGING;
      return false;
    } 

    if(bw != 125 && bw != 250 && bw != 500) {
      *error = CONFIG_ERR_RANGING;
      return false;
    } 

    if((cfg-> num_anclas < 1) || (cfg->num_anclas > MAX_ANCLAS)) {
      *error = CONFIG_ERR_RANGING;
      return false;
    }

    if((cfg-> anclas_objetivo < 1) || (cfg->anclas_objetivo > cfg-> num_anclas)) {
      *error = CONFIG_ERR_RANGING;
      return false;
    } 

    if((cfg-> media_medidas < 1) || (cfg->media_medidas > MAX_MEDIDAS_ANCLA)) {
      *error = CONFIG_ERR_RANGING;
      return false;
    } 

    if(cfg->tx_power_dbm < -17 || cfg->tx_power_dbm > 22){
      *error = CONFIG_ERR_RANGING;
      return false;
    }
  }

  *error = CONFIG_OK;
  return true;
}