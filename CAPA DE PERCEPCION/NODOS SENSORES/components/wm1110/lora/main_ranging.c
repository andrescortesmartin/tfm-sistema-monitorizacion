/*!
 * @file      main_ranging.c
 *
 * @brief     Ranging implementation for LR1110
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

#include "main_ranging.h"
#include "apps_configuration.h"
#include "apps_utilities.h"
#include "lr11xx_hal_context.h"
#include "lr11xx_radio.h"
#include "lr11xx_rttof.h"
#include "lr11xx_rttof_types.h"
#include "lr11xx_system.h"
#include "lr11xx_system_types.h"
#include "main_lora.h"
#include "main_lorawan.h"
#include "modem_pinout.h"
#include "smtc_hal.h"
#include "smtc_shield_lr11xx_common_if.h"

/** @brief Parámetros de modulación LoRa */
static lr11xx_radio_mod_params_lora_t lora_mod_params = {
  .sf = LORA_SPREADING_FACTOR,
  .bw = LORA_BANDWIDTH,
  .cr = LORA_CODING_RATE,
  .ldro = 0
};

/** @brief Parámetros de paquete LoRa */
static const lr11xx_radio_pkt_params_lora_t lora_pkt_params = {
  .preamble_len_in_symb = LORA_PREAMBLE_LENGTH,
  .header_type = LORA_PKT_LEN_MODE,
  .pld_len_in_bytes = PAYLOAD_LENGTH,
  .crc = LORA_CRC,
  .iq = LORA_IQ,
};

/** @brief Contador de fallos en las mediciones */
uint32_t contador_fallos = 0;

/* Estado acumulativo por ancla */
/** @brief Distancias en bruto (raw) acumuladas para el ancla en curso, en metros */
static int32_t muestras_anclas[MAX_MEDIDAS_ANCLA];
/** @brief RSSI en bruto (raw) acumulados para el ancla en curso, en dBm */
static int32_t muestras_rssi[MAX_MEDIDAS_ANCLA];
/** @brief Número de medidas válidas acumuladas para el ancla en curso */
static uint8_t n_muestras;
/** @brief Indica si el ancla en curso ha alcanzado @c media_medidas y su sesión está completa */
static bool ancla_completada;

/**
 * @brief Inicializa el LR11xx para RTToF
 * @param context Contexto del LR11xx
 */
static void rttof_init(const void* context) {
  // Obtiene la configuración de potencia del PA
  const smtc_shield_lr11xx_pa_pwr_cfg_t* pa_pwr_cfg = smtc_shield_lr11xx_get_pa_pwr_cfg(RF_FREQ_IN_HZ, configuracion_app.tx_power_dbm);

  // Comprueba que la configuración sea valida
  if (pa_pwr_cfg == NULL) {
    HAL_PERF_TEST_TRACE_PRINTF("ERROR: Invalid target frequency or power level\r\n");
    HAL_DBG_TRACE_INFO("ERROR: Invalid target frequency or power level\r\n");
    while (true);
  }

  // Configura el tipo de paquete
  lr11xx_radio_set_pkt_type(context, LR11XX_RADIO_PKT_TYPE_RTTOF);
  // Configura la frecuencia de RF
  lr11xx_radio_set_rf_freq(context, RF_FREQ_IN_HZ);
  // Configura la calibración de RSSI
  lr11xx_radio_set_rssi_calibration(context, smtc_shield_lr11xx_get_rssi_calibration_table(RF_FREQ_IN_HZ));
  // Configura la potencia del PA
  lr11xx_radio_set_pa_cfg(context, &(pa_pwr_cfg->pa_config));
  // Configura los parámetros de transmisión
  lr11xx_radio_set_tx_params(context, pa_pwr_cfg->power, PA_RAMP_TIME);
  // Configura el modo de fallback de RX/TX
  lr11xx_radio_set_rx_tx_fallback_mode(context, FALLBACK_MODE);
  // Configura el modo boost de RX
  lr11xx_radio_cfg_rx_boosted(context, ENABLE_RX_BOOST_MODE);

  // Calcula el LDRO
  lora_mod_params.ldro = compute_lora_ldro(lora_mod_params.sf, lora_mod_params.bw);
  // Configura los parámetros de modulación LoRa
  lr11xx_radio_set_lora_mod_params(context, &lora_mod_params);
  // Configura los parámetros de paquete LoRa
  lr11xx_radio_set_lora_pkt_params(context, &lora_pkt_params);
  // Configura la palabra de sincronización LoRa
  lr11xx_radio_set_lora_sync_word(context, LORA_SYNCWORD);

  uint32_t rttof_rx_tx_delay = 0u;
  // Obtiene el delay de RX/TX
  if (smtc_shield_lr11x0_common_rttof_recommended_rx_tx_delay_indicator(RF_FREQ_IN_HZ, lora_mod_params.bw, lora_mod_params.sf, &rttof_rx_tx_delay)) {
    // Configura el delay de RX/TX
    lr11xx_rttof_set_rx_tx_delay_indicator(context, rttof_rx_tx_delay);
  } else {
    HAL_DBG_TRACE_ERROR("Failed to get RTToF delay indicator\n");
  }
}

