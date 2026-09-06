/*!
 * @file      main_lorawan.c
 *
 * @brief     LoRa Basics Modem Class A/C device implementation
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
#include "GPS.h"
#include "app_at_fds_datas.h"
#include "app_ble_all.h"
#include "app_timer.h"
#include "apps_modem_common.h"
#include "apps_modem_event.h"
#include "apps_utilities.h"
#include "device_management_defs.h"
#include "lorawan_key_config.h"
#include "lr11xx_system.h"
#include "main_LPS22DF.h"
#include "main_LSM6DSOX.h"
#include "main_bateria.h"
#include "main_flash.h"
#include "main_lora.h"
#include "main_ranging.h"
#include "nrf_drv_gpiote.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_pwr_mgmt.h"
#include "sensor_power.h"
#include "smtc_board.h"
#include "smtc_board_ralf.h"
#include "smtc_hal.h"
#include "smtc_modem_api.h"
#include "smtc_modem_utilities.h"

/** @brief Temporizador para la máquina de estados */
APP_TIMER_DEF(m_app_state_timer);

/** @brief Buffer del payload de uplink LoRaWAN */
static uint8_t buf_lorawan[PAYLOAD_MAX_LORAWAN];
/** @brief Longitud actual del payload de uplink LoRaWAN */
static uint16_t len_lorawan = 0;

/** @brief Buffer del payload a guardar en flash */
static uint8_t buf_flash[PAYLOAD_MAX_FLASH];
/** @brief Longitud actual del payload a guardar en flash */
static uint16_t len_flash = 0;

/** @brief Offset de tiempo Unix */
static uint32_t current_unix_offset = 0;

/** @brief Indica si el tiempo está sincronizado */
static bool is_time_synchronized = false;

/** @brief Ticks del periodo objetivo, usado en modo BLE */
static uint32_t ticks_periodo_objetivo = 0;

/** @brief Identificador de la pila LoRaWAN */
static uint8_t stack_id = 0;

/** @brief Lista personalizada de data rates para ADR */
static uint8_t adr_list[16] = {0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05};

/** @brief Estado actual de la máquina de estados */
static uint8_t estado = ENCENDER_GPS;

/** @brief Timestamp de encendido del GPS en el ciclo actual, usado para el timeout de fix */
static uint32_t gps_ts_inicio = 0;

/** @brief Tiempo restante hasta el siguiente envío (usado en modo BLE) */
static uint32_t tiempo_siguiente_envio = 0;

/** @brief Indica si se obtuvo un fix de GPS válido en el ciclo actual */
static bool gps_fix_ciclo = false;

/** @brief Contadores acumulados de tiempo por módulo, usados para disparar cada periodo */
static tick_counters_t contadores = {0};

/** @brief Subconjunto de flags de módulos que toca medir en el ciclo actual */
static uint8_t flags_ciclo = 0;

/** @brief Indica si en el ciclo actual toca enviar por LoRaWAN */
static bool    enviar_ciclo = false;

/** @brief Duración estimada del último/próximo ciclo de medida, en segundos */
static uint32_t duracion_ciclo = 1;

/** @brief Últimas medidas de todos los sensores/módulos, usadas para construir los payloads */
ultimas_medidas_t ultimas_medidas = {0};

/**
 * @brief Envía una trama LoRaWAN
 * @param buffer Puntero al buffer con los datos a enviar
 * @param length Longitud del buffer en bytes
 * @param tx_confirmed true para transmisión confirmada, false para no confirmada
 */
static void send_frame(const uint8_t* buffer, const uint16_t length, bool tx_confirmed);

/**
 * @brief Callback ejecutado cuando el módem se reinicia
 * @param reset_count Contador de reinicios del módem
 * @details Configura parámetros LoRaWAN e inicia el proceso de join
 */
static void on_modem_reset(uint16_t reset_count);

/**
 * @brief Callback ejecutado tras unirse exitosamente a la red
 * @details Configura ADR personalizado y activa sincronización de tiempo
 */
static void on_modem_network_joined(void);

/**
 * @brief Callback ejecutado cuando expira la alarma periódica
 * @details Avanza la máquina de estados principal de la aplicación
 */
static void on_modem_alarm(void);

/**
 * @brief Callback ejecutado cuando se sincroniza el tiempo
 * @param status Estado de la sincronización de tiempo
 */
static void on_modem_time_sync(smtc_modem_event_time_status_t status);

/**
 * @brief Callback ejecutado tras completar una transmisión
 * @param status Estado de la transmisión completada
 */
static void on_modem_tx_done(smtc_modem_event_txdone_status_t status);

/**
 * @brief Callback ejecutado al recibir un downlink
 * @param rssi RSSI del downlink recibido
 * @param snr SNR del downlink recibido
 * @param rx_window Ventana de recepción utilizada
 * @param port Puerto LoRaWAN del downlink
 * @param payload Puntero al payload recibido
 * @param size Tamaño del payload en bytes
 */
static void on_modem_down_data(int8_t rssi, int8_t snr, smtc_modem_event_downdata_window_t rx_window, uint8_t port, const uint8_t* payload, uint8_t size);

/** @brief Función de procesamiento de la máquina de estados */
static void app_process(void);

/** @brief Gestiona la configuración de la aplicación */
static void gestionar_configuracion(void);

/** @brief Gestiona la conexión BLE */
static void gestionar_conexion_ble(void);

/** @brief Gestiona las medicion de distancias*/
static void procesar_ranging(const void* context);

/**
 * @brief Manejador del temporizador de la máquina de estados
 * @param p_context Contexto del temporizador
 */
static void app_state_timer_handler(void* p_context) {
  flags_globales.m_flag_maquina_pendiente = true;
}

/**
 * @brief Inicia el temporizador de la máquina de estados
 * @param segundos Segundos hasta que expire el temporizador
 */
