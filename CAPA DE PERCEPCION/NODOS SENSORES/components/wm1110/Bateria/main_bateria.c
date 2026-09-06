/**
 * @file main_bateria.c
 * @brief Implementación del driver de medición de batería
 *
 * Utiliza el ADC del nRF52 para medir el voltaje de la batería y lo convierte
 * a milivoltios para transmisión vía LoRaWAN.
 */

#include "main_bateria.h"
#include "main_lorawan.h"
#include "nrf_drv_saadc.h"
#include "smtc_hal.h"

/** @brief Valor leído del ADC */
nrf_saadc_value_t saadc_val = 0;

/**
 * @brief Callback de interrupción del ADC
 * @param p_event Evento del ADC
 * @details Handler vacío, la conversión se realiza de forma síncrona
 */
void ADC_interrupt(nrfx_saadc_evt_t const *p_event) {}

/**
 * @brief Mide el nivel de batería y añade el dato al payload
 * @details Configura el ADC del nRF52 en modo single-ended con ganancia 1/6,
 *          mide el voltaje en el pin AIN5, aplica la fórmula de conversión
 *          considerando el divisor de tensión (5.7:1), y añade el resultado
 *          en milivoltios al payload de transmisión LoRaWAN
 */
void medir_bateria(void) {

  nrf_saadc_channel_config_t channel_config = {
      .resistor_p = NRF_SAADC_RESISTOR_DISABLED,
      .resistor_n = NRF_SAADC_RESISTOR_DISABLED,
      .gain = NRF_SAADC_GAIN1_6,
      .reference = NRF_SAADC_REFERENCE_INTERNAL,
      .acq_time = NRF_SAADC_ACQTIME_40US,
      .mode = NRF_SAADC_MODE_SINGLE_ENDED,
      .burst = NRF_SAADC_BURST_DISABLED,
      .pin_p = NRF_SAADC_INPUT_AIN5,
      .pin_n = NRF_SAADC_INPUT_DISABLED};

  hal_gpio_init_out(VBAT_CTRL, HAL_GPIO_RESET);
  
  hal_mcu_wait_ms(2);

  nrf_drv_saadc_init(NULL, ADC_interrupt);
  nrf_drv_saadc_channel_init(0, &channel_config);

  int32_t suma_saadc = 0;
  nrf_saadc_value_t saadc_val;
  const uint8_t numero_promedio = 8;

  for (uint8_t i = 0; i < numero_promedio; i++) {
      nrf_drv_saadc_sample_convert(0, &saadc_val);
      suma_saadc += saadc_val;
  }
  
  uint32_t bateria_prom = suma_saadc/numero_promedio;

  uint32_t bateria = (uint32_t)(bateria_prom * 0.00087890625 * 5.7 * 1000); // Vbat = SAADC_VAL * 5.7 * 6 * 0.8 * 2**-12

  ultimas_medidas.bateria_mv = (uint16_t)bateria; // Convertir a 2 bytes
  ultimas_medidas.ts_bateria = get_timestamp();

  nrfx_saadc_uninit();

  hal_gpio_init_in(VBAT_CTRL, HAL_GPIO_PULL_MODE_NONE, HAL_GPIO_IRQ_MODE_OFF, NULL);
}