/**
 * @brief Obtiene el resultado del RTToF
 * @param context Contexto del LR11xx
 * @param rttof_bw Ancho de banda del RTToF
 * @param result Resultado del RTToF
 * @return Estado del LR11xx
 */
static lr11xx_status_t get_rttof_result(const void* context, lr11xx_radio_lora_bw_t rttof_bw, rttof_result_t* result) {
  lr11xx_status_t rc;
  uint8_t buf[LR11XX_RTTOF_RESULT_LENGTH];

  // Obtiene la distancia
  rc = lr11xx_rttof_get_raw_result(context, LR11XX_RTTOF_RESULT_TYPE_RAW, buf);
  if (rc != LR11XX_STATUS_OK) {
    return rc;
  }
  result->distance_m = lr11xx_rttof_distance_raw_to_meter(rttof_bw, buf);

  // Obtiene el RSSI
  rc = lr11xx_rttof_get_raw_result(context, LR11XX_RTTOF_RESULT_TYPE_RSSI, buf);
  if (rc != LR11XX_STATUS_OK) {
    return rc;
  }
  result->rssi = lr11xx_rttof_rssi_raw_to_value(buf);

  return rc;
}

/**
 * @brief Calcula la mediana de las distancias y el RSSI asociado, para dar robustez frente a outliers
 * @param dist Array de distancias muestreadas; se reordena in-place al calcular la mediana
 * @param rssi Array de RSSI muestreados, alineado con @p dist; se reordena in-place junto a él
 * @param n Número de muestras válidas en @p dist / @p rssi
 * @param out_dist Puntero donde se escribe la distancia mediana (0 si @p n es 0)
 * @param out_rssi Puntero donde se escribe el RSSI correspondiente a la mediana (0 si @p n es 0)
 */
static void mediana_medida (int32_t *dist, int32_t *rssi, uint8_t n, int32_t *out_dist, int32_t *out_rssi){
  if (n == 0){
   *out_dist = 0;
   *out_rssi = 0;
   return; 
  } // Si no hay medidas, devolver 0
  
  // Ordenar las medidas
  for(uint8_t i = 1; i<n; i++){
    int32_t key_dist = dist[i];
    int32_t key_rssi = rssi[i];
    int8_t j = i-1;

    while(j >= 0 && dist[j] > key_dist){
      dist[j+1] = dist[j];
      rssi[j+1] = rssi[j];
      j--;
    }
    dist[j+1] = key_dist;
    rssi[j+1] = key_rssi;
  }
  
  if(n & 1){
    *out_dist = dist[n/2];
    *out_rssi = rssi[n/2];
  }else{
    *out_dist = (dist[n/2-1] + dist[n/2])/2;
    *out_rssi = (rssi[n/2-1] + rssi[n/2])/2;
  }
}

/**
 * @brief Procesa las interrupciones del sistema
 * @param context Contexto del sistema
 * @param irq_filter_mask Máscara de filtros de interrupción
 */
void rttof_irq_process(const void* context, lr11xx_system_irq_mask_t irq_filter_mask) {
  lr11xx_system_irq_mask_t irq_regs;
  lr11xx_system_get_and_clear_irq_status(context, &irq_regs);

  irq_regs &= irq_filter_mask;

  // Si se ha completado un intercambio RTToF
  if ((irq_regs & LR11XX_SYSTEM_IRQ_RTTOF_EXCH_VALID) == LR11XX_SYSTEM_IRQ_RTTOF_EXCH_VALID) {
    rttof_result_t result = {0};
    if(get_rttof_result(context, lora_mod_params.bw, &result) == LR11XX_STATUS_OK){
      if(n_muestras < MAX_MEDIDAS_ANCLA) {
        muestras_anclas[n_muestras] = result.distance_m;
        muestras_rssi[n_muestras] = result.rssi;
        n_muestras++;
      }
      contador_fallos = 0;

      if (n_muestras < configuracion_app.media_medidas) {
        // Rearmar intercambio para la siguiente medida
        lr11xx_radio_set_tx(context, MANAGER_TX_RX_TIMEOUT_MS);
      }else {
        ancla_completada = true;  // Sesión completa, ranging() romperá el bucle
      }
    } else {
      // Rearmar intercambio para la siguiente medida
      lr11xx_radio_set_tx(context, MANAGER_TX_RX_TIMEOUT_MS);
      contador_fallos++;
    }
  }

  // Si hay un timeout RTToF
  if ((irq_regs & LR11XX_SYSTEM_IRQ_RTTOF_TIMEOUT) == LR11XX_SYSTEM_IRQ_RTTOF_TIMEOUT) {
    lr11xx_radio_set_tx(context, MANAGER_TX_RX_TIMEOUT_MS);
    contador_fallos++;
  }
}