static void app_state_start_timer(uint32_t segundos) {
  if (configuracion_app.flags & MODULO_LORAWAN) {
    smtc_modem_alarm_start_timer(segundos);
  } else {
    uint32_t ticks_actuales = app_timer_cnt_get();
    uint32_t ticks_periodo = APP_TIMER_TICKS(segundos * 1000);

    ticks_periodo_objetivo = (ticks_actuales + ticks_periodo) & APP_TIMER_MAX_CNT_VAL;

    app_timer_start(m_app_state_timer, APP_TIMER_TICKS(segundos * 1000), NULL);
  }
}

/**
 * @brief Calcula cuántos segundos faltan hasta que toque medir o enviar algo
 * @return Segundos hasta el próximo evento pendiente entre todos los módulos activos,
 * o @c configuracion_app.periodo_medida si no hay ningún periodo configurado
 */
static uint32_t tiempo_hasta_proximo(void){
  uint32_t min = UINT32_MAX;
  
  if((configuracion_app.flags & MODULO_AMBIENTE) && configuracion_app.periodo_ambiente > 0){
    min = MIN(min, configuracion_app.periodo_ambiente - contadores.ambiente);
  }
  if((configuracion_app.flags & MODULO_ANGULO) && configuracion_app.periodo_angulo > 0){
    min = MIN(min, configuracion_app.periodo_angulo - contadores.angulo);
  }
  if((configuracion_app.flags & MODULO_GPS) && configuracion_app.periodo_gps > 0){
    min = MIN(min, configuracion_app.periodo_gps - contadores.gps);
  }
  if((configuracion_app.flags & MODULO_RANGING) && configuracion_app.periodo_ranging > 0){
    min = MIN(min, configuracion_app.periodo_ranging - contadores.ranging);
  }
  if((configuracion_app.flags & MODULO_BEACONS) && configuracion_app.periodo_beacons > 0){
    min = MIN(min, configuracion_app.periodo_beacons - contadores.beacons);
  }
  if((configuracion_app.flags & MODULO_LORAWAN) && configuracion_app.periodo_envio > 0){
    min = MIN(min, configuracion_app.periodo_envio - contadores.envio);
  }
  if((configuracion_app.flags & MODULO_IMU_PERIODICO) && configuracion_app.periodo_imu_periodico > 0){
    min = MIN(min, configuracion_app.periodo_imu_periodico - contadores.imu_periodico);
  }
  
  if (min == UINT32_MAX || min == 0) return configuracion_app.periodo_medida;

  return min;
}

/**
 * @brief Avanza los contadores de periodo según el tiempo transcurrido
 * @details Actualiza @c contadores con el tiempo transcurrido desde la última llamada y,
 * para cada módulo cuyo periodo se haya cumplido, marca su flag en @c flags_ciclo
 * (o @c enviar_ciclo para el envío LoRaWAN) y descuenta el periodo del contador
 */
static void actualizar_contadores(void) {
  static uint32_t ts_ultimo = 0;

  flags_ciclo  = 0;
  enviar_ciclo = false;
  
  uint32_t ts_ahora = hal_rtc_get_time_s();
  uint32_t elapsed = (ts_ultimo > 0)? (ts_ahora - ts_ultimo) : duracion_ciclo;
  ts_ultimo = ts_ahora;

  //Actualizado contadores de medida
  contadores.ambiente      += elapsed;
  contadores.gps           += elapsed;
  contadores.ranging       += elapsed;
  contadores.beacons       += elapsed;
  contadores.envio         += elapsed;
  contadores.angulo        += elapsed;
  contadores.imu_periodico += elapsed;
  
  // Actualizo los flags que toca medir en el ciclo de medida
  // Modulos ambienteales
  while ((configuracion_app.flags & MODULO_AMBIENTE) && configuracion_app.periodo_ambiente > 0 && contadores.ambiente >= configuracion_app.periodo_ambiente){ 
    flags_ciclo |= MODULO_AMBIENTE; 
    contadores.ambiente -= configuracion_app.periodo_ambiente; 
  }
  
  while ((configuracion_app.flags & MODULO_ANGULO) && configuracion_app.periodo_angulo > 0 && contadores.angulo >= configuracion_app.periodo_angulo){ 
    flags_ciclo |= MODULO_ANGULO; 
    contadores.angulo -= configuracion_app.periodo_angulo; 
  }
  
  // Modulo de GPS
  while ((configuracion_app.flags & MODULO_GPS) && configuracion_app.periodo_gps > 0 && contadores.gps >= configuracion_app.periodo_gps){ 
    flags_ciclo |= MODULO_GPS;           
    contadores.gps -= configuracion_app.periodo_gps; 
  }
  
  // Modulo ranging
  while ((configuracion_app.flags & MODULO_RANGING) && configuracion_app.periodo_ranging > 0 && contadores.ranging >= configuracion_app.periodo_ranging){ 
    flags_ciclo |= MODULO_RANGING;                    
    contadores.ranging  -= configuracion_app.periodo_ranging; 
  }

  // Modulo de beacons
  while ((configuracion_app.flags & MODULO_BEACONS) && configuracion_app.periodo_beacons > 0 && contadores.beacons >= configuracion_app.periodo_beacons){ 
    flags_ciclo |= MODULO_BEACONS;                    
    contadores.beacons -= configuracion_app.periodo_beacons; 
  }

  // Modulo de envio de datos
  while ((configuracion_app.flags & MODULO_LORAWAN) && configuracion_app.periodo_envio > 0 && contadores.envio >= configuracion_app.periodo_envio){ 
    enviar_ciclo  = true;                              
    contadores.envio -= configuracion_app.periodo_envio; 
  }

  // Modulo imu periodico
  while ((configuracion_app.flags & MODULO_IMU_PERIODICO) && configuracion_app.periodo_imu_periodico > 0 && contadores.imu_periodico >= configuracion_app.periodo_imu_periodico){ 
    flags_globales.m_flag_imu_disparo_periodico  = true;                              
    contadores.imu_periodico -= configuracion_app.periodo_imu_periodico; 
  }
}

