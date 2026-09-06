/**
 * @file main_LSM6DSOX.c
 * @brief Implementación del driver para el sensor inercial LSM6DSOX
 *
 * Proporciona funciones para configurar, leer y gestionar el sensor LSM6DSOX
 * mediante comunicación SPI. Incluye soporte para FIFO, detección de actividad/
 * inactividad, lectura de datos inerciales y cálculo de ángulos de orientación.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "lsm6dsox_reg.h"
#include "main_LSM6DSOX.h"
#include "main_lorawan.h"
#include "nrf_drv_gpiote.h"
#include "smtc_hal.h"

/** @brief Buffer para datos brutos del acelerómetro */
static axis3bit16_t data_raw_acceleration;

/** @brief Buffer para datos brutos del giroscopio */
static axis3bit16_t data_raw_angular_rate;

/** @brief Bandera de estado de inicialización del sensor */
static bool lsm6dsox_is_initialized = false;

/** @brief Bandera de estado de inicialización de la interrupción */
bool lsm6dsox_int_initialized = false;


/**
 * @brief Función de escritura SPI para el sensor LSM6DSOX
 * @param handle Manejador del dispositivo (no utilizado en SPI)
 * @param reg Dirección del registro a escribir
 * @param bufp Puntero al buffer de datos a escribir
 * @param len Número de bytes a escribir
 * @return 0 en caso de éxito, -1 en caso de error
 */
static int32_t sensor_write(void *handle, uint8_t reg, const uint8_t *bufp, uint16_t len) {
  (void)handle; // no se usa en SPI

  uint8_t tx_buf[1 + len];
  tx_buf[0] = reg & 0x7F; // bit7=0 -> escritura
  memcpy(&tx_buf[1], bufp, len);

  hal_gpio_init_out(LSM6DSOX_SPI_CS_PIN, HAL_GPIO_RESET);
  hal_spi_lsm6dsox_write(tx_buf, sizeof(tx_buf));
  hal_gpio_init_out(LSM6DSOX_SPI_CS_PIN, HAL_GPIO_SET);

  return 0; // éxito
}

/**
 * @brief Función de lectura SPI para el sensor LSM6DSOX
 * @param handle Manejador del dispositivo (no utilizado en SPI)
 * @param reg Dirección del registro a leer
 * @param bufp Puntero al buffer donde almacenar los datos leídos
 * @param len Número de bytes a leer
 * @return 0 en caso de éxito, -1 en caso de error
 */
static int32_t sensor_read(void *handle, uint8_t reg, uint8_t *bufp, uint16_t len) {
  (void)handle; // no se usa en SPI

  reg |= 0x80;
  hal_gpio_init_out(LSM6DSOX_SPI_CS_PIN, HAL_GPIO_RESET);
  hal_spi_lsm6dsox_write(&reg, 1);
  hal_spi_lsm6dsox_read(bufp, len, 0x00);
  hal_gpio_init_out(LSM6DSOX_SPI_CS_PIN, HAL_GPIO_SET);

  return 0; // éxito
}

/** @brief Contexto del dispositivo LSM6DSOX con funciones de lectura/escritura
 */
stmdev_ctx_t dev_ctx_lsm6dsox = {
    .write_reg = sensor_write,
    .read_reg = sensor_read,
};

/**
 * @brief Manejador de interrupción GPIO para el sensor LSM6DSOX
 * @param pin Pin GPIO que generó la interrupción
 * @param action Tipo de transición que generó la interrupción
 */
void gpio_handler_movimiento(nrf_drv_gpiote_pin_t pin, nrf_gpiote_polarity_t action) {
  flags_globales.m_flag_movimiento = true;
}

void gpio_handler_watermark(nrf_drv_gpiote_pin_t pin, nrf_gpiote_polarity_t action) {
  flags_globales.m_flag_watermark = true;
}

/**
 * @brief Inicializa la interrupción de movimiento del sensor
 * @details Configura el GPIO para detectar interrupciones de borde ascendente
 *          en el pin LSM6DSOX_INTERRUPT1 sin resistencia de pull
 */
void init_interrupt_mov(void) {
  if (lsm6dsox_int_initialized) return;
  nrf_drv_gpiote_init();
  nrf_drv_gpiote_in_config_t in_config = GPIOTE_CONFIG_IN_SENSE_LOTOHI(false);
  in_config.pull = NRF_GPIO_PIN_NOPULL;

  nrf_drv_gpiote_in_init(LSM6DSOX_INTERRUPT1, &in_config, gpio_handler_watermark);
  nrf_drv_gpiote_in_event_enable(LSM6DSOX_INTERRUPT1, true);

  nrf_drv_gpiote_in_init(LSM6DSOX_INTERRUPT2, &in_config, gpio_handler_movimiento);
  nrf_drv_gpiote_in_event_enable(LSM6DSOX_INTERRUPT2, true);

  lsm6dsox_int_initialized = true;
}

