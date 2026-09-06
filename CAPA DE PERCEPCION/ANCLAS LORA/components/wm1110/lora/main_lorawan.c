/*!
 * @file      main_lorawan.c
 *
 * @brief     Ancla LoRa: heartbeat LoRaWAN + subordinado RTToF
 *
 * Bucle principal:
 *   - smtc_modem_run_engine() gestiona toda la actividad LoRaWAN.
 *   - Cuando el modem tiene tiempo libre (> CAD_ACTIVATION_THRESHOLD_MS),
 *     se suspende el modem, se configura la radio para RTToF y se activa
 *     el duty-cycle CAD. Al despertar se devuelve la radio al modem.
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


#include "main_lorawan.h"
#include "apps_configuration.h"
#include "lorawan_key_config.h"
#include "app_at_fds_datas.h"
#include "sht41.h"
#include "apps_modem_common.h"
#include "apps_modem_event.h"
#include "apps_utilities.h"
#include "device_management_defs.h"
#include "smtc_board.h"
#include "smtc_board_ralf.h"
#include "smtc_hal.h"
#include "smtc_modem_api.h"
#include "smtc_modem_utilities.h"
#include "lr11xx_radio.h"
#include "lr11xx_rttof.h"
#include "lr11xx_system.h"
#include "lr11xx_system_types.h"
#include "smtc_shield_lr11xx_common_if.h"
#include "app_timer.h"
#include "nrf_drv_saadc.h"

/** @brief Identificador de la pila LoRaWAN usada por el modem (único stack). */
static uint8_t  stack_id = 0;

/** @brief Lista de data rates para el perfil ADR personalizado (16 entradas, DR5 fijo). */
static uint8_t  adr_list[16] = {
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05
};

/** @brief Número acumulado de intercambios RTToF respondidos con éxito. */
static uint32_t ranging_count = 0;
/** @brief Número acumulado de solicitudes RTToF descartadas por el hardware. */
static uint16_t discard_count = 0;
/** @brief RSSI (dBm) del último intercambio RTToF respondido. */
static int8_t   last_rssi     = 0;
/** @brief SNR (dB) del último intercambio RTToF respondido. */
static int8_t   last_snr      = 0;

/** @brief Parámetros de modulación LoRa para RTToF; se recalculan en cada ventana (@ref rttof_radio_init). */
static lr11xx_radio_mod_params_lora_t lora_mod_params;

/** @brief Parámetros de paquete LoRa para RTToF (constantes, tomados de apps_configuration.h). */
static const lr11xx_radio_pkt_params_lora_t lora_pkt_params = {
  .preamble_len_in_symb = LORA_PREAMBLE_LENGTH,
  .header_type          = LORA_PKT_LEN_MODE,
  .pld_len_in_bytes     = PAYLOAD_LENGTH,
  .crc                  = LORA_CRC,
  .iq                   = LORA_IQ,
};

/** @brief Configuración vigente de la ancla, con los valores por defecto (se sobreescribe desde flash o por downlink 0x01). */
anchor_config_t anchor_config = {
  .heartbeat_interval_s    = 60,
  .activation_threshold_ms = 2000,
  .margen_ms               = 1000,
  .lora_sf                 = LORA_SPREADING_FACTOR,
  .lora_bw                 = LORA_BANDWIDTH,
  ._pad                    = {0, 0},
};

/* RTToF */
static uint8_t rttof_compute_ldro(lr11xx_radio_lora_sf_t sf, lr11xx_radio_lora_bw_t bw);
static void    rttof_radio_init(const void* context);
static void    rttof_irq_process(const void* context);

/* LoRaWAN callbacks */
static void on_modem_reset(uint16_t reset_count);
static void on_modem_network_joined(void);
static void on_modem_alarm(void);
static void on_modem_tx_done(smtc_modem_event_txdone_status_t status);
static void on_modem_down_data(int8_t rssi, int8_t snr, smtc_modem_event_downdata_window_t rx_window, uint8_t port, const uint8_t* payload, uint8_t size);