/** @brief Punto de entrada principal */
int main(void) {
  // Estructura de callbacks del módem
  static apps_modem_event_callback_t smtc_event_callback = {
      .adr_mobile_to_static = NULL,
      .alarm = on_modem_alarm,
      .almanac_update = NULL,
      .down_data = on_modem_down_data,
      .join_fail = NULL,
      .joined = on_modem_network_joined,
      .link_status = NULL,
      .mute = NULL,
      .new_link_adr = NULL,
      .reset = on_modem_reset,
      .set_conf = NULL,
      .stream_done = NULL,
      .time_updated_alc_sync = on_modem_time_sync,
      .tx_done = on_modem_tx_done,
      .upload_done = NULL,
  };

  // Inicializa el objeto radio (RALF) correspondiente a la placa
  ralf_t* modem_radio = smtc_board_initialise_and_get_ralf();

  // Inicializa el hardware
  hal_mcu_init();
  app_fds_init();
  flash_init();

  // Inicializa los temporizadores
  ret_code_t err_code;
  err_code = app_timer_init();
  APP_ERROR_CHECK(err_code);

  APP_ERROR_CHECK(app_timer_create(&m_app_state_timer, APP_TIMER_MODE_SINGLE_SHOT, app_state_timer_handler));

  // Inicializa el BLE
  app_ble_all_init();
  // Inicializa los sensores
  configurar_sensores();

  // Inicializa el módem LoRaWAN si esta habilitado
  if (configuracion_app.flags & MODULO_LORAWAN) {
    encender_lr1110();
    apps_modem_event_init(&smtc_event_callback); // Inicializa los callbacks del módem
    smtc_modem_init(modem_radio, &apps_modem_event_process);  // Inicializa el módem
    app_state_start_timer(1);
  } else { // Si no esta habilitado, apaga el módem y programa la máquina de estados
    apagar_lr1110();
    app_state_start_timer(1);
  }

  // Bucle principal
  while (1) {
    gestionar_configuracion();  // Gestiona la configuración de la aplicación
    gestionar_conexion_ble();   // Gestiona la conexión BLE

    if(flags_globales.m_flag_reiniciar && !flags_globales.m_flag_ble_conectado) hal_mcu_reset();

    // Si el BLE está conectado, procesa los comandos BLE y recarga el watchdog
    if (flags_globales.m_flag_ble_conectado) {
      procesar_comandos_ble();  // Procesa los comandos BLE
      hal_watchdog_reload();    // Recarga el watchdog
      nrf_pwr_mgmt_run();       // Entra en modo sleep
      continue;
    }
    
    // Si hay un borrado de flash pendiente, pero el ble se ha desconectado (por un fallo), terminarlo
    if (flags_globales.m_flag_borrado_flash){
      borrar_flash();
      hal_watchdog_reload();
      nrf_pwr_mgmt_run();
      continue;
    }
    
    // Cuando esta encendido el GPS, procesamos en el bucle principal para que si hay un fix valiado y no se llega a COMPROBAR_FIX se apague antes
    if (configuracion_app.flags & MODULO_GPS) gps_process_background();
  

    // Procesa la máquina de estados
    if (flags_globales.m_flag_maquina_pendiente) {
      flags_globales.m_flag_maquina_pendiente = false;
      app_process();
    }

    // Como el ranging es bloqueante, se procesa en el bucle principal
    if (flags_globales.m_flag_ejecutar_ranging) procesar_ranging(modem_radio->ral.context);
    

    // Gestiona las interrupciones de la IMU
    if ((flags_globales.m_flag_movimiento || flags_globales.m_flag_watermark) && (configuracion_app.flags & (MODULO_IMU_XL | MODULO_IMU_GY))) {
      gestion_interrupcion_lsm6dsox();
    }

    if (flags_globales.m_flag_imu_disparo_periodico  && (configuracion_app.flags & (MODULO_IMU_XL | MODULO_IMU_GY))){
      flags_globales.m_flag_imu_disparo_periodico = false;
      if(configuracion_app.flags & MODULO_IMU_PERIODICO){
        imu_disparo_periodico();
      }
    }

    // Procesar los paquetes pendientes de guardado en Flash
    flash_procesar_pendientes();

    // Si está el módem LoRaWAN, ejecuta el motor lorawan y entra en modo sleep
    // Si no está el módem LoRaWAN, entra en modo sleep
    if (configuracion_app.flags & MODULO_LORAWAN) {
      uint32_t sleep_time_ms = smtc_modem_run_engine();  // Motor lorawan. Llamar en sleep_time_ms o menos.

      // Si se va a ejecutar el ranging, no entra en modo sleep
      if (flags_globales.m_flag_ejecutar_ranging == true) {
        sleep_time_ms = 0;
      }

      if (flags_globales.m_flag_maquina_pendiente == true){
        sleep_time_ms = 0;
      }

      hal_watchdog_reload();  // Recarga el watchdog
      hal_mcu_set_sleep_for_ms(sleep_time_ms);
    } else {
      hal_watchdog_reload();  // Recarga el watchdog
      nrf_pwr_mgmt_run();     // Entra en modo sleep
    }
  }
}

/**
 * @brief Callback cuando se detecta un reinicio del módem
 * @param reset_count Número de reinicios
 */
static void on_modem_reset(uint16_t reset_count) {
  // Configura los parámetros LoRaWAN
  apps_modem_common_configure_lorawan_params(stack_id);

  ultimas_medidas.reset_count = reset_count;

  smtc_modem_dm_set_info_interval( SMTC_MODEM_DM_INFO_INTERVAL_IN_SECOND, 0 );
  smtc_modem_dm_set_info_fields( NULL, 0 );

  // Solicita unirse a la red
  ASSERT_SMTC_MODEM_RC(smtc_modem_join_network(stack_id));
}

/**
 * @brief Callback cuando el módem se une a la red
 */