/**
 * @brief Configura e inicializa el sensor LSM6DSOX
 * @details Realiza las siguientes operaciones:
 *          - Inicializa interrupciones GPIO y comunicación SPI
 *          - Verifica el ID del dispositivo
 *          - Realiza reset del sensor
 *          - Deshabilita I3C y habilita Block Data Update
 *          - Configura escalas completas para acelerómetro y giroscopio
 *          - Configura FIFO (watermark, modo, batch)
 *          - Configura detección de actividad/inactividad
 *          - Configura interrupciones en INT1
 *          - Establece tasas de muestreo (ODR) y modos de potencia
 * @note Esta función combina configuración e inicialización
 */
void configuracion_LSM6DSOX(void) {
  lsm6dsox_pin_int1_route_t int1_route;
  lsm6dsox_pin_int2_route_t int2_route;
  static uint8_t whoamI, rst;

  hal_spi_lsm6dsox_init();

  /* Check device ID */
  lsm6dsox_device_id_get(&dev_ctx_lsm6dsox, &whoamI);

  if (whoamI != LSM6DSOX_ID) {
    while (1);
  }

  /* Restore default configuration */
  lsm6dsox_reset_set(&dev_ctx_lsm6dsox, PROPERTY_ENABLE);

  do {
    lsm6dsox_reset_get(&dev_ctx_lsm6dsox, &rst);
  } while (rst);

  /* Disable I3C interface */
  lsm6dsox_i3c_disable_set(&dev_ctx_lsm6dsox, LSM6DSOX_I3C_DISABLE);
  /* Enable Block Data Update */
  lsm6dsox_block_data_update_set(&dev_ctx_lsm6dsox, PROPERTY_ENABLE);
  /* Set XL full scale */
  lsm6dsox_xl_full_scale_set(&dev_ctx_lsm6dsox, configuracion_app.imu_scale_xl);
  /* Set Output XL Data Rate */
  lsm6dsox_xl_data_rate_set(&dev_ctx_lsm6dsox, configuracion_app.imu_odr_xl);
  lsm6dsox_xl_power_mode_set(&dev_ctx_lsm6dsox, configuracion_app.imu_power_mode);

  if (configuracion_app.flags & MODULO_IMU_XL || configuracion_app.flags & MODULO_IMU_GY) {
    init_interrupt_mov();

    lsm6dsox_fifo_watermark_set(&dev_ctx_lsm6dsox, configuracion_app.imu_fifo_muestras);
    lsm6dsox_fifo_xl_batch_set(&dev_ctx_lsm6dsox, LSM6DSOX_XL_NOT_BATCHED);
    lsm6dsox_fifo_gy_batch_set(&dev_ctx_lsm6dsox, LSM6DSOX_GY_NOT_BATCHED);
    lsm6dsox_fifo_mode_set(&dev_ctx_lsm6dsox, configuracion_app.imu_fifo_mode);

    lsm6dsox_wkup_dur_set(&dev_ctx_lsm6dsox, configuracion_app.imu_umbral_actividad);
    lsm6dsox_act_sleep_dur_set(&dev_ctx_lsm6dsox, configuracion_app.imu_duracion_inactividad);
    lsm6dsox_wkup_threshold_set(&dev_ctx_lsm6dsox, 0x01);
    lsm6dsox_act_mode_set(&dev_ctx_lsm6dsox, LSM6DSOX_XL_AND_GY_NOT_AFFECTED);

    lsm6dsox_data_ready_mode_set(&dev_ctx_lsm6dsox, LSM6DSOX_DRDY_LATCHED);
  
    // Pin 1 de interrupcion, dedicado a las interrupciones de watermark
    lsm6dsox_pin_int1_route_get(&dev_ctx_lsm6dsox, &int1_route);
    int1_route.fifo_th = PROPERTY_ENABLE;
    lsm6dsox_pin_int1_route_set(&dev_ctx_lsm6dsox, int1_route);

    // Pin 2 de interrupcion, dedicado a las interrupciones de movimiento
    lsm6dsox_pin_int2_route_get(&dev_ctx_lsm6dsox, NULL, &int2_route);
    int2_route.sleep_change = PROPERTY_ENABLE;
    int2_route.wake_up = PROPERTY_ENABLE;
    lsm6dsox_pin_int2_route_set(&dev_ctx_lsm6dsox, NULL, int2_route);

    if (configuracion_app.flags & MODULO_IMU_GY) {
      lsm6dsox_gy_full_scale_set(&dev_ctx_lsm6dsox, configuracion_app.imu_scale_gy);
      lsm6dsox_gy_data_rate_set(&dev_ctx_lsm6dsox, configuracion_app.imu_odr_gy);
      lsm6dsox_gy_power_mode_set(&dev_ctx_lsm6dsox, LSM6DSOX_GY_NORMAL);

    } else {
      lsm6dsox_gy_data_rate_set(&dev_ctx_lsm6dsox, LSM6DSOX_GY_ODR_OFF);
    }
  }
}

