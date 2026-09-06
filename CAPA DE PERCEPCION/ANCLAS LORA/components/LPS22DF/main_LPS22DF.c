/**
 * @file main_LPS22DF.c
 * @brief Implementación del driver para el sensor de presión y temperatura
 * LPS22DF
 *
 * Proporciona funciones para configurar y leer el sensor LPS22DF mediante
 * comunicación I2C. Opera en modo one-shot para minimizar el consumo de
 * energía.
 */

#include "main_LPS22DF.h"
#include "lps22df_reg.h"
#include "main_lorawan.h"
#include "smtc_hal.h"

/** @brief Contexto del dispositivo para comunicación I2C */
static stmdev_ctx_t dev_ctx;

/** @brief Configuración del modo de bus (interfaz y filtro) */
lps22df_bus_mode_t bus_mode;

/** @brief Estado del sensor (boot, reset, etc.) */
lps22df_stat_t status;

/** @brief Identificación del dispositivo (WHO_AM_I) */
lps22df_id_t id;

/** @brief Configuración de modo (ODR, promediado, filtro) */
lps22df_md_t md;

/**
 * @brief Función de escritura I2C para el sensor LPS22DF
 * @param handle Manejador del dispositivo (dirección I2C)
 * @param reg Dirección del registro a escribir
 * @param bufp Puntero al buffer de datos a escribir
 * @param len Número de bytes a escribir
 * @return 0 en caso de éxito, -1 en caso de error
 */
static int32_t sensor_write(void *handle, uint8_t reg, const uint8_t *bufp, uint16_t len) {
  uint8_t i2c_addr = (uint8_t)(uintptr_t)handle;

  if (hal_i2c_write_reg(i2c_addr, reg, (uint8_t *)bufp, len)) {
    return 0; // Éxito
  }
  return -1; // Error
}

/**
 * @brief Función de lectura I2C para el sensor LPS22DF
 * @param handle Manejador del dispositivo (dirección I2C)
 * @param reg Dirección del registro a leer
 * @param bufp Puntero al buffer donde almacenar los datos leídos
 * @param len Número de bytes a leer
 * @return 0 en caso de éxito, -1 en caso de error
 */
static int32_t sensor_read(void *handle, uint8_t reg, uint8_t *bufp, uint16_t len) {
  uint8_t i2c_addr = (uint8_t)(uintptr_t)handle;

  if (hal_i2c_read_reg(i2c_addr, reg, bufp, len)) {
    return 0; // Éxito
  }
  return -1; // Error
}

/**
 * @brief Configura e inicializa el sensor LPS22DF
 * @details Realiza las siguientes operaciones:
 *          - Inicializa el contexto del dispositivo con funciones de I2C
 *          - Verifica el ID del dispositivo (WHO_AM_I)
 *          - Realiza reset del sensor y espera a que complete
 *          - Configura Block Data Update e incremento automático
 *          - Configura modo de bus (filtro automático, selección por HW)
 *          - Establece modo one-shot con promediado de 4 muestras y filtro
 * ODR/4
 */
void lps22df_config(void) {
  /* Initialize mems driver interface */
  dev_ctx.write_reg = sensor_write;
  dev_ctx.read_reg = sensor_read;
  dev_ctx.handle = (void *)(uintptr_t)LPS22DF_ADDR;

  hal_i2c_init();

  /* Check device ID */
  lps22df_id_get(&dev_ctx, &id);
  if (id.whoami != LPS22DF_ID){
    while (1);
  }
  /* Restore default configuration */
  lps22df_init_set(&dev_ctx, LPS22DF_RESET);
  do {
    lps22df_status_get(&dev_ctx, &status);
  } while (status.sw_reset);

  /* Set bdu and if_inc recommended for driver usage */
  lps22df_init_set(&dev_ctx, LPS22DF_DRV_RDY);

  /* Select bus interface */
  bus_mode.filter = LPS22DF_FILTER_AUTO;
  bus_mode.interface = LPS22DF_SEL_BY_HW;
  lps22df_bus_mode_set(&dev_ctx, &bus_mode);

  /* Set Output Data Rate */
  md.odr = LPS22DF_ONE_SHOT;
  md.avg = LPS22DF_4_AVG;
  md.lpf = LPS22DF_LPF_ODR_DIV_4;
  lps22df_mode_set(&dev_ctx, &md);

  hal_i2c_deinit();
}

/**
 * @brief Mide presión y temperatura ambiente
 * @details Dispara una medición one-shot, lee los datos de presión (en hPa) y
 *          temperatura (en °C), convierte a tipos enteros (int16_t para
 *          presión, int8_t para temperatura) y los añade al payload de transmisión
 */
void medir_ambiente(void) {
  static lps22df_data_t data;
  
  hal_i2c_init();

  lps22df_trigger_sw(&dev_ctx, &md);
  hal_mcu_wait_ms(10);
  lps22df_data_get(&dev_ctx, &data);

  ultimas_medidas.presion = (int16_t)data.pressure.hpa;
  ultimas_medidas.temperatura = (int8_t)(data.heat.deg_c);
  
  ultimas_medidas.ts_ambiente = get_timestamp();

  hal_i2c_deinit();
}