static void on_modem_network_joined(void) {
  // Configura perfil ADR
  ASSERT_SMTC_MODEM_RC(smtc_modem_adr_set_profile(stack_id, SMTC_MODEM_ADR_PROFILE_CUSTOM, adr_list));

  // Sincronizar reloj
  ASSERT_SMTC_MODEM_RC(smtc_modem_time_set_sync_interval_s(86400));
  ASSERT_SMTC_MODEM_RC(smtc_modem_time_start_sync_service(stack_id, SMTC_MODEM_TIME_MAC_SYNC));
  smtc_modem_time_trigger_sync_request(stack_id);
}

/** @brief Callback para la alarma periódica */
static void on_modem_alarm(void) {
  flags_globales.m_flag_maquina_pendiente = true;
}

/**
 * @brief Callback para la finalización de una transmisión
 * @param status Estado de la transmisión
 */
static void on_modem_tx_done(smtc_modem_event_txdone_status_t status) {
  static uint32_t uplink_count = 0;
  HAL_DBG_TRACE_INFO("Uplink numero: %d\n", ++uplink_count);
}

/**
 * @brief Callback para recibir datos desde la red
 * @param rssi RSSI del paquete recibido
 * @param snr SNR del paquete recibido
 * @param rx_window Ventana de recepción
 * @param port Puerto Fport
 * @param payload Puntero al buffer con los datos recibidos
 * @param size Tamaño del buffer
 */
static void on_modem_down_data(int8_t rssi, int8_t snr, smtc_modem_event_downdata_window_t rx_window, uint8_t port, const uint8_t* payload, uint8_t size) {
  HAL_DBG_TRACE_INFO("Downlink recibido:\n");
  HAL_DBG_TRACE_INFO("  - Puerto Fport = %d\n", port);
  HAL_DBG_TRACE_INFO("  - Tamaño payload = %d\n", size);
  HAL_DBG_TRACE_INFO("  - RSSI = %d dBm\n", rssi - 64);
  HAL_DBG_TRACE_INFO("  - SNR  = %d dB\n", snr >> 2);

  // Muestra en qué ventana se recibió
  switch (rx_window) {
    case SMTC_MODEM_EVENT_DOWNDATA_WINDOW_RX1:
      HAL_DBG_TRACE_INFO("  - Ventana Rx = %s\n", xstr(SMTC_MODEM_EVENT_DOWNDATA_WINDOW_RX1));
      break;
    case SMTC_MODEM_EVENT_DOWNDATA_WINDOW_RX2:
      HAL_DBG_TRACE_INFO("  - Ventana Rx = %s\n", xstr(SMTC_MODEM_EVENT_DOWNDATA_WINDOW_RX2));
      break;
    case SMTC_MODEM_EVENT_DOWNDATA_WINDOW_RXC:
      HAL_DBG_TRACE_INFO("  - Ventana Rx = %s\n", xstr(SMTC_MODEM_EVENT_DOWNDATA_WINDOW_RXC));
      break;
  }
}

/**
 * @brief Callback para la sincronización del tiempo
 * @param status Estado de la sincronización
 */
static void on_modem_time_sync(smtc_modem_event_time_status_t status) {
  if (status == SMTC_MODEM_EVENT_TIME_VALID) {
    uint32_t unix_timestamp_ts = apps_modem_common_get_utc_time();
    sync_timestamp(unix_timestamp_ts);
  }
}

/**
 * @brief Envía un marco LoRaWAN
 * @param buffer Puntero al buffer con los datos a enviar
 * @param length Tamaño del buffer
 * @param tx_confirmed Indica si se requiere confirmación de recepción
 */
static void send_frame(const uint8_t* buffer, const uint16_t length, bool tx_confirmed) {
  uint8_t tx_max_payload;
  int32_t duty_cycle;

  // Comprueba si hay restricciones de duty-cycle
  ASSERT_SMTC_MODEM_RC(smtc_modem_get_duty_cycle_status(&duty_cycle));
  if (duty_cycle < 0) {
    HAL_DBG_TRACE_WARNING("Limitación de duty-cycle - proximo uplink en %d ms\n\n", -duty_cycle);
    return;
  }

  // Comprueba el tamaño máximo de payload permitido
  ASSERT_SMTC_MODEM_RC(smtc_modem_get_next_tx_max_payload(stack_id, &tx_max_payload));

  // Si el payload es demasiado grande, se envía un uplink vacío para liberar comandos MAC
  if (length > tx_max_payload) {
    HAL_DBG_TRACE_WARNING("Payload demasiado grande - se enviara uplink vacio para liberar comandos MAC\n");
    ASSERT_SMTC_MODEM_RC(smtc_modem_request_empty_uplink(stack_id, true, LORAWAN_APP_PORT, tx_confirmed));
  } else {  // Si el payload es válido, se envía el uplink
    HAL_DBG_TRACE_INFO("Solicitando uplink\n");
    ASSERT_SMTC_MODEM_RC(smtc_modem_request_uplink(stack_id, LORAWAN_APP_PORT, tx_confirmed, buffer, length));
  }
}

/**
 * @brief Construye el payload a guardar en flash con las medidas del ciclo
 * @param flags_ciclo Subconjunto de flags de módulos medidos en este ciclo, indica qué
 * campos incluir en el payload
 * @details Deja el resultado en el buffer estático @c buf_flash / @c len_flash
 */