/* LoRaWAN TX */
static void send_frame(const uint8_t* buffer, uint8_t length, bool tx_confirmed);

/**
 * @brief Callback de interrupción del ADC
 * @param p_event Evento del ADC
 * @details Handler vacío, la conversión se realiza de forma síncrona
 */
void ADC_interrupt(nrfx_saadc_evt_t const *p_event) {}

/**
 * @brief  Determina el valor de Low Data Rate Optimization (LDRO) para RTToF.
 * @param[in] sf  Spreading factor LoRa.
 * @param[in] bw  Ancho de banda LoRa.
 * @return 1 si debe activarse LDRO para la combinación SF/BW dada, 0 en caso contrario.
 */
static uint8_t rttof_compute_ldro(lr11xx_radio_lora_sf_t sf, lr11xx_radio_lora_bw_t bw) {
  switch (bw) {
    case LR11XX_RADIO_LORA_BW_500: return 0;
    case LR11XX_RADIO_LORA_BW_250: return (sf == LR11XX_RADIO_LORA_SF12) ? 1 : 0;
    case LR11XX_RADIO_LORA_BW_800:
    case LR11XX_RADIO_LORA_BW_400:
    case LR11XX_RADIO_LORA_BW_200:
    case LR11XX_RADIO_LORA_BW_125:
        return (sf == LR11XX_RADIO_LORA_SF12 || sf == LR11XX_RADIO_LORA_SF11) ? 1 : 0;
    case LR11XX_RADIO_LORA_BW_62:
        return (sf >= LR11XX_RADIO_LORA_SF10) ? 1 : 0;
    case LR11XX_RADIO_LORA_BW_41:
        return (sf >= LR11XX_RADIO_LORA_SF9) ? 1 : 0;
    default: return 1;
  }
}

/**
 * @brief  Configura la radio del LR11xx para operar como subordinado RTToF.
 *
 * Debe llamarse con el modem LoRaWAN suspendido, antes de cada ventana de
 * duty-cycle, ya que el modem puede haber modificado la configuración de la
 * radio entre ventanas. Ajusta el tipo de paquete, la frecuencia, el PA, los
 * parámetros de modulación LoRa (SF/BW/CR/LDRO según @ref anchor_config) y el
 * retardo RX/TX recomendado para el cálculo de distancia.
 *
 * @param[in] context  Contexto de la radio LR11xx (driver ral).
 */
static void rttof_radio_init(const void* context) {
  const smtc_shield_lr11xx_pa_pwr_cfg_t* pa_pwr_cfg = smtc_shield_lr11xx_get_pa_pwr_cfg(RF_FREQ_IN_HZ, TX_OUTPUT_POWER_DBM);

  if (pa_pwr_cfg == NULL) {
    HAL_DBG_TRACE_ERROR("RTToF: configuracion PA invalida\r\n");
    while (true);
  }

  ASSERT_SMTC_MODEM_RC(lr11xx_radio_set_pkt_type(context, LR11XX_RADIO_PKT_TYPE_RTTOF));
  ASSERT_SMTC_MODEM_RC(lr11xx_radio_set_rf_freq(context, RF_FREQ_IN_HZ));
  ASSERT_SMTC_MODEM_RC(lr11xx_radio_set_rssi_calibration(context, smtc_shield_lr11xx_get_rssi_calibration_table(RF_FREQ_IN_HZ)));
  ASSERT_SMTC_MODEM_RC(lr11xx_radio_set_pa_cfg(context, &pa_pwr_cfg->pa_config));
  ASSERT_SMTC_MODEM_RC(lr11xx_radio_set_tx_params(context, pa_pwr_cfg->power, PA_RAMP_TIME));
  ASSERT_SMTC_MODEM_RC(lr11xx_radio_set_rx_tx_fallback_mode(context, FALLBACK_MODE));
  ASSERT_SMTC_MODEM_RC(lr11xx_radio_cfg_rx_boosted(context, ENABLE_RX_BOOST_MODE));
  
  lora_mod_params.sf   = (lr11xx_radio_lora_sf_t)anchor_config.lora_sf;
  lora_mod_params.bw   = (lr11xx_radio_lora_bw_t)anchor_config.lora_bw;
  lora_mod_params.cr   = LORA_CODING_RATE;
  lora_mod_params.ldro = rttof_compute_ldro(lora_mod_params.sf, lora_mod_params.bw);

  ASSERT_SMTC_MODEM_RC(lr11xx_radio_set_lora_mod_params(context, &lora_mod_params));
  ASSERT_SMTC_MODEM_RC(lr11xx_radio_set_lora_pkt_params(context, &lora_pkt_params));
  ASSERT_SMTC_MODEM_RC(lr11xx_radio_set_lora_sync_word(context, LORA_SYNCWORD));

  uint32_t rttof_rx_tx_delay = 0u;
  if (smtc_shield_lr11x0_common_rttof_recommended_rx_tx_delay_indicator(RF_FREQ_IN_HZ, lora_mod_params.bw, lora_mod_params.sf, &rttof_rx_tx_delay)) {
    ASSERT_SMTC_MODEM_RC(lr11xx_rttof_set_rx_tx_delay_indicator(context, rttof_rx_tx_delay));
  } else {
    HAL_DBG_TRACE_ERROR("RTToF: no se pudo obtener el delay RX/TX\n");
  }
}