/**
 * @brief Función principal de ranging
 * @param context Contexto del LR11xx
 */
void ranging(const void* context) {
  uint8_t sf_raw = (configuracion_app.sf_bw >> 12) & 0x0F;  // Extrae el Spreading Factor de la configuración
  uint16_t bw_raw = configuracion_app.sf_bw & 0xFFF;        // Extrae el Bandwidth de la configuración

  // Traducir SF (Raw -> Enum Driver)
  switch (sf_raw) {
    case 7:
      lora_mod_params.sf = LR11XX_RADIO_LORA_SF7;
      break;
    case 8:
      lora_mod_params.sf = LR11XX_RADIO_LORA_SF8;
      break;
    case 9:
      lora_mod_params.sf = LR11XX_RADIO_LORA_SF9;
      break;
    case 10:
      lora_mod_params.sf = LR11XX_RADIO_LORA_SF10;
      break;
    case 11:
      lora_mod_params.sf = LR11XX_RADIO_LORA_SF11;
      break;
    case 12:
      lora_mod_params.sf = LR11XX_RADIO_LORA_SF12;
      break;
    default:
      lora_mod_params.sf = LORA_SPREADING_FACTOR;
      break;  // Fallback seguro
  }

  // Traducir BW (Raw -> Enum Driver)
  switch (bw_raw) {
    case 125:
      lora_mod_params.bw = LR11XX_RADIO_LORA_BW_125;
      break;
    case 250:
      lora_mod_params.bw = LR11XX_RADIO_LORA_BW_250;
      break;
    case 500:
      lora_mod_params.bw = LR11XX_RADIO_LORA_BW_500;
      break;
    default:
      lora_mod_params.bw = LORA_BANDWIDTH;
      break;  // Fallback seguro
  }

  // Inicializa el LR11xx si no se está usando LoRaWAN
  if (!(configuracion_app.flags & MODULO_LORAWAN)) {
    lr11xx_system_init(context);
  }

  // Inicializa RTToF
  rttof_init(context);
  lr11xx_radio_set_lora_sync_timeout(context, 0u);
  lr11xx_rttof_set_parameters(context, RESPONSE_SYMBOLS_COUNT);
  lr11xx_system_set_dio_irq_params(context, RTTOF_MANAGER_IRQ_MASK, 0);

  uint8_t n_validas = 0;
  
  for(uint8_t a = 0; a < configuracion_app.num_anclas && n_validas < configuracion_app.anclas_objetivo; a++) {
    // Resetear estado de medición para esta ancla
    n_muestras = 0;
    ancla_completada     = false;
    contador_fallos      = 0;

    lr11xx_rttof_set_request_address(context, (uint32_t)(a+1));
    lr11xx_system_clear_irq_status(context, LR11XX_SYSTEM_IRQ_ALL_MASK);
    lr11xx_radio_set_tx(context, MANAGER_TX_RX_TIMEOUT_MS);

    // Tiempo inicial para control de errores
    uint32_t t0 = hal_rtc_get_time_s();
    do{
      hal_watchdog_reload();
      rttof_irq_process(context, RTTOF_MANAGER_IRQ_MASK);

      if (contador_fallos >= FALLOS_MAX_POR_ANCLA) {
        HAL_DBG_TRACE_WARNING("Ancla %u: sin respuesta tras %u fallos consecutivos\n", a, contador_fallos);
        break;
      }
      if (hal_rtc_get_time_s() - t0 > 10) {
        HAL_DBG_TRACE_WARNING("Ancla %u: timeout global de sesion\n", a);
        break;
      }

    }while(!ancla_completada);
    
    // Calcular y almacenar distancia media (parcial si hubo fallos intermedios)
    if (n_muestras > 0) {
      int32_t dist, rssi;

      ultimas_medidas.ranging_muestras_n[n_validas] = n_muestras;
      for (uint8_t s = 0; s < n_muestras; s++) {
        int32_t d = muestras_anclas[s];
        
        ultimas_medidas.ranging_muestras_dist[n_validas][s] = (int16_t) d;
        ultimas_medidas.ranging_muestras_rssi[n_validas][s] = (int8_t) muestras_rssi[s];
      }

      mediana_medida(muestras_anclas, muestras_rssi, n_muestras, &dist, &rssi);
      if (dist < 0)   dist = 0;    // RTToF puede dar valores negativos por ruido
      if (dist > 65535) dist = 65535;  // Límite uint16_t (65535 m)
      ultimas_medidas.ranging[n_validas] = (uint16_t)dist;
      ultimas_medidas.ranging_addr[n_validas] = (uint32_t)(a + 1);
      ultimas_medidas.ranging_rssi[n_validas] = (int8_t)rssi;
      n_validas++;
    }
  }
  ultimas_medidas.ranging_len = n_validas;
  ultimas_medidas.ts_ranging  = get_timestamp();
}