void construir_payload_flash(uint8_t flags_ciclo) {
  len_flash = 0;
  
  payload_add(buf_flash, &len_flash, PAYLOAD_MAX_FLASH, (const uint8_t*)&flags_ciclo, 1);
  payload_add(buf_flash, &len_flash, PAYLOAD_MAX_FLASH, (const uint8_t*)&ultimas_medidas.bateria_mv, 2);  // siempre

  if (flags_ciclo & MODULO_AMBIENTE) {
    payload_add(buf_flash, &len_flash, PAYLOAD_MAX_FLASH, (const uint8_t*)&ultimas_medidas.presion,     2);
    payload_add(buf_flash, &len_flash, PAYLOAD_MAX_FLASH, (const uint8_t*)&ultimas_medidas.temperatura, 1);
  }
  if (flags_ciclo & MODULO_ANGULO) {
    payload_add(buf_flash, &len_flash, PAYLOAD_MAX_FLASH, (const uint8_t*)&ultimas_medidas.pitch, 2);
    payload_add(buf_flash, &len_flash, PAYLOAD_MAX_FLASH, (const uint8_t*)&ultimas_medidas.roll,  2);
  }
  if (flags_ciclo & MODULO_GPS) {
    payload_add(buf_flash, &len_flash, PAYLOAD_MAX_FLASH, (const uint8_t*)&ultimas_medidas.gps_lat, 4);
    payload_add(buf_flash, &len_flash, PAYLOAD_MAX_FLASH, (const uint8_t*)&ultimas_medidas.gps_lon, 4);
  }
  if (flags_ciclo & MODULO_RANGING) {
    payload_add(buf_flash, &len_flash, PAYLOAD_MAX_FLASH, (const uint8_t*)&ultimas_medidas.ranging_len, 1);
    payload_add(buf_flash, &len_flash, PAYLOAD_MAX_FLASH, (const uint8_t*)ultimas_medidas.ranging_addr, ultimas_medidas.ranging_len * sizeof(uint32_t));
    for (uint8_t k = 0; k < ultimas_medidas.ranging_len; k++) {
      uint8_t n = ultimas_medidas.ranging_muestras_n[k];
      payload_add(buf_flash, &len_flash, PAYLOAD_MAX_FLASH, &n, 1);
      payload_add(buf_flash, &len_flash, PAYLOAD_MAX_FLASH, (const uint8_t*)ultimas_medidas.ranging_muestras_dist[k], n * sizeof(int16_t));
      payload_add(buf_flash, &len_flash, PAYLOAD_MAX_FLASH, (const uint8_t*)ultimas_medidas.ranging_muestras_rssi[k], n);
    }
  }
  if (flags_ciclo & MODULO_BEACONS) {
    payload_add(buf_flash, &len_flash, PAYLOAD_MAX_FLASH, &ultimas_medidas.ble_logger_count, 1);
    for (int i = 0; i < ultimas_medidas.ble_logger_count; i++) {
      uint8_t type = HOWL_TYPE_LOGGER;
      payload_add(buf_flash, &len_flash, PAYLOAD_MAX_FLASH, &type, 1);
      payload_add(buf_flash, &len_flash, PAYLOAD_MAX_FLASH, &ultimas_medidas.ble_logger_ids[i], 1);
      payload_add(buf_flash, &len_flash, PAYLOAD_MAX_FLASH, (uint8_t*)&ultimas_medidas.ble_logger_rssi[i], 1);
      payload_add(buf_flash, &len_flash, PAYLOAD_MAX_FLASH, &ultimas_medidas.ble_logger_obs[i], 1);
    }
    payload_add(buf_flash, &len_flash, PAYLOAD_MAX_FLASH, &ultimas_medidas.ble_beacon_count, 1);
    for (int i = 0; i < ultimas_medidas.ble_beacon_count; i++) {
      uint8_t type = HOWL_TYPE_BEACON;
      uint8_t blen  = MIN(ultimas_medidas.ble_beacon_data[i][0], MAX_BEACON_SIZE - 1);
      payload_add(buf_flash, &len_flash, PAYLOAD_MAX_FLASH, &type, 1);
      payload_add(buf_flash, &len_flash, PAYLOAD_MAX_FLASH, &ultimas_medidas.ble_beacon_ids[i], 1);
      payload_add(buf_flash, &len_flash, PAYLOAD_MAX_FLASH, (uint8_t*)&ultimas_medidas.ble_beacon_rssi[i], 1);
      payload_add(buf_flash, &len_flash, PAYLOAD_MAX_FLASH, &ultimas_medidas.ble_beacon_obs[i], 1);
      payload_add(buf_flash, &len_flash, PAYLOAD_MAX_FLASH, &blen, 1);
      payload_add(buf_flash, &len_flash, PAYLOAD_MAX_FLASH, &ultimas_medidas.ble_beacon_data[i][1], blen);
    }
  }

  uint32_t ts = get_timestamp(); 
  payload_add(buf_flash, &len_flash, PAYLOAD_MAX_FLASH, (const uint8_t*)&ts, 4);
}

/**
 * @brief Construye el payload de uplink LoRaWAN con las últimas medidas
 * @details Incluye los campos habilitados en @c configuracion_app.flags_envio, más los
 * campos de diagnóstico (ocupación de flash, reinicios, TTFF GPS, etc.) y la MAC BLE.
 * Deja el resultado en el buffer estático @c buf_lorawan / @c len_lorawan
 */