/**
 * @brief  Atiende las interrupciones RTToF del subordinado.
 *
 * Lee y limpia el registro de IRQ de la radio y, filtrando por
 * @ref RTTOF_SUBORDINATE_IRQ_MASK, actualiza los contadores de intercambios
 * válidos (@ref ranging_count) y descartados (@ref discard_count), guarda el
 * RSSI/SNR del último intercambio y vuelve a poner la radio en RX.
 *
 * @param[in] ctx  Contexto de la radio LR11xx.
 */
static void rttof_irq_process(const void* ctx){
  lr11xx_system_irq_mask_t irq;
  lr11xx_system_get_and_clear_irq_status(ctx, &irq);
  
  if (irq != 0) {
    HAL_DBG_TRACE_INFO("IRQ raw = 0x%08X\n", irq);
  }

  irq &= RTTOF_SUBORDINATE_IRQ_MASK;
  
  if (irq & LR11XX_SYSTEM_IRQ_RTTOF_RESP_DONE) {
    ranging_count++;
    lr11xx_radio_pkt_status_lora_t pkt_status;
    lr11xx_radio_get_lora_pkt_status(ctx, &pkt_status);
    last_rssi = pkt_status.rssi_pkt_in_dbm;
    last_snr  = pkt_status.snr_pkt_in_db;
    HAL_DBG_TRACE_INFO("RTToF: respuesta enviada count=%u rssi=%d snr=%d\n", ranging_count, last_rssi, last_snr);
    lr11xx_radio_set_rx(ctx, 0u);
  }

  if (irq & LR11XX_SYSTEM_IRQ_RTTOF_REQ_DISCARDED) {
    discard_count++;
    HAL_DBG_TRACE_INFO("RTToF: solicitud descartada discard=%u\n", discard_count);
    lr11xx_radio_set_rx(ctx, 0u);
  }
}

/**
 * @brief  Callback del modem al conceder o denegar el acceso de usuario a la radio.
 * @param[in] timestamp_ms  Marca de tiempo del evento en milisegundos.
 * @param[in] status        Resultado de la solicitud de acceso a la radio.
 */
void on_modem_user_radio_access(uint32_t timestamp_ms, smtc_modem_event_user_radio_access_status_t status) {
  HAL_DBG_TRACE_INFO("User radio access status: %d\n", status);
}