/**
 * @brief Lee y procesa los datos almacenados en la FIFO del sensor
 * @details Lee el número de muestras en la FIFO, extrae cada muestra según su
 * tipo (acelerómetro o giroscopio), las almacena en un buffer temporal y
 * finalmente guarda los datos en flash con un timestamp
 */
void lectura(void) {
  uint16_t num = 0;
  lsm6dsox_fifo_tag_t reg_tag;
  axis3bit16_t dummy;
  uint16_t cursor_buffer = 0;

  static uint8_t buffer_temp_lectura[IMU_BUFFER_MAX_BYTES];

  /* Read number of samples in FIFO */
  lsm6dsox_fifo_data_level_get(&dev_ctx_lsm6dsox, &num);

  if (num == 0)
    return;
  
  while (num--) {
    /* Read FIFO tag */
    lsm6dsox_fifo_sensor_tag_get(&dev_ctx_lsm6dsox, &reg_tag);
    switch (reg_tag) {
    case LSM6DSOX_XL_NC_TAG:
      lsm6dsox_fifo_out_raw_get(&dev_ctx_lsm6dsox, &buffer_temp_lectura[cursor_buffer]);
      cursor_buffer += 6;
      break;
    case LSM6DSOX_GYRO_NC_TAG:
      lsm6dsox_fifo_out_raw_get(&dev_ctx_lsm6dsox, &buffer_temp_lectura[cursor_buffer]);
      cursor_buffer += 6;
      break;
    default:
      /* Flush unused samples */
      memset(dummy.u8bit, 0x00, 3 * sizeof(int16_t));
      lsm6dsox_fifo_out_raw_get(&dev_ctx_lsm6dsox, dummy.u8bit);
      break;
    }

    if (cursor_buffer >= (IMU_BUFFER_MAX_BYTES - 6)) break;
  }

  uint32_t timestamp_referencia = get_timestamp();
  if (cursor_buffer > 0) {
    flash_log_imu(buffer_temp_lectura, cursor_buffer, timestamp_referencia);
  }
}

/**
 * @brief Gestiona las interrupciones del sensor LSM6DSOX
 * @details Lee todas las fuentes de interrupción y ejecuta las acciones
 * apropiadas:
 *          - Wake-up: activa el batch de la FIFO según configuración (XL y/o GY)
 *          - FIFO threshold: descarga los datos de la FIFO
 *          - Sleep state: descarga datos remanentes y desactiva batch de FIFO
 */
void gestion_interrupcion_lsm6dsox(void) {
  // Guardamos y limpiamos banderas, para evitar perder eventos por si saltan varias interrupciones seguidas
  bool hay_movimiento = flags_globales.m_flag_movimiento;
  bool hay_watermark  = flags_globales.m_flag_watermark;
  flags_globales.m_flag_movimiento = false;
  flags_globales.m_flag_watermark  = false;
  lsm6dsox_pin_int2_route_t int2;


  // Leemos el origen de la interrupcion
  lsm6dsox_all_sources_t all_source;
  lsm6dsox_all_sources_get(&dev_ctx_lsm6dsox, &all_source);
  
  if(hay_movimiento){
    if (all_source.wake_up) {
      flags_globales.m_flag_inactividad = false;  // Por si llegan varios eventos seguidos de wake up y hay un falso sleep
      ultimas_medidas.actividad_eventos++;
      // Activamos la FIFO segun lo que queramos muestrear
      if (configuracion_app.flags & MODULO_IMU_XL) {
        lsm6dsox_fifo_xl_batch_set(&dev_ctx_lsm6dsox, configuracion_app.imu_fifo_xl_odr);
      }

      if (configuracion_app.flags & MODULO_IMU_GY) {
        lsm6dsox_fifo_gy_batch_set(&dev_ctx_lsm6dsox, configuracion_app.imu_fifo_gy_odr);
      }
      
      // Desactivo la interrupcioon de actividad para evitar picos innecesarios
      lsm6dsox_pin_int2_route_get(&dev_ctx_lsm6dsox, NULL, &int2); 
      int2.wake_up = PROPERTY_DISABLE;
      lsm6dsox_pin_int2_route_set(&dev_ctx_lsm6dsox, NULL, int2);
    }else if (all_source.sleep_state) { // Cuando detectamos inactividad, marcamos y esperamos a desactivar muestreo a que se llene la fifo (para tener paquetes de misma duracion)
      flags_globales.m_flag_inactividad = true;
    }
  }

  if (all_source.fifo_th || hay_watermark) {
    lectura(); // Descargamos y guardamos la FIFO

    if (flags_globales.m_flag_inactividad) {
      lsm6dsox_fifo_xl_batch_set(&dev_ctx_lsm6dsox, LSM6DSOX_XL_NOT_BATCHED);
      lsm6dsox_fifo_gy_batch_set(&dev_ctx_lsm6dsox, LSM6DSOX_GY_NOT_BATCHED);

      lsm6dsox_pin_int2_route_get(&dev_ctx_lsm6dsox, NULL, &int2);
      int2.wake_up = PROPERTY_ENABLE;
      lsm6dsox_pin_int2_route_set(&dev_ctx_lsm6dsox, NULL, int2);

      flags_globales.m_flag_inactividad = false;
    }
  }
}