void construir_uplink(void) {
  len_lorawan = 0;

  payload_add(buf_lorawan, &len_lorawan, PAYLOAD_MAX_LORAWAN, &configuracion_app.flags_envio, 1);
  payload_add(buf_lorawan, &len_lorawan, PAYLOAD_MAX_LORAWAN, (const uint8_t*)&ultimas_medidas.bateria_mv, 2);  // siempre

  if (configuracion_app.flags_envio & ENVIO_AMBIENTE) {
    payload_add(buf_lorawan, &len_lorawan, PAYLOAD_MAX_LORAWAN, (const uint8_t*)&ultimas_medidas.presion,     2);
    payload_add(buf_lorawan, &len_lorawan, PAYLOAD_MAX_LORAWAN, (const uint8_t*)&ultimas_medidas.temperatura, 1);
    payload_add(buf_lorawan, &len_lorawan, PAYLOAD_MAX_LORAWAN, (const uint8_t*)&ultimas_medidas.ts_ambiente, 4);
  }
  if (configuracion_app.flags_envio & ENVIO_ANGULO) {
    payload_add(buf_lorawan, &len_lorawan, PAYLOAD_MAX_LORAWAN, (const uint8_t*)&ultimas_medidas.pitch,     2);
    payload_add(buf_lorawan, &len_lorawan, PAYLOAD_MAX_LORAWAN, (const uint8_t*)&ultimas_medidas.roll,      2);
    payload_add(buf_lorawan, &len_lorawan, PAYLOAD_MAX_LORAWAN, (const uint8_t*)&ultimas_medidas.ts_angulo, 4);
  }
  if (configuracion_app.flags_envio & ENVIO_GPS) {
    payload_add(buf_lorawan, &len_lorawan, PAYLOAD_MAX_LORAWAN, (const uint8_t*)&ultimas_medidas.gps_lat, 4);
    payload_add(buf_lorawan, &len_lorawan, PAYLOAD_MAX_LORAWAN, (const uint8_t*)&ultimas_medidas.gps_lon, 4);
    payload_add(buf_lorawan, &len_lorawan, PAYLOAD_MAX_LORAWAN, (const uint8_t*)&ultimas_medidas.ts_gps,  4);
  }
  if (configuracion_app.flags_envio & ENVIO_RANGING) {
    payload_add(buf_lorawan, &len_lorawan, PAYLOAD_MAX_LORAWAN, (const uint8_t*)&ultimas_medidas.ranging_len, 1);
    payload_add(buf_lorawan, &len_lorawan, PAYLOAD_MAX_LORAWAN, (const uint8_t*)ultimas_medidas.ranging, ultimas_medidas.ranging_len*sizeof(uint16_t));
    payload_add(buf_lorawan, &len_lorawan, PAYLOAD_MAX_LORAWAN, (const uint8_t*)ultimas_medidas.ranging_addr, ultimas_medidas.ranging_len * sizeof(uint32_t));
    payload_add(buf_lorawan, &len_lorawan, PAYLOAD_MAX_LORAWAN, (const uint8_t*)ultimas_medidas.ranging_rssi, ultimas_medidas.ranging_len);
    payload_add(buf_lorawan, &len_lorawan, PAYLOAD_MAX_LORAWAN, (const uint8_t*)&ultimas_medidas.ts_ranging,  4);
  }
  if (configuracion_app.flags_envio & ENVIO_BEACONS) {
    payload_add(buf_lorawan, &len_lorawan, PAYLOAD_MAX_LORAWAN, &ultimas_medidas.ble_logger_count, 1);
    for (int i = 0; i < ultimas_medidas.ble_logger_count; i++) {
      payload_add(buf_lorawan, &len_lorawan, PAYLOAD_MAX_LORAWAN, &ultimas_medidas.ble_logger_ids[i], 1);
      payload_add(buf_lorawan, &len_lorawan, PAYLOAD_MAX_LORAWAN, (uint8_t*)&ultimas_medidas.ble_logger_rssi[i], 1);
    }
    payload_add(buf_lorawan, &len_lorawan, PAYLOAD_MAX_LORAWAN, &ultimas_medidas.ble_beacon_count, 1);
    for (int i = 0; i < ultimas_medidas.ble_beacon_count; i++) {
      payload_add(buf_lorawan, &len_lorawan, PAYLOAD_MAX_LORAWAN, &ultimas_medidas.ble_beacon_ids[i], 1);
      payload_add(buf_lorawan, &len_lorawan, PAYLOAD_MAX_LORAWAN, (uint8_t*)&ultimas_medidas.ble_beacon_rssi[i], 1);
    }
    
    payload_add(buf_lorawan, &len_lorawan, PAYLOAD_MAX_LORAWAN, (const uint8_t*)&ultimas_medidas.ts_ble_scan, 4);
  }
  
  flash_get_ocupacion(&ultimas_medidas.flash_dir_lora, &ultimas_medidas.flash_dir_imu);

  ultimas_medidas.flash_descarte = flash_get_descarte();

  payload_add(buf_lorawan, &len_lorawan, PAYLOAD_MAX_LORAWAN, (const uint8_t*)&ultimas_medidas.reset_count, 2);
  payload_add(buf_lorawan, &len_lorawan, PAYLOAD_MAX_LORAWAN, (const uint8_t*)&ultimas_medidas.ttff_gps, 1);
  payload_add(buf_lorawan, &len_lorawan, PAYLOAD_MAX_LORAWAN, (const uint8_t*)&ultimas_medidas.flash_dir_lora, 4);
  payload_add(buf_lorawan, &len_lorawan, PAYLOAD_MAX_LORAWAN, (const uint8_t*)&ultimas_medidas.flash_dir_imu, 4);
  payload_add(buf_lorawan, &len_lorawan, PAYLOAD_MAX_LORAWAN, (const uint8_t*)&ultimas_medidas.actividad_eventos, 2);
  payload_add(buf_lorawan, &len_lorawan, PAYLOAD_MAX_LORAWAN, (const uint8_t*)&ultimas_medidas.flash_descarte, 1);

  ultimas_medidas.actividad_eventos = 0;
  ultimas_medidas.ttff_gps = 0;

  uint8_t mac[6];
  get_ble_mac(mac);
  payload_add(buf_lorawan, &len_lorawan, PAYLOAD_MAX_LORAWAN, mac, 6);
}

/**
 * @brief Función de procesamiento principal de la aplicación
 * @details Implementa la máquina de estados para gestionar sensores, GPS, BLE y transmisión
 */