/**
 * @brief  Punto de entrada de la aplicación de la ancla.
 *
 * Inicializa la placa y los periféricos (I2C, SHT41, timers, FDS), el modem
 * LoRaWAN y sus callbacks. En el bucle principal ejecuta el motor del modem;
 * cuando este deja tiempo libre por encima de
 * @ref anchor_config_t::activation_threshold_ms suspende el modem, configura la
 * radio para RTToF y mantiene RX continuo atendiendo solicitudes de ranging
 * hasta @ref anchor_config_t::margen_ms antes de que el modem vuelva a
 * necesitar la radio. Con menos tiempo libre, duerme el MCU.
 *
 * @return No retorna.
 */
int main(void) {
  static apps_modem_event_callback_t smtc_event_callback = {
    .adr_mobile_to_static  = NULL,
    .alarm                 = on_modem_alarm,
    .almanac_update        = NULL,
    .down_data             = on_modem_down_data,
    .join_fail             = NULL,
    .joined                = on_modem_network_joined,
    .link_status           = NULL,
    .mute                  = NULL,
    .new_link_adr          = NULL,
    .reset                 = on_modem_reset,
    .set_conf              = NULL,
    .stream_done           = NULL,
    .time_updated_alc_sync = NULL,
    .tx_done               = on_modem_tx_done,
    .upload_done           = NULL,
    .user_radio_access     = on_modem_user_radio_access
  };

  ralf_t* modem_radio = smtc_board_initialise_and_get_ralf();
  hal_mcu_init();
  app_fds_init();

  hal_i2c_master_init( );
  hal_gpio_init_out( SENSOR_POWER, HAL_GPIO_SET );
  hal_mcu_wait_ms( 10 ); // wait power on
  SHT41Init( );

  ret_code_t err_code = app_timer_init();
  APP_ERROR_CHECK(err_code);

  apps_modem_event_init(&smtc_event_callback);
  smtc_modem_init(modem_radio, &apps_modem_event_process);

  while (1) {
    uint32_t sleep_ms = smtc_modem_run_engine();

    if (sleep_ms > anchor_config.activation_threshold_ms){
      smtc_modem_suspend_before_user_radio_access();
      const void* ctx = modem_radio->ral.context;

      lr11xx_system_clear_irq_status(ctx, LR11XX_SYSTEM_IRQ_ALL_MASK);  

      NVIC_DisableIRQ(GPIOTE_IRQn);

      /* Configurar radio para RTToF */
      rttof_radio_init(ctx);
      lr11xx_radio_set_lora_sync_timeout( ctx, 0u );
      lr11xx_rttof_set_parameters(ctx, RESPONSE_SYMBOLS_COUNT);
      lr11xx_system_set_dio_irq_params( ctx, RTTOF_SUBORDINATE_IRQ_MASK, 0 );
      lr11xx_rttof_set_address( ctx, RTTOF_ADDRESS, RTTOF_SUBORDINATE_CHECK_LENGTH_BYTES);
      lr11xx_system_clear_irq_status( ctx, LR11XX_SYSTEM_IRQ_ALL_MASK );
      lr11xx_radio_set_rx( ctx, 0u );

      /* Mantener RX hasta que LoRaWAN necesite la radio */
      uint32_t t0 = hal_rtc_get_time_ms();
      uint32_t window = sleep_ms - anchor_config.margen_ms;

      while ((hal_rtc_get_time_ms() - t0) < window){
        rttof_irq_process(ctx);
      }
      
      /* Salir de RX y devolver radio al modem */
      lr11xx_system_set_standby(ctx, LR11XX_SYSTEM_STANDBY_CFG_RC);

      NVIC_ClearPendingIRQ(GPIOTE_IRQn);
      NVIC_EnableIRQ(GPIOTE_IRQn);

      smtc_modem_resume_after_user_radio_access();
    }else{
      hal_mcu_set_sleep_for_ms(sleep_ms);
    }
  }
}

/**
 * @brief  Callback de reset del modem: configura LoRaWAN e inicia el join.
 * @param[in] reset_count  Número de resets del modem registrados.
 */
