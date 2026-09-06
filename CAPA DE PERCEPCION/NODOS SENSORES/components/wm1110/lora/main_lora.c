/**
 * @file main_lora.c
 * @brief Implementación de funciones básicas del transceiver LR11XX
 *
 * Proporciona inicialización del sistema, gestión de sleep y funciones de
 * transmisión para el radio LoRa Semtech LR11XX.
 */

#include "main_lora.h"
#include "apps_utilities.h"
#include "lr11xx_system.h"
#include "lr11xx_system_types.h"
#include "smtc_hal.h"
#include "smtc_shield_lr11xx_common_if.h"

/**
 * @brief Pone el radio LR1110 en modo sleep de bajo consumo
 * @param context Contexto del dispositivo LR11XX
 * @details Configura el reloj LF en modo RC y pone el radio en sleep con warm start habilitado y sin timeout RTC
 */
void lr1110_sleep_enter(const void* context) {
  lr11xx_system_sleep_cfg_t radio_sleep_cfg;

  radio_sleep_cfg.is_warm_start = true;
  radio_sleep_cfg.is_rtc_timeout = false;

  if (lr11xx_system_cfg_lfclk(context, LR11XX_SYSTEM_LFCLK_XTAL, true) != LR11XX_STATUS_OK) {
    HAL_DBG_TRACE_ERROR("Failed to set LF clock\n");
  }
  if (lr11xx_system_set_sleep(context, radio_sleep_cfg, 0) != LR11XX_STATUS_OK) {
    HAL_DBG_TRACE_ERROR("Failed to set the radio to sleep\n");
  }
}

/**
 * @brief Inicializa el sistema LR11XX completamente
 * @param context Contexto del dispositivo LR11XX
 * @details Realiza:
 *          - Reset del chip
 *          - Configuración del modo regulador (LDO/DC-DC)
 *          - Configuración del switch RF
 *          - Configuración del TCXO si está presente
 *          - Configuración del reloj de baja frecuencia
 *          - Calibración de todos los bloques internos
 *          - Limpieza de errores e interrupciones
 */
void lr11xx_system_init(const void* context) {
  ASSERT_SMTC_MODEM_RC(lr11xx_system_reset(context));  // Realiza un reset completo del chip LR11XX

  const lr11xx_system_reg_mode_t regulator = smtc_shield_lr11xx_get_reg_mode();
  ASSERT_SMTC_MODEM_RC(lr11xx_system_set_reg_mode(context, regulator));  // Configura el modo de regulación de energía (LDO o DC-DC)

  const lr11xx_system_rfswitch_cfg_t rf_switch_setup = smtc_shield_lr11xx_get_rf_switch_cfg();
  ASSERT_SMTC_MODEM_RC(lr11xx_system_set_dio_as_rf_switch(context, &rf_switch_setup));  // Configura los pines DIO como controladores de un switch RF

  const smtc_shield_lr11xx_tcxo_cfg_t tcxo_cfg = smtc_shield_lr11xx_get_tcxo_cfg();
  if (tcxo_cfg.has_tcxo == true) {
    ASSERT_SMTC_MODEM_RC(lr11xx_system_set_tcxo_mode(context, tcxo_cfg.supply, tcxo_cfg.startup_time_in_tick));
  }

  const smtc_shield_lr11xx_lf_clck_cfg_t lfclk_cfg = smtc_shield_lr11xx_get_lf_clk_cfg();
  ASSERT_SMTC_MODEM_RC(lr11xx_system_cfg_lfclk(context, lfclk_cfg.lf_clk_cfg, lfclk_cfg.wait_32k_ready));

  // Configura el reloj de baja frecuencia (LFCLK), puede ser cristal externo o RC interno
  ASSERT_SMTC_MODEM_RC(lr11xx_system_clear_errors(context));
  ASSERT_SMTC_MODEM_RC(lr11xx_system_calibrate(context, 0x3F));  // Realiza la calibración de todos los bloques internos

  uint16_t errors;
  ASSERT_SMTC_MODEM_RC(lr11xx_system_get_errors(context, &errors));                           // Se lee el estado de errores
  ASSERT_SMTC_MODEM_RC(lr11xx_system_clear_errors(context));                                  // Limpieza final por seguridad
  ASSERT_SMTC_MODEM_RC(lr11xx_system_clear_irq_status(context, LR11XX_SYSTEM_IRQ_ALL_MASK));  // Borra cualquier interrupción pendiente
}

/**
 * @brief Calcula el LDRO, que indica si se debe habilitar la opción de baja tasa de datos en LoRa
 * @param sf Spreading Factor
 * @param bw Ancho de banda
 * @return LDRO
 */
uint8_t compute_lora_ldro(const lr11xx_radio_lora_sf_t sf, const lr11xx_radio_lora_bw_t bw) {
  // Calcula el LDRO según el ancho de banda y el Spreading Factor
  switch (bw) {
    case LR11XX_RADIO_LORA_BW_500:
      return 0;

    case LR11XX_RADIO_LORA_BW_250:
      if (sf == LR11XX_RADIO_LORA_SF12) {
        return 1;
      } else {
        return 0;
      }

    case LR11XX_RADIO_LORA_BW_800:
    case LR11XX_RADIO_LORA_BW_400:
    case LR11XX_RADIO_LORA_BW_200:
    case LR11XX_RADIO_LORA_BW_125:
      if ((sf == LR11XX_RADIO_LORA_SF12) || (sf == LR11XX_RADIO_LORA_SF11)) {
        return 1;
      } else {
        return 0;
      }

    case LR11XX_RADIO_LORA_BW_62:
      if ((sf == LR11XX_RADIO_LORA_SF12) || (sf == LR11XX_RADIO_LORA_SF11) ||
          (sf == LR11XX_RADIO_LORA_SF10)) {
        return 1;
      } else {
        return 0;
      }

    case LR11XX_RADIO_LORA_BW_41:
      if ((sf == LR11XX_RADIO_LORA_SF12) || (sf == LR11XX_RADIO_LORA_SF11) ||
          (sf == LR11XX_RADIO_LORA_SF10) || (sf == LR11XX_RADIO_LORA_SF9)) {
        return 1;
      } else {
        return 0;
      }

    case LR11XX_RADIO_LORA_BW_31:
    case LR11XX_RADIO_LORA_BW_20:
    case LR11XX_RADIO_LORA_BW_15:
    case LR11XX_RADIO_LORA_BW_10:
      // case LR11XX_RADIO_LORA_BW_7:
      return 1;
    default:
      return 0;
  }
}