static void app_process(void) {
  bool continuar = true;
  while (continuar) {
    continuar = false;
    switch (estado) {
      case ENCENDER_GPS:{  // Enciende el GPS y cambia al estado de escaneo de beacons
        actualizar_contadores(); // Actualizo los contadores de periodos

        if (flags_ciclo == 0 && !enviar_ciclo){ //Salida de FSM sino hay que medir o enviar
          uint32_t proximo = tiempo_hasta_proximo();
          duracion_ciclo = proximo;
          app_state_start_timer(proximo);
          break;
        }
      
        // Apago el advertising para evitar conexiones durante la maquina de estados
        app_ble_advertising_stop();

        gps_ts_inicio = get_timestamp();
        if((configuracion_app.flags & MODULO_GPS) && (flags_ciclo & MODULO_GPS)) gps_on();
        gps_fix_ciclo = false;

        estado = ENCENDER_ESCANER_BEACONS;
        continuar = true;
        break;
      }
      case ENCENDER_ESCANER_BEACONS:{  // Enciende el escáner de beacons y cambia al estado de apagar el escáner de beacons o ranging segun configuracion
        if ((configuracion_app.flags & MODULO_BEACONS) && (flags_ciclo & MODULO_BEACONS)) {
          ble_scan_start();
          estado = APAGAR_ESCANER_BEACONS;
          app_state_start_timer(configuracion_app.duracion_escaneo);
        } else {
          estado = RANGING;
          continuar = true;
        }
        break;
      }
      case APAGAR_ESCANER_BEACONS:{  // Apaga el escáner de beacons y cambia al estado de ranging
        ble_scan_stop();
        estado = RANGING;
        continuar = true;
        break;
      }
      case RANGING:{  // Ejecuta el ranging si esta habilitado y cambia al estado de comprobar fix
        if ((configuracion_app.flags & MODULO_RANGING) && (flags_ciclo & MODULO_RANGING)) {
          flags_globales.m_flag_ejecutar_ranging = true;
        } else {
          continuar = true;
        }
        estado = COMPROBAR_FIX;
        break;
      }
      case COMPROBAR_FIX:{
        if ((configuracion_app.flags & MODULO_GPS) && (flags_ciclo & MODULO_GPS)) {
          uint32_t gps_elapsed = get_timestamp() - gps_ts_inicio;
          if (flags_globales.m_flag_fix_valido || gps_elapsed >= 60) { // timeotu de 60 s
            // Si el fix fue obtenido en background, el GPS ya está apagado
            // Si llegamos aquí por timeout, apagar ahora
            if (!flags_globales.m_flag_fix_valido){
              gps_off();
              ultimas_medidas.ttff_gps = 60;
            }
            gps_fix_ciclo = flags_globales.m_flag_fix_valido;
            flags_globales.m_flag_fix_valido = false;

            estado = LECTURA_SENSORES;
            continuar = true;
          } else {
            app_state_start_timer(5);
          }
        } else {
            estado = LECTURA_SENSORES;
            continuar = true;
        }
        break;
      }
      case LECTURA_SENSORES:{  // Lee los sensores y cambia al estado de guardar en flash
        if ((configuracion_app.flags & MODULO_AMBIENTE) && (flags_ciclo & MODULO_AMBIENTE)) {
          medir_ambiente();
        }
        if ((configuracion_app.flags & MODULO_ANGULO) && (flags_ciclo & MODULO_ANGULO)) {
          medir_angulo();
        }
        if (flags_ciclo != 0 || enviar_ciclo){
          medir_bateria();
        }

        estado = GUARDAR_FLASH;
        continuar = true;
        break;
      }
      case GUARDAR_FLASH:{
        if(flags_ciclo != 0){ //Solo guardo si hay datos de sensores
          uint8_t flags_flash = flags_ciclo; // Por como esta definido el GPS, si no hay fix, se guarda un fix viejo con ts nuevo
          if((flags_flash & MODULO_GPS) && !gps_fix_ciclo){
            flags_flash &= ~MODULO_GPS; // Si no hubo fix, quitamos el GPS del ciclo
          }

          construir_payload_flash(flags_flash);
          flash_log_lora(buf_flash, len_flash);
        }

        if ((configuracion_app.flags & MODULO_LORAWAN) && enviar_ciclo) {
          estado = TRANSMISION;
          continuar = true;
        } else {
          estado = ENCENDER_GPS;
          uint32_t proximo = tiempo_hasta_proximo();
          duracion_ciclo = proximo;
          app_state_start_timer(proximo);
          app_ble_advertising_start();
        }
        break;
      }
      case TRANSMISION:{
        construir_uplink();
        send_frame(buf_lorawan, len_lorawan, false);

        estado = ENCENDER_GPS;
        uint32_t proximo = tiempo_hasta_proximo();
        duracion_ciclo = proximo;
        app_state_start_timer(proximo);
        app_ble_advertising_start();
        break;
      }
      default:{  // Estado por defecto
        estado = ENCENDER_GPS;
        uint32_t proximo = tiempo_hasta_proximo();
        duracion_ciclo = proximo;
        app_state_start_timer(proximo);
        break;
      }
    }
  }
}

/**
 * @brief Agrega datos a un buffer de payload, respetando su tamaño máximo
 * @param buf Buffer de destino
 * @param len Puntero a la longitud actual del buffer, se incrementa en @c n bytes si cabe
 * @param max Tamaño máximo del buffer
 * @param data Puntero a los datos a agregar
 * @param n Longitud de los datos a agregar, en bytes
 * @return true si los datos cupieron y se agregaron, false si se excedía @c max
 */
bool payload_add(uint8_t* buf, uint16_t *len, uint16_t max, const uint8_t* data, size_t n) {
  if (*len + n > max) return false;

  memcpy(&buf[*len], data, n);
  *len += n;
  return true;
}

/** @brief Gestiona la configuración de la aplicación */
static void gestionar_configuracion(void) {
  static bool lorawan_prev = false;  // Variable para detectar cambios en la configuración de LoRaWAN
  static bool init_done = false;     // Variable para detectar la primera ejecución

  // Si es la primera ejecución, inicializa la configuración
  if (!init_done) {
    lorawan_prev = (configuracion_app.flags & MODULO_LORAWAN) != 0;
    init_done = true;
  }

  // Si hay cambios en la configuración, actualiza los sensores y el módem LoRaWAN
  if (flags_globales.m_flag_guardar_config) {
    bool lorawan_new = (configuracion_app.flags & MODULO_LORAWAN) != 0;

    app_fds_store_save();
    configurar_sensores();
    flags_globales.m_flag_guardar_config = false;

    uint8_t ack_data[] = "ACK_CONFIG_UPDATE";
    app_enviar_datos_nus(ack_data, sizeof(ack_data)-1);

    memset(&contadores, 0, sizeof(contadores));
    duracion_ciclo = 1;
    tiempo_siguiente_envio = 1;

    // Si hay cambios en la configuración de LoRaWAN, reinicia el dispositivo
    if (lorawan_new != lorawan_prev) {
      flags_globales.m_flag_reiniciar = true;
    }

    lorawan_prev = lorawan_new;
  }
}