static void on_modem_reset(uint16_t reset_count) {
  apps_modem_common_configure_lorawan_params(stack_id);
  smtc_modem_dm_set_info_interval(SMTC_MODEM_DM_INFO_INTERVAL_IN_SECOND, 0);
  smtc_modem_dm_set_info_fields(NULL, 0);
  ASSERT_SMTC_MODEM_RC(smtc_modem_join_network(stack_id));
}

/**
 * @brief  Callback de red unida: fija el perfil ADR y arranca el temporizador de heartbeat.
 */
static void on_modem_network_joined(void) {
  ASSERT_SMTC_MODEM_RC(smtc_modem_adr_set_profile(stack_id, SMTC_MODEM_ADR_PROFILE_CUSTOM, adr_list));
  ASSERT_SMTC_MODEM_RC(smtc_modem_alarm_start_timer(anchor_config.heartbeat_interval_s));
}

/**
 * @brief  Callback de alarma periódica: compone y envía el uplink de heartbeat.
 *
 * Empaqueta la dirección RTToF, los contadores de intercambios válidos y
 * descartados, el RSSI/SNR del último intercambio, la temperatura y humedad
 * del SHT41 y la configuración vigente (periodo, SF, BW), lo transmite con
 * @ref send_frame y rearma el temporizador.
 */
static void on_modem_alarm(void) {
  uint8_t buf[17];
  buf[0]  = (RTTOF_ADDRESS >> 24) & 0xFF; //RTTOF Address
  buf[1]  = (RTTOF_ADDRESS >> 16) & 0xFF;
  buf[2]  = (RTTOF_ADDRESS >>  8) & 0xFF;
  buf[3]  =  RTTOF_ADDRESS        & 0xFF;
  buf[4]  = (ranging_count >> 16) & 0xFF; // Intercambios válidos
  buf[5]  = (ranging_count >>  8) & 0xFF;
  buf[6]  =  ranging_count        & 0xFF;
  buf[7]  = (discard_count >>  8) & 0xFF; // Intercambios rechazados
  buf[8]  =  discard_count        & 0xFF;
  buf[9]  = (uint8_t)last_rssi; // RSSI del ultimo intercambio
  buf[10] = (uint8_t)last_snr; // SNR del ultimo intercambio

  float temp = 0, humi = 0;
  if (!SHT41GetTempAndHumi(&temp, &humi)) {
    HAL_DBG_TRACE_WARNING("SHT41: lectura fallida\n");
  }

  buf[11] = (uint8_t)(int8_t)temp;
  buf[12] = (humi < 0.0f) ? 0 : (uint8_t)humi;

  buf[13] = (anchor_config.heartbeat_interval_s >> 8) & 0xFF;  // periodo MSB
  buf[14] =  anchor_config.heartbeat_interval_s       & 0xFF;

  buf[15] =  anchor_config.lora_sf;                            // SF actual
  buf[16] =  anchor_config.lora_bw;                            // BW actual

  send_frame(buf, sizeof(buf), LORAWAN_CONFIRMED_MSG_ON);
  ASSERT_SMTC_MODEM_RC(smtc_modem_alarm_start_timer(anchor_config.heartbeat_interval_s));
}

/**
 * @brief  Callback de fin de transmisión: traza el resultado del uplink.
 * @param[in] status  Estado de la transmisión.
 */
static void on_modem_tx_done(smtc_modem_event_txdone_status_t status) {
  static uint32_t uplink_count = 0;
  HAL_DBG_TRACE_INFO("LoRaWAN: uplink %u completado (status=%d)\n", ++uplink_count, status);
}

/**
 * @brief  Callback de recepción de downlink: procesa los comandos de gestión.
 *
 * Comandos soportados según el primer byte del payload:
 *   - 0x01: actualiza @ref anchor_config con la estructura recibida, la
 *           persiste en flash y reprograma el temporizador de heartbeat.
 *   - 0x02: reinicia el dispositivo.
 *
 * @param[in] rssi       RSSI del downlink (codificado; se resta 64 al trazar).
 * @param[in] snr        SNR del downlink (codificado; se desplaza 2 bits al trazar).
 * @param[in] rx_window  Ventana de recepción (RX1/RX2/RXC).
 * @param[in] port       Puerto LoRaWAN del downlink.
 * @param[in] payload    Datos recibidos.
 * @param[in] size       Longitud de @p payload en bytes.
 */
