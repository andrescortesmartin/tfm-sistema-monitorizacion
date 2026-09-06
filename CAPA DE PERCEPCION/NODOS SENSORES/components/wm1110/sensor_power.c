/**
 * @file sensor_power.c
 * @brief Gestión de alimentación y encendido/apagado de los sensores y el LR1110
 */

#include "smtc_hal.h"
#include "sensor_power.h"
#include "main_LPS22DF.h"
#include "main_LSM6DSOX.h"
#include "nrf_drv_gpiote.h"
#include <stdbool.h>

/** @brief Indica si el LSM6DSOX está encendido y en uso */
static bool lsm6dsox_activo = false;
/** @brief Indica si el LPS22DF está encendido y en uso */
static bool lps22df_activo = false;
/** @brief Indica si el LR1110 está encendido */
static bool lr1110_activo = false;

/**
 * @brief Activa o corta la alimentación compartida de los sensores I2C/SPI
 * @details Enciende @c SENSOR_POWER si algún sensor la necesita y estaba apagada
 * (esperando a que se estabilice), o la corta si ningún sensor la necesita
 */
static void actualizar_alimentacion_sensores(void) {
  if (lsm6dsox_activo || lps22df_activo) {  // Al menos uno necesita alimentacion
    if (hal_gpio_get_output_value(SENSOR_POWER) == HAL_GPIO_RESET) {
      hal_gpio_init_out(SENSOR_POWER, HAL_GPIO_SET);
      hal_mcu_wait_ms(100);
    }
  } else {  // Ninguno necesita alimentacion
    hal_gpio_init_out(SENSOR_POWER, HAL_GPIO_RESET);
  }
}

/** @brief Enciende el LSM6DSOX y lo configura */
void lsm6dsox_encender(void) {
  lsm6dsox_activo = true;
  actualizar_alimentacion_sensores();  // Enciendo Vcc si estaba apagado

  hal_gpio_init_out(LSM6DSOX_SPI_CS_PIN, HAL_GPIO_SET);

  if(!configuracion_LSM6DSOX()){  // Inicializo SPI + configuro sensor
    lsm6dsox_apagar();            // Si falla el arranque, apago el sensor
  }
}


/** @brief Apaga el LSM6DSOX y libera sus recursos */
void lsm6dsox_apagar(void) {
  if (!lsm6dsox_activo) return;

  imu_stop(true);  // Paro el sensor (ODR e interrupciones)

  hal_spi_lsm6dsox_deinit();  // Apago el bus

  if (lsm6dsox_int_initialized) {
    nrf_drv_gpiote_in_uninit(LSM6DSOX_INTERRUPT1);
    nrf_drv_gpiote_in_uninit(LSM6DSOX_INTERRUPT2);

    hal_gpio_init_out(LSM6DSOX_INTERRUPT1, HAL_GPIO_RESET);
    hal_gpio_init_out(LSM6DSOX_INTERRUPT2, HAL_GPIO_RESET);

    lsm6dsox_int_initialized = false;
  }

  hal_gpio_init_in(LSM6DSOX_SPI_SCK_PIN, HAL_GPIO_PULL_MODE_DOWN, HAL_GPIO_IRQ_MODE_OFF, NULL);
  hal_gpio_init_in(LSM6DSOX_SPI_MISO_SDO_PIN, HAL_GPIO_PULL_MODE_DOWN, HAL_GPIO_IRQ_MODE_OFF, NULL);
  hal_gpio_init_in(LSM6DSOX_SPI_MOSI_SDI_PIN, HAL_GPIO_PULL_MODE_DOWN, HAL_GPIO_IRQ_MODE_OFF, NULL);
  hal_gpio_init_in(LSM6DSOX_SPI_CS_PIN, HAL_GPIO_PULL_MODE_NONE, HAL_GPIO_IRQ_MODE_OFF, NULL);

  lsm6dsox_activo = false;
  actualizar_alimentacion_sensores();
}

/** @brief Enciende el LPS22DF y lo configura */
void lps22df_encender(void) {
  lps22df_activo = true;
  actualizar_alimentacion_sensores();  // Enciendo Vcc si estaba apagado
  if(!lps22df_config()){               // Inicializo i2c + configuro sensor
    lps22df_apagar();                  // Si falla el arranque, apago el sensor
  }
} 