/** @brief Gestiona la conexión BLE */
static void gestionar_conexion_ble(void) {
  // Comprueba si hay una conexión BLE
  flags_globales.m_flag_ble_conectado = !app_ble_is_disconnected();

  // Si hay una conexión BLE y el dispositivo no está parado, entra en modo BLE
  if (flags_globales.m_flag_ble_conectado && !flags_globales.m_flag_dispositivo_parado) {
    entrar_modo_ble();
    flags_globales.m_flag_dispositivo_parado = true;
  } else if (!flags_globales.m_flag_ble_conectado && flags_globales.m_flag_dispositivo_parado) {
    if(!flags_globales.m_flag_borrado_flash){ //Para no reactivar sensores durante un borrado pendiente, se reactivan solo cuando la flash este lista para guardar
      // Si no hay una conexión BLE y el dispositivo está parado, sale del modo BLE
      salir_modo_ble();
      flags_globales.m_flag_dispositivo_parado = false;
    }
  }
}

/**
 * @brief Ejecuta el ranging de forma bloqueante y reprograma la máquina de estados
 * @param context Contexto RAL del radio, usado para las transacciones de ranging
 * @details Si LoRaWAN está habilitado, suspende la actividad del módem durante el
 * ranging para poder usar el radio directamente; si no, enciende y apaga el LR1110
 */
static void procesar_ranging(const void* context){
  flags_globales.m_flag_ejecutar_ranging = false;
  if (configuracion_app.flags & MODULO_LORAWAN) {
    modem_suspender_actividad();
    ranging(context);
    modem_reanudar_actividad();
  } else {
    encender_lr1110();
    ranging(context);
    apagar_lr1110();
  }
  app_state_start_timer(1);
}

/** @brief Suspender la actividad del módem */
void modem_suspender_actividad(void) {
  if (configuracion_app.flags & MODULO_LORAWAN) {
    smtc_modem_suspend_before_user_radio_access();
    nrf_drv_gpiote_in_event_disable(LR1110_IRQ_PIN);
  }
}

/** @brief Reanudar la actividad del módem */
void modem_reanudar_actividad(void) {
  if (configuracion_app.flags & MODULO_LORAWAN) {
    nrf_drv_gpiote_in_event_enable(LR1110_IRQ_PIN, true);
    smtc_modem_resume_after_user_radio_access();
  }
}

/** @brief Gestiona la entrada al modo BLE */
void entrar_modo_ble(void) {
  if (configuracion_app.flags & MODULO_GPS) gps_off();
  if (configuracion_app.flags & MODULO_BEACONS) ble_scan_stop();
  flags_globales.m_flag_ejecutar_ranging = false;
  estado = ENCENDER_GPS;
  
  // Si el IMU esta habilitado, se apaga
  if ((configuracion_app.flags & MODULO_IMU_XL) ||
      (configuracion_app.flags & MODULO_ANGULO) ||
      (configuracion_app.flags & MODULO_IMU_GY)) {
    imu_stop(true);
    flags_globales.m_flag_movimiento = false;
  }

  // Si LoRaWAN esta habilitado, se guarda el tiempo restante del temporizador del módem
  if (configuracion_app.flags & MODULO_LORAWAN) {
    smtc_modem_alarm_get_remaining_time(&tiempo_siguiente_envio);
    smtc_modem_alarm_clear_timer();
  } else {  // Si LoRaWAN no esta habilitado, se guarda el tiempo restante del temporizador del timer
    app_timer_stop(m_app_state_timer);

    uint32_t ticks_act = app_timer_cnt_get();
    uint32_t ticks_restantes = 0;

    ticks_restantes = app_timer_cnt_diff_compute(ticks_periodo_objetivo, ticks_act);
    uint32_t ticks_ciclo = (uint32_t)duracion_ciclo* APP_TIMER_CLOCK_FREQ;
    tiempo_siguiente_envio = (ticks_restantes <= ticks_ciclo) ? (ticks_restantes / APP_TIMER_CLOCK_FREQ) : 0;
  }
  modem_suspender_actividad();
}

/** @brief Gestiona la salida del modo BLE */
void salir_modo_ble(void) {
  // Si el IMU esta habilitado, se reanuda la actividad
  if ((configuracion_app.flags & MODULO_IMU_XL) ||
      (configuracion_app.flags & MODULO_ANGULO) ||
      (configuracion_app.flags & MODULO_IMU_GY)) {
    imu_stop(false);
  }
  // Se reanuda la actividad del módem (la propia funcion ya comprueba si LoRaWAN esta habilitado)
  modem_reanudar_actividad();

  // Se reanuda el temporizador de la app
  uint32_t tiempo_resume = tiempo_siguiente_envio + 1;
  duracion_ciclo = tiempo_resume;
  app_state_start_timer(tiempo_siguiente_envio + 1);
}

/**
 * @brief Sincroniza el timestamp del sistema
 * @param absolute_unix_timestamp_s Timestamp absoluto en segundos
 */
void sync_timestamp(uint32_t absolute_unix_timestamp_s) {
  uint32_t current_uptime_s = hal_rtc_get_time_s();  // Tiempo actual del sistema

  current_unix_offset = absolute_unix_timestamp_s - current_uptime_s;  // Diferencia entre el tiempo absoluto y el tiempo actual

  is_time_synchronized = true;  // Se marca como sincronizado
}

/**
 * @brief Obtiene el timestamp actual
 * @return Timestamp actual en segundos
 */
uint32_t get_timestamp(void) {
  if (!is_time_synchronized) {  // Si el tiempo no esta sincronizado, se devuelve 0
    return 0;
  }

  return hal_rtc_get_time_s() + current_unix_offset;  // Se devuelve el tiempo actual
}