static void on_modem_down_data(int8_t rssi, int8_t snr, smtc_modem_event_downdata_window_t rx_window, uint8_t port, const uint8_t* payload, uint8_t size) {
  HAL_DBG_TRACE_INFO("LoRaWAN: downlink puerto=%u size=%u RSSI=%d SNR=%d\n", port, size, rssi - 64, snr >> 2);

  switch (rx_window) {
    case SMTC_MODEM_EVENT_DOWNDATA_WINDOW_RX1:
        HAL_DBG_TRACE_INFO("  ventana: RX1\n"); break;
    case SMTC_MODEM_EVENT_DOWNDATA_WINDOW_RX2:
        HAL_DBG_TRACE_INFO("  ventana: RX2\n"); break;
    case SMTC_MODEM_EVENT_DOWNDATA_WINDOW_RXC:
        HAL_DBG_TRACE_INFO("  ventana: RXC\n"); break;
  }
  
  if (size == 0) return;
  uint8_t cmd = payload[0];

  if(cmd == 0x01){
    if (size < 1 + (uint8_t)sizeof(anchor_config_t)) {
      HAL_DBG_TRACE_WARNING("CMD_CONFIG_UPDATE: payload corto (%u bytes)\n", size);
      return;
    }
    memcpy(&anchor_config, &payload[1], sizeof(anchor_config_t)); // Actualizo la variable
    app_fds_store_save(); // Guardo configuracion en flash
    
    //Actualizo el temporizador
    smtc_modem_alarm_clear_timer();
    smtc_modem_alarm_start_timer(anchor_config.heartbeat_interval_s);
    
  }else if(cmd == 0x02){
    HAL_DBG_TRACE_INFO("CMD_RESET: reiniciando dispositivo\n");
    hal_mcu_wait_ms(100); 
    NVIC_SystemReset();
  }else{
    HAL_DBG_TRACE_WARNING("CMD desconocido: 0x%02X\n", payload[0]);
  }
}

/**
 * @brief  Solicita un uplink LoRaWAN respetando el duty-cycle y el tamaño máximo.
 *
 * Si el duty-cycle está limitado, descarta el envío. Si @p length supera el
 * payload máximo permitido en la próxima transmisión, envía un uplink vacío.
 *
 * @param[in] buffer        Datos a transmitir.
 * @param[in] length        Longitud de @p buffer en bytes.
 * @param[in] tx_confirmed  true para solicitar un uplink confirmado.
 */
static void send_frame(const uint8_t* buffer, uint8_t length, bool tx_confirmed) {
  int32_t duty_cycle;
  ASSERT_SMTC_MODEM_RC(smtc_modem_get_duty_cycle_status(&duty_cycle));
  if (duty_cycle < 0) {
    HAL_DBG_TRACE_WARNING("LoRaWAN: duty-cycle limitado, proximo uplink en %d ms\n", duty_cycle);
    return;
  }

  uint8_t tx_max_payload;
  ASSERT_SMTC_MODEM_RC(smtc_modem_get_next_tx_max_payload(stack_id, &tx_max_payload));

  if (length > tx_max_payload) {
    HAL_DBG_TRACE_WARNING("LoRaWAN: payload grande, enviando uplink vacio\n");
    ASSERT_SMTC_MODEM_RC(smtc_modem_request_empty_uplink(stack_id, true, LORAWAN_APP_PORT, tx_confirmed));
  } else {
    ASSERT_SMTC_MODEM_RC(smtc_modem_request_uplink(stack_id, LORAWAN_APP_PORT, tx_confirmed, buffer, length));
  }
}