/**
 * @brief Mide y calcula los ángulos de orientación pitch y roll
 * @details Lee los datos del acelerómetro, calcula pitch y roll usando
 * funciones trigonométricas (atan2) y añade los valores al payload de
 * transmisión. Los ángulos se escalan por 100 para mantener precisión decimal
 */
void medir_angulo(void) {

  int16_t data_raw_acceleration[3];

  memset(data_raw_acceleration, 0x00, 3 * sizeof(int16_t));
  lsm6dsox_acceleration_raw_get(&dev_ctx_lsm6dsox, data_raw_acceleration);

  float x = (float)data_raw_acceleration[0];
  float y = (float)data_raw_acceleration[1];
  float z = (float)data_raw_acceleration[2];

  float pitch_f = atan2(x, sqrt(y * y + z * z)) * 180.0f / M_PI;
  float roll_f = atan2(y, sqrt(x * x + z * z)) * 180.0f / M_PI;

  ultimas_medidas.pitch = (int16_t)(pitch_f * 10.0f);
  ultimas_medidas.roll = (int16_t)(roll_f * 10.0f);
  ultimas_medidas.ts_angulo = get_timestamp();
}

/**
 * @brief Controla el encendido/apagado del sensor IMU
 * @param stop true para detener el sensor, false para reactivarlo
 * @details Si stop es true:
 *          - Deshabilita interrupciones GPIO
 *          - Desactiva ODR del acelerómetro y giroscopio
 *          Si stop es false:
 *          - Reactiva ODR según configuración de la aplicación
 *          - Habilita interrupciones GPIO
 */
void imu_stop(bool stop) {
  if (stop) {
    if (lsm6dsox_int_initialized) {
      nrf_drv_gpiote_in_event_disable(LSM6DSOX_INTERRUPT1);
      nrf_drv_gpiote_in_event_disable(LSM6DSOX_INTERRUPT2);
    }

    lsm6dsox_xl_data_rate_set(&dev_ctx_lsm6dsox, LSM6DSOX_XL_ODR_OFF);
    lsm6dsox_gy_data_rate_set(&dev_ctx_lsm6dsox, LSM6DSOX_GY_ODR_OFF);
  } else {
    if ((configuracion_app.flags & MODULO_IMU_XL) ||
        (configuracion_app.flags & MODULO_ANGULO) ||
        (configuracion_app.flags & MODULO_IMU_GY)) {
      lsm6dsox_xl_data_rate_set(&dev_ctx_lsm6dsox, configuracion_app.imu_odr_xl);
    }

    if (configuracion_app.flags & MODULO_IMU_GY) {
      lsm6dsox_gy_data_rate_set(&dev_ctx_lsm6dsox, configuracion_app.imu_odr_gy);
    }
    
    if (configuracion_app.flags & MODULO_IMU_XL || configuracion_app.flags & MODULO_IMU_GY) {
      lsm6dsox_fifo_mode_set(&dev_ctx_lsm6dsox, LSM6DSOX_BYPASS_MODE);
      lsm6dsox_fifo_mode_set(&dev_ctx_lsm6dsox, configuracion_app.imu_fifo_mode);
      if (lsm6dsox_int_initialized) {
        nrf_drv_gpiote_in_event_enable(LSM6DSOX_INTERRUPT1, true);
        nrf_drv_gpiote_in_event_enable(LSM6DSOX_INTERRUPT2,true);
      }
    }
  }
}