/** @brief Apaga el LPS22DF y libera sus recursos */
void lps22df_apagar(void) {
  if (!lps22df_activo) return;

  hal_i2c_deinit();  // Apago el bus

  hal_gpio_init_in(LPS22DF_SCL, HAL_GPIO_PULL_MODE_NONE, HAL_GPIO_IRQ_MODE_OFF, NULL);
  hal_gpio_init_in(LPS22DF_SDA, HAL_GPIO_PULL_MODE_NONE, HAL_GPIO_IRQ_MODE_OFF, NULL);

  lps22df_activo = false;
  actualizar_alimentacion_sensores();
}

/** @brief Enciende o apaga cada sensor según la configuración de la aplicación */
void configurar_sensores(void) {
  if (configuracion_app.flags & MODULO_IMU_XL ||
      configuracion_app.flags & MODULO_ANGULO ||
      configuracion_app.flags & MODULO_IMU_GY) {

    if(lsm6dsox_activo) lsm6dsox_apagar(); //Si hay una reconfiguracion, partimos de un estado limpio del sensor si se encontraba activo

    lsm6dsox_encender();
  } else {
    lsm6dsox_apagar();
  }

  if (configuracion_app.flags & MODULO_AMBIENTE) {
    lps22df_encender();
  } else {
    lps22df_apagar();
  }
}

/** @brief Enciende el LR1110 y prepara sus pines para operación normal */
void encender_lr1110(void) {
  if (lr1110_activo) return;
  lr1110_activo = true;

  hal_gpio_init_out(LR1110_POWER, HAL_GPIO_SET);  // Alimentación ON

  // Poner todos los pines en estado bajo antes de inicializar
  hal_gpio_init_out(LR1110_IRQ_PIN, HAL_GPIO_RESET);
  hal_gpio_init_out(LR1110_BUSY_PIN, HAL_GPIO_RESET);
  hal_gpio_init_out(LR1110_SPI_NSS_PIN, HAL_GPIO_RESET);
  hal_gpio_init_out(LR1110_SPI_SCK_PIN, HAL_GPIO_RESET);
  hal_gpio_init_out(LR1110_SPI_MOSI_PIN, HAL_GPIO_RESET);
  hal_gpio_init_out(LR1110_SPI_MISO_PIN, HAL_GPIO_RESET);
  hal_gpio_init_out(LR1110_NRESER_PIN, HAL_GPIO_RESET);

  hal_mcu_wait_ms(500);  // Espera re-arranque del LR1110

  // Configuración final de pines para operación normal
  hal_gpio_init_out(LR1110_SPI_NSS_PIN, HAL_GPIO_SET);
  hal_gpio_init_in(LR1110_BUSY_PIN, HAL_GPIO_PULL_MODE_NONE, HAL_GPIO_IRQ_MODE_OFF, NULL);
  hal_gpio_init_in(LR1110_IRQ_PIN, HAL_GPIO_PULL_MODE_DOWN, HAL_GPIO_IRQ_MODE_RISING, NULL);
  hal_gpio_set_value(LR1110_NRESER_PIN, HAL_GPIO_SET);

  hal_spi_init();  // Inicializar SPI para el LR1110
}

/** @brief Apaga el LR1110 de forma limpia */
void apagar_lr1110(void) {
  if (!lr1110_activo) {
    return;
  }

  lr1110_activo = false;
  hal_spi_deinit();  // Liberar SPI antes de tocar los pines

  nrf_drv_gpiote_in_uninit(LR1110_IRQ_PIN);

  // Poner pines SPI en estado bajo (evita corriente de fuga hacia el chip apagado)
  hal_gpio_init_out(LR1110_SPI_NSS_PIN, HAL_GPIO_RESET);
  hal_gpio_init_out(LR1110_SPI_SCK_PIN, HAL_GPIO_RESET);
  hal_gpio_init_out(LR1110_SPI_MOSI_PIN, HAL_GPIO_RESET);
  hal_gpio_init_out(LR1110_SPI_MISO_PIN, HAL_GPIO_RESET);

  // Poner IRQ y BUSY como salidas en bajo (desactivar pull-ups/IRQs activos)
  hal_gpio_init_out(LR1110_IRQ_PIN, HAL_GPIO_RESET);
  hal_gpio_init_out(LR1110_BUSY_PIN, HAL_GPIO_RESET);

  // Reset del chip antes de cortar alimentación (apagado limpio)
  hal_gpio_set_value(LR1110_NRESER_PIN, HAL_GPIO_RESET);
  hal_mcu_wait_ms(10);

  hal_gpio_init_out(LR1110_POWER, HAL_GPIO_RESET);  // Cortar alimentación
}