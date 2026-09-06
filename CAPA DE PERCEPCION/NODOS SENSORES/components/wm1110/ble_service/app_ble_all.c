/**
 * @file app_ble_all.c
 * @brief Implementación completa del módulo BLE (advertising, NUS, escaneo)
 *
 * Gestiona:
 * - Pila BLE y configuración GAP/GATT
 * - Nordic UART Service (NUS) para comunicación bidireccional
 * - Advertising con un nombre de dispositivo personalizado
 * - Escaneo pasivo de beacons con filtrado por MAC
 * - Almacenamiento y transmisión de los beacons detectados
 */

#include "app_ble_all.h"
#include "app_ble_nus.h"
#include "app_error.h"
#include "app_timer.h"
#include "ble.h"
#include "ble_advdata.h"
#include "ble_advertising.h"
#include "ble_conn_params.h"
#include "ble_hci.h"
#include "ble_ots.h"
#include "main_lorawan.h"
#include "nordic_common.h"
#include "nrf_ble_gatt.h"
#include "nrf_ble_qwr.h"
#include "nrf_ble_scan.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
#include "nrf_pwr_mgmt.h"
#include "nrf_sdh.h"
#include "nrf_sdh_ble.h"
#include "nrf_sdh_soc.h"
#include "nrf_sdm.h"
#include "smtc_hal.h"
#include "main_flash.h"
#include "ble_cts_c.h"
#include "time.h"
#include "app_at_fds_datas.h"

#define APP_BLE_CONN_CFG_TAG                1 /*!< Etiqueta que identifica la configuración BLE del SoftDevice. */
#define APP_BLE_OBSERVER_PRIO               3 /*!< Prioridad del observador BLE de la aplicación. No debería ser necesario modificar este valor. */
#define APP_SOC_OBSERVER_PRIO               1 /*!< Prioridad del observador SoC de la aplicación. No debería ser necesario modificar este valor. */

#define APP_ADV_INTERVAL                    MSEC_TO_UNITS(5000, UNIT_0_625_MS)  /*! 5 segundos */
#define APP_ADV_DURATION                    0     /*! Duración infinita */

// Parámetros rápidos (descarga NUS)
#define MIN_CONN_INTERVAL                   MSEC_TO_UNITS(15, UNIT_1_25_MS)
#define MAX_CONN_INTERVAL                   MSEC_TO_UNITS(30,  UNIT_1_25_MS)
#define SLAVE_LATENCY                       0

#define CONN_SUP_TIMEOUT                    MSEC_TO_UNITS(6000, UNIT_10_MS) /*!< Timeout de supervisión de la conexión (6 segundos). */

#define FIRST_CONN_PARAMS_UPDATE_DELAY      APP_TIMER_TICKS(500)  /*!< Tiempo desde el evento de inicio (conexión o inicio de notificación) hasta la primera llamada a sd_ble_gap_conn_param_update (5 segundos). */
#define NEXT_CONN_PARAMS_UPDATE_DELAY       APP_TIMER_TICKS(5000) /*!< Tiempo entre cada llamada a sd_ble_gap_conn_param_update tras la primera (30 segundos). */
#define MAX_CONN_PARAMS_UPDATE_COUNT        3 /*!< Número de intentos antes de abandonar la negociación de parámetros de conexión. */

#define DEAD_BEEF                           0xDEADBEEF /*!< Valor usado como código de error en el volcado de pila; permite identificar la ubicación en el desenrollado de la pila. */

#define APP_SCAN_INTERVAL                   160 /*!< Intervalo de escaneo (en unidades de 0.625 ms). */
#define APP_SCAN_WINDOW                     160 /*!< Ventana de escaneo (en unidades de 0.625 ms). */
#define APP_SCAN_DURATION                   0   /*!< Duración del escaneo (en unidades de 10 ms). */

#define TIMEOUT_TX_NUS_MS                   5000

/*!< Parámetros de escaneo para el escaneo y la conexión. */
static ble_gap_scan_params_t m_scan_param = {
  .active = 0x00,
  .interval = APP_SCAN_INTERVAL,
  .window = APP_SCAN_WINDOW,
  .timeout = 0,
  .filter_policy = BLE_GAP_SCAN_FP_ACCEPT_ALL,
  .scan_phys = BLE_GAP_PHY_1MBPS,
};

BLE_CTS_C_DEF(m_cts_c); /*!< Instancia del servicio Current Time. */
BLE_DB_DISCOVERY_DEF(m_db_disc); /*!< Instancia de descubrimiento. */
BLE_NUS_DEF(m_nus, NRF_SDH_BLE_TOTAL_LINK_COUNT); /*!< Instancia del servicio BLE NUS. */
NRF_BLE_SCAN_DEF(m_scan); /*!< Instancia del módulo de escaneo. */
NRF_BLE_GATT_DEF(m_gatt); /*!< Instancia del módulo GATT. */
NRF_BLE_QWR_DEF(m_qwr); /*!< Contexto del módulo Queued Write.*/
BLE_ADVERTISING_DEF(m_advertising); /*!< Instancia del módulo de advertising. */
NRF_BLE_GQ_DEF(m_ble_gatt_queue, NRF_SDH_BLE_PERIPHERAL_LINK_COUNT, NRF_BLE_GQ_QUEUE_SIZE); /*!< Instancia de la cola GATT BLE. */

static uint16_t m_conn_handle = BLE_CONN_HANDLE_INVALID; /*!< Handle de la conexión actual. */

static uint8_t app_ble_state = BLE_GAP_EVT_DISCONNECTED; /*!< Variable que mantiene el estado actual de la conexión BLE. */

/** @brief Número de notificaciones/indicaciones NUS encoladas para envío */
static volatile uint32_t m_notif_encoladas = 0;
/** @brief Número de notificaciones/indicaciones NUS confirmadas por el SoftDevice */
static volatile uint32_t m_notif_confirmadas = 0;

/** @brief UUIDs relacionados con los datos de advertising. */
static ble_uuid_t m_adv_uuids[] = {
  {BLE_UUID_NUS_SERVICE, BLE_UUID_TYPE_VENDOR_BEGIN}, /*!< UUID del servicio NUS */
  {BLE_UUID_CURRENT_TIME_SERVICE, BLE_UUID_TYPE_BLE} /*!< UUID del servicio CTS */
};

/** @brief Identificador (YY) de cada Logger HOWL detectado en el escaneo en curso */
static uint8_t ble_logger_ids[MAX_LOGGERS_SAVED];
/** @brief RSSI máximo observado de cada Logger detectado, en dBm */
static int8_t  ble_logger_rssi[MAX_LOGGERS_SAVED];
/** @brief Contador de avistamientos de cada Logger detectado */
static uint8_t ble_logger_obs[MAX_LOGGERS_SAVED];
/** @brief Número de Loggers detectados en el escaneo en curso */
static uint8_t ble_logger_count = 0;

/** @brief Identificador (YY) de cada Beacon HOWL detectado en el escaneo en curso */
static uint8_t ble_beacon_ids[MAX_BEACONS_SAVED];
/** @brief RSSI máximo observado de cada Beacon detectado, en dBm */
static int8_t  ble_beacon_rssi[MAX_BEACONS_SAVED];
/** @brief Contador de avistamientos de cada Beacon detectado */
static uint8_t ble_beacon_obs[MAX_BEACONS_SAVED];
/** @brief Payload de advertising bruto de cada Beacon detectado */
static uint8_t ble_beacon_data[MAX_BEACONS_SAVED][MAX_BEACON_SIZE];
/** @brief Número de Beacons detectados en el escaneo en curso */
static uint8_t ble_beacon_count = 0;

/** @brief Timestamp Unix de la última desconexión, usado en el paquete de advertising */
static uint32_t m_last_connection_timestamp = 0;
/** @brief Service data del advertising, con el timestamp de última conexión */
ble_advdata_service_data_t m_service_data;
/** @brief Buffer del timestamp de última conexión, serializado para el advertising */
static uint8_t m_paquete_advertising[4];

/** @brief Función para iniciar el advertising.
 *
 * @param   p_erase_bonds   No usado.
 */
static void advertising_start(void *p_erase_bonds);

/** @brief Función de callback para la macro de assert.
 *
 * @details Esta función se llama en caso de un assert en el SoftDevice.
 *
 * @warning Este manejador es solo un ejemplo y no es apto para un producto final.
 * Es necesario analizar cómo debe reaccionar el producto ante un Assert.
 * @warning Ante un assert del SoftDevice, el sistema solo puede recuperarse con un reset.

 * @param   line_num   Número de línea de la llamada ASSERT que falló.
 * @param   p_file_name   Nombre del archivo de la llamada ASSERT que falló.
 */
void assert_nrf_callback(uint16_t line_num, const uint8_t *p_file_name) {
  NRF_LOG_ERROR("ASSERT en linea %d del archivo %s", line_num, (uint32_t)p_file_name);
  NRF_LOG_FLUSH();
  hal_mcu_wait_ms(500);
  app_error_handler(DEAD_BEEF, line_num, p_file_name);
}

/** @brief Función para gestionar los errores del servicio Current Time.
 *
 * @param[in]  nrf_error  Código de error con información sobre el fallo.
 */
static void current_time_error_handler(uint32_t nrf_error){
  APP_ERROR_HANDLER(nrf_error);
}

/** @brief Función para gestionar el evento de desconexión.
 *
 * @param   conn_handle   Handle de conexión en el que ocurrió el evento de desconexión.
 * @param   p_context   No usado.
 */
static void disconnect(uint16_t conn_handle, void *p_context) {
  UNUSED_PARAMETER(p_context);

  ret_code_t err_code = sd_ble_gap_disconnect(conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
  if (err_code != NRF_SUCCESS) {
    NRF_LOG_WARNING("Failed to disconnect connection. Connection handle: %d Error: %d", conn_handle, err_code);
  } else {
    NRF_LOG_DEBUG("Disconnected connection handle %d", conn_handle);
  }
}

/** @brief Función para construir la configuración de modos de advertising.
 *
 * @param   p_config   Configuración de modos de advertising a rellenar.
 */
static void advertising_config_get(ble_adv_modes_config_t *p_config) {
  memset(p_config, 0, sizeof(ble_adv_modes_config_t));

  p_config->ble_adv_fast_enabled = true;
  p_config->ble_adv_fast_interval = APP_ADV_INTERVAL;
  p_config->ble_adv_fast_timeout = APP_ADV_DURATION;
  p_config->ble_adv_slow_enabled = false;
  p_config->ble_adv_slow_interval = APP_ADV_INTERVAL;
  p_config->ble_adv_slow_timeout = APP_ADV_DURATION;
}

/** @brief Función para la inicialización de GAP.
 *
 * @details Esta función configura todos los parámetros GAP (Generic Access
 * Profile) necesarios del dispositivo, incluyendo el nombre del dispositivo,
 * su apariencia y los parámetros de conexión preferidos.
 */
static void gap_params_init(void) {
  uint32_t err_code;
  ble_gap_conn_params_t gap_conn_params;
  ble_gap_conn_sec_mode_t sec_mode;
  uint8_t adv_device_name[24] = {0};
  sprintf(adv_device_name, "HOWLLO04");

  ble_gap_addr_t new_mac_addr;
  new_mac_addr.addr_type = BLE_GAP_ADDR_TYPE_PUBLIC;

  new_mac_addr.addr[0] = 0x04; // LSB
  new_mac_addr.addr[1] = 0x4C;
  new_mac_addr.addr[2] = 0x4C;
  new_mac_addr.addr[3] = 0x57;
  new_mac_addr.addr[4] = 0x4F;
  new_mac_addr.addr[5] = 0x48; // MSB  

  err_code = sd_ble_gap_addr_set(&new_mac_addr);
  APP_ERROR_CHECK(err_code);

  BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);
  err_code = sd_ble_gap_device_name_set(&sec_mode, (const uint8_t *)adv_device_name, strlen(adv_device_name));
  APP_ERROR_CHECK(err_code);

  memset(&gap_conn_params, 0, sizeof(gap_conn_params));

  gap_conn_params.min_conn_interval = MIN_CONN_INTERVAL;
  gap_conn_params.max_conn_interval = MAX_CONN_INTERVAL;
  gap_conn_params.slave_latency =     SLAVE_LATENCY;
  gap_conn_params.conn_sup_timeout =  CONN_SUP_TIMEOUT;

  err_code = sd_ble_gap_ppcp_set(&gap_conn_params);
  APP_ERROR_CHECK(err_code);
}

/** @brief Función para gestionar los errores del módulo Queued Write.
 *
 * @details Se pasa un puntero a esta función a cada servicio que pueda
 * necesitar informar a la aplicación de un error.
 *
 * @param   nrf_error   Código de error con información sobre el fallo.
 */
static void nrf_qwr_error_handler(uint32_t nrf_error) {
  APP_ERROR_HANDLER(nrf_error);
}

/** @brief Manejador de eventos NUS (Nordic UART Service)
 *
 * @param   p_evt   Evento NUS recibido
 * @details Procesa los comandos recibidos:
 *   - 0x01: Actualizar configuración
 *   - 0x02: Solicitar descarga de datos
 *   - 0x03: Leer configuración actual
 *   - 0x04: Reiniciar el MCU
 *   - 0x05: Borrar flash
 */
static void nus_data_handler(ble_nus_evt_t *p_evt) {
  uint32_t err_code;
  const uint8_t *p_data = p_evt->params.rx_data.p_data;
  uint16_t length = p_evt->params.rx_data.length;
  switch (p_evt->type) {
  case BLE_NUS_EVT_RX_DATA:
    if (p_data[0] == 0x01) { // Reconfigurar
      if (length == (sizeof(logger_config_t) + 1)) {
        logger_config_t nueva_config;
        memcpy(&nueva_config, p_evt->params.rx_data.p_data + 1, sizeof(logger_config_t));
        
        config_error_t error;

        if(validar_configuracion(&nueva_config, &error)){
          configuracion_app = nueva_config;
          flags_globales.m_flag_guardar_config = true;
        }else{
          const char *msg = NULL;
          switch (error) {
            case CONFIG_ERR_PERIODO_MIN: 
              msg = "NACK_PERIODO_MIN"; 
              break;
            case CONFIG_ERR_ESCANEO: 
              msg = "NACK_ESCANEO"; 
              break;
            case CONFIG_ERR_IMU: 
              msg = "NACK_IMU"; 
              break;
            case CONFIG_ERR_RANGING: 
              msg = "NACK_RANGING"; 
              break;
            default: 
              msg = "NACK_CONFIG"; 
              break;
          }
        
          app_enviar_datos_nus((uint8_t*)msg, strlen(msg));
        }
      } else {
        PRINTF("Error: Tamaño config incorrecto (Esperado: %d, Recibido: %d)\n", sizeof(logger_config_t) + 1, length);
      }
    } else if (p_data[0] == 0x02) { // Descargar Flash
        flags_globales.m_flag_descargar_datos = true;
    } else if (p_data[0] == 0x03) { // Enviar configuracion
        uint32_t addr_lora, addr_imu;
        flash_get_ocupacion(&addr_lora, &addr_imu);

        uint16_t resp_len = 1 + sizeof(logger_config_t) + 8; // 1 cmd + config + 4 lora + 4 imu
        uint8_t resp_buffer[resp_len];
        memset(resp_buffer, 0, resp_len);

        resp_buffer[0] = 0x03;
        memcpy(&resp_buffer[1], &configuracion_app, sizeof(logger_config_t));

        uint16_t offset = 1 + sizeof(logger_config_t);
        memcpy(&resp_buffer[offset],     &addr_lora, 4);
        memcpy(&resp_buffer[offset + 4], &addr_imu,  4);

        app_enviar_datos_nus(resp_buffer, resp_len);

        uint8_t ack_data[] = "ACK_SEND_COMPLETE";
        app_enviar_datos_nus(ack_data, sizeof(ack_data)-1);
    } else if (p_data[0] == 0x04) { // Reiniciar placa
        flags_globales.m_flag_reiniciar = true;
    } else if (p_data[0] == 0x05){ // Borrar flash
        puntero_borrado = ADDR_LORA_START;
        flags_globales.m_flag_borrado_flash = true;
    } else if (p_data[0] == 0x06){ // Sincronizar hora
        uint32_t timestamp_ble_manual = 0;
        memcpy(&timestamp_ble_manual, p_evt->params.rx_data.p_data + 1, sizeof(timestamp_ble_manual));
        sync_timestamp(timestamp_ble_manual);

        uint8_t ack_data[] = "ACK_TIME_SYNC";
        app_enviar_datos_nus(ack_data, sizeof(ack_data)-1);
    } else if (p_data[0] == 0x07){ // Confirmacion/repeticion sector (descarga confirmada)
        flags_globales.m_sector_ack_status = p_data[1];
        flags_globales.m_flag_sector_ack_recibido = true;
    } else {
      PRINTF("Comando incorrecto. Recibido: %02X\n", p_data[0]);
    }
    break;

  case BLE_NUS_EVT_INDICATE_CONFIRM:
    flags_globales.m_flag_indication_confirmada = true;
    break;

  default:
    break;
  }
}

/** @brief Función para gestionar los eventos del cliente del servicio Current Time.
 *
 * @details Esta función se llama para todos los eventos del cliente del
 *          servicio Current Time que se pasan a la aplicación.
 *
 * @param[in] p_evt Evento recibido del cliente del servicio Current Time.
 */
static void on_cts_c_evt(ble_cts_c_t * p_cts, ble_cts_c_evt_t * p_evt){
  ret_code_t err_code;

  switch (p_evt->evt_type){
    case BLE_CTS_C_EVT_DISCOVERY_COMPLETE:
      NRF_LOG_INFO("Current Time Service discovered on server.");
      err_code = ble_cts_c_handles_assign(&m_cts_c, p_evt->conn_handle, &p_evt->params.char_handles);
      APP_ERROR_CHECK(err_code);
      ble_cts_c_current_time_read(&m_cts_c);
      break;

    case BLE_CTS_C_EVT_DISCOVERY_FAILED:
      NRF_LOG_INFO("Current Time Service not found on server. ");
      break;

    case BLE_CTS_C_EVT_DISCONN_COMPLETE:
      NRF_LOG_INFO("Disconnect Complete.");
      break;

    case BLE_CTS_C_EVT_CURRENT_TIME:{
      NRF_LOG_INFO("Current Time received.");
      ble_date_time_t *dt = &p_evt->params.current_time.exact_time_256.day_date_time.date_time;

      struct tm cts_t = {0};

      cts_t.tm_year = dt->year - 1900;
      cts_t.tm_mon = dt->month - 1; 
      cts_t.tm_mday = dt->day;
      cts_t.tm_hour = dt->hours;
      cts_t.tm_min = dt->minutes;
      cts_t.tm_sec = dt->seconds;
      cts_t.tm_isdst = 0;

      time_t unix_cts_ts = mktime(&cts_t);
      sync_timestamp((uint32_t)unix_cts_ts);
      break;
    }
    case BLE_CTS_C_EVT_INVALID_TIME:
      NRF_LOG_INFO("Invalid Time received.");
      break;

    default:
      break;
  }
}

/** @brief Función para inicializar los servicios que usará la aplicación.
 *
 * @details Inicializa el módulo Queued Write y el servicio Nordic UART.
 */
static void services_init(void) {
  uint32_t err_code;
  nrf_ble_qwr_init_t qwr_init = {0};
  ble_nus_init_t     nus_init;
  ble_cts_c_init_t   cts_init = {0};

  // Inicializa el módulo Queued Write.
  qwr_init.error_handler = nrf_qwr_error_handler;
  err_code = nrf_ble_qwr_init(&m_qwr, &qwr_init);
  APP_ERROR_CHECK(err_code);

  // Inicializa el NUS.
  memset(&nus_init, 0, sizeof(nus_init));

  nus_init.data_handler = nus_data_handler;

  err_code = ble_nus_init(&m_nus, &nus_init);
  APP_ERROR_CHECK(err_code);

  // Inicializa el CTS.
  cts_init.evt_handler   = on_cts_c_evt;
  cts_init.error_handler = current_time_error_handler;
  cts_init.p_gatt_queue  = &m_ble_gatt_queue;
  err_code               = ble_cts_c_init(&m_cts_c, &cts_init);
  APP_ERROR_CHECK(err_code);
}

/** @brief Función para gestionar los eventos de Database Discovery.
 *
 * @details Esta función es un callback que gestiona los eventos del módulo de
 *          descubrimiento de base de datos. Según los UUIDs descubiertos,
 *          reenvía los eventos a sus respectivas instancias de servicio.
 *
 * @param[in] p_event  Puntero al evento de descubrimiento de base de datos.
 */
static void db_disc_handler(ble_db_discovery_evt_t * p_evt){
  ble_cts_c_on_db_disc_evt(&m_cts_c, p_evt);
}

/**
 * @brief Inicialización del recolector de descubrimiento de base de datos.
 */
static void db_discovery_init(void){
  ble_db_discovery_init_t db_init;

  memset(&db_init, 0, sizeof(ble_db_discovery_init_t));

  db_init.evt_handler  = db_disc_handler;
  db_init.p_gatt_queue = &m_ble_gatt_queue;

  ret_code_t err_code = ble_db_discovery_init(&db_init);
  APP_ERROR_CHECK(err_code);
}

/** @brief Función para gestionar el módulo de parámetros de conexión.
 *
 * @details Esta función se llama para todos los eventos del módulo de
 * parámetros de conexión que se pasan a la aplicación.
 *          @note Lo único que hace esta función es desconectar. Esto podría
 * haberse hecho simplemente con el parámetro de configuración
 * disconnect_on_fail, pero en su lugar se usa el mecanismo de manejador de
 * eventos para mostrar su uso.
 *
 * @param   p_evt   Evento recibido del módulo de parámetros de conexión.
 */
static void on_conn_params_evt(ble_conn_params_evt_t *p_evt) {
  uint32_t err_code;

  if (p_evt->evt_type == BLE_CONN_PARAMS_EVT_FAILED) {
    err_code = sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_CONN_INTERVAL_UNACCEPTABLE);
    APP_ERROR_CHECK(err_code);
  }
}

/** @brief Función para gestionar un error de parámetros de conexión.
 *
 * @param   nrf_error   Código de error con información sobre el fallo.
 */
static void conn_params_error_handler(uint32_t nrf_error) {
  APP_ERROR_HANDLER(nrf_error);
}

/** @brief Función para inicializar el módulo de parámetros de conexión.
 */
static void conn_params_init(void) {
  uint32_t err_code;
  ble_conn_params_init_t cp_init;

  memset(&cp_init, 0, sizeof(cp_init));

  cp_init.p_conn_params = NULL;
  cp_init.first_conn_params_update_delay = FIRST_CONN_PARAMS_UPDATE_DELAY;
  cp_init.next_conn_params_update_delay = NEXT_CONN_PARAMS_UPDATE_DELAY;
  cp_init.max_conn_params_update_count = MAX_CONN_PARAMS_UPDATE_COUNT;
  cp_init.start_on_notify_cccd_handle = BLE_GATT_HANDLE_INVALID;
  cp_init.disconnect_on_fail = false;
  cp_init.evt_handler = on_conn_params_evt;
  cp_init.error_handler = conn_params_error_handler;

  err_code = ble_conn_params_init(&cp_init);
  APP_ERROR_CHECK(err_code);
}

/** @brief Función para poner el chip en modo sueño.
 *
 * @note Esta función no retorna.
 */
static void sleep_mode_enter(void) { 
  nrf_pwr_mgmt_run(); 
}

/** @brief Función para gestionar los eventos de advertising.
 *
 * @details Esta función se llama para los eventos de advertising que se pasan
 * a la aplicación.
 *
 * @param   ble_adv_evt   Evento de advertising.
 */
static void on_adv_evt(ble_adv_evt_t ble_adv_evt) {
  uint32_t err_code;

  switch (ble_adv_evt) {
    case BLE_ADV_EVT_FAST:
      NRF_LOG_INFO("BLE_ADV_EVT_FAST\r\n");
    break;

    case BLE_ADV_EVT_IDLE:
      sleep_mode_enter();
    break;

    default:
    break;
  }
}

/** @brief Reinicia el buffer de almacenamiento de dispositivos
 *
 * @details Pone a cero los contadores y limpia los arrays de dispositivos
 */
void device_storage_reset(void) {
  ble_logger_count = 0;
  ble_beacon_count = 0;
  memset(ble_logger_ids, 0, sizeof(ble_logger_ids));
  memset(ble_logger_rssi, 0, sizeof(ble_logger_rssi));
  memset(ble_logger_obs, 0, sizeof(ble_logger_obs));
  memset(ble_beacon_ids, 0, sizeof(ble_beacon_ids));
  memset(ble_beacon_rssi, 0, sizeof(ble_beacon_rssi));
  memset(ble_beacon_obs, 0, sizeof(ble_beacon_obs));
  memset(ble_beacon_data, 0, sizeof(ble_beacon_data));

}

/** @brief Registra un paquete HOWL detectado en el escaneo (Logger o Beacon)
 *
 * @param   data          Payload de advertising del dispositivo.
 * @param   rssi          RSSI del paquete recibido.
 * @param   device_type   Tipo de dispositivo HOWL (@c HOWL_TYPE_LOGGER o @c HOWL_TYPE_BEACON).
 * @param   device_id     Identificador (YY) del dispositivo.
 *
 * @details Si el dispositivo ya se había visto en este escaneo, actualiza su
 * contador de avistamientos y el RSSI máximo; si no, lo añade a la lista
 * correspondiente si aún queda espacio.
 */
void device_process_packet(uint8_t *data, int8_t rssi, uint8_t device_type, uint8_t device_id) {
  if(device_type == HOWL_TYPE_LOGGER){
    // Gestion de duplicados (para no añadir si ya se ha visto y simplemente actualizar)
    for (int i = 0; i < ble_logger_count; i++){
      if(ble_logger_ids[i] == device_id){
        ble_logger_obs[i]++;
        if (rssi > ble_logger_rssi[i]) ble_logger_rssi[i] = rssi;
        return;
      }
    }
    if(ble_logger_count >= MAX_LOGGERS_SAVED) return; // Si ya se han visto todos los loggers esperados, no añadir, es un error
    
    // Si pasa los filtros, añadir el dispositivo a la lista
    ble_logger_ids[ble_logger_count] = device_id;
    ble_logger_rssi[ble_logger_count] = rssi;
    ble_logger_obs[ble_logger_count] = 1;
    ble_logger_count++;
  }else if (device_type == HOWL_TYPE_BEACON){
    uint8_t len = data[0];
    if(len + 1 > MAX_BEACON_SIZE) len = MAX_BEACON_SIZE - 1;

    // Gestion de duplicados (para no añadir si ya se ha visto y simplemente actualizar)
    for(int i = 0; i < ble_beacon_count; i++){
      if(ble_beacon_ids[i] == device_id){
        ble_beacon_obs[i]++;
        if (rssi > ble_beacon_rssi[i]) ble_beacon_rssi[i] = rssi;
        return;
      }
    }
    if(ble_beacon_count >= MAX_BEACONS_SAVED) return; // Si ya se han visto todos los BEACONS esperados, no añadir, es un error
    
    // Si pasa los filtros, añadir el dispositivo a la lista
    ble_beacon_ids[ble_beacon_count] = device_id;
    ble_beacon_rssi[ble_beacon_count] = rssi;
    ble_beacon_obs[ble_beacon_count] = 1;
    memcpy(ble_beacon_data[ble_beacon_count], data, len+1);
    ble_beacon_count++;
  }
}

/** @brief Vuelca los Loggers/Beacons detectados en el escaneo a @c ultimas_medidas
 *
 * @details Copia los buffers locales de dispositivos detectados a
 * @c ultimas_medidas y marca el timestamp del escaneo, para que queden
 * disponibles al construir los payloads de flash/uplink.
 */
void device_storage_save(void) {
  ultimas_medidas.ble_logger_count = ble_logger_count;
  memcpy(ultimas_medidas.ble_logger_ids, ble_logger_ids, ble_logger_count);
  memcpy(ultimas_medidas.ble_logger_rssi, ble_logger_rssi, ble_logger_count);
  memcpy(ultimas_medidas.ble_logger_obs, ble_logger_obs, ble_logger_count);

  ultimas_medidas.ble_beacon_count = ble_beacon_count;
  memcpy(ultimas_medidas.ble_beacon_ids, ble_beacon_ids, ble_beacon_count);
  memcpy(ultimas_medidas.ble_beacon_rssi, ble_beacon_rssi, ble_beacon_count);
  memcpy(ultimas_medidas.ble_beacon_obs, ble_beacon_obs, ble_beacon_count);
  memcpy(ultimas_medidas.ble_beacon_data, ble_beacon_data, ble_beacon_count * MAX_BEACON_SIZE);
  ultimas_medidas.ts_ble_scan = get_timestamp();
}

/** @brief Actualiza el service data del advertising con el timestamp de última conexión
 *
 * @details Serializa @c m_last_connection_timestamp en @c m_paquete_advertising
 * y refresca los datos de advertising activos con ese service data.
 */
static void update_timestamp_advertising(void){
  m_paquete_advertising[0] = (m_last_connection_timestamp >> 0)  & 0xFF;
  m_paquete_advertising[1] = (m_last_connection_timestamp >> 8)  & 0xFF;
  m_paquete_advertising[2] = (m_last_connection_timestamp >> 16) & 0xFF;
  m_paquete_advertising[3] = (m_last_connection_timestamp >> 24) & 0xFF;
  
  m_service_data.service_uuid = 0x1234;
  m_service_data.data.size = sizeof(m_paquete_advertising);
  m_service_data.data.p_data = m_paquete_advertising;

  ble_advdata_t advdata;
  memset(&advdata, 0, sizeof(advdata));

  advdata.name_type            = BLE_ADVDATA_FULL_NAME;
  advdata.p_service_data_array = &m_service_data;
  advdata.service_data_count   = 1;

  ble_advertising_advdata_update(&m_advertising, &advdata, NULL);
}

/** @brief Función para gestionar los eventos de la pila BLE.
 *
 * @param   p_ble_evt   Evento de la pila Bluetooth.
 * @param   p_context   No usado.
 */
static void ble_evt_handler(ble_evt_t const *p_ble_evt, void *p_context) {
  ret_code_t err_code;
  uint32_t last, now = 0;
  ble_gap_evt_t const *p_gap_evt = &p_ble_evt->evt.gap_evt;

  uint32_t rssi = 0;

  switch (p_ble_evt->header.evt_id) {
    case BLE_GAP_EVT_ADV_REPORT: {
      ble_gap_evt_adv_report_t const *p_adv_report = &p_gap_evt->params.adv_report;
      ble_gap_addr_t const *p_peer_addr = &p_adv_report->peer_addr;

      if (p_peer_addr->addr[5] == 0x48 && // 'H'
          p_peer_addr->addr[4] == 0x4F && // 'O'
          p_peer_addr->addr[3] == 0x57 && // 'W'
          p_peer_addr->addr[2] == 0x4C)   // 'L'
      {

        device_process_packet((uint8_t *)p_adv_report->data.p_data, p_adv_report->rssi, p_peer_addr->addr[1],p_peer_addr-> addr[0]);

        break;
      }

      break;
    }
    case BLE_GAP_EVT_DISCONNECTED:
      app_ble_state = BLE_GAP_EVT_DISCONNECTED;
      NRF_LOG_DEBUG("BLE_GAP_EVT_DISCONNECTED\r\n");
    
      m_last_connection_timestamp = get_timestamp();
      update_timestamp_advertising();

      flags_globales.m_flag_descargar_datos = false;

      m_conn_handle = BLE_CONN_HANDLE_INVALID;
      break;

    case BLE_GAP_EVT_CONNECTED:
      app_ble_state = BLE_GAP_EVT_CONNECTED;
      NRF_LOG_DEBUG("BLE_GAP_EVT_CONNECTED\r\n");
      m_conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
      err_code = nrf_ble_qwr_conn_handle_assign(&m_qwr, m_conn_handle);
      APP_ERROR_CHECK(err_code);
      ble_db_discovery_start(&m_db_disc, m_conn_handle);

      m_notif_confirmadas = 0;
      m_notif_encoladas = 0;
      break;

    case BLE_GATTS_EVT_SYS_ATTR_MISSING:
      err_code = sd_ble_gatts_sys_attr_set(m_conn_handle, NULL, 0, 0);
      APP_ERROR_CHECK(err_code);
      break;
    
    case BLE_GATTS_EVT_HVN_TX_COMPLETE:
      m_notif_confirmadas += p_ble_evt->evt.gatts_evt.params.hvn_tx_complete.count;
      break;

    case BLE_GAP_EVT_PHY_UPDATE_REQUEST:
      NRF_LOG_DEBUG("PHY update request.");
      ble_gap_phys_t const phys2 = {
          .rx_phys = BLE_GAP_PHY_AUTO,
          .tx_phys = BLE_GAP_PHY_AUTO,
      };
      err_code = sd_ble_gap_phy_update(p_ble_evt->evt.gap_evt.conn_handle, &phys2);
      APP_ERROR_CHECK(err_code);
      break;

    case BLE_GATTC_EVT_TIMEOUT:
      // Desconecta ante un timeout del cliente GATT.
      NRF_LOG_DEBUG("GATT Client Timeout.");
      err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gattc_evt.conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
      APP_ERROR_CHECK(err_code);
      break;

    case BLE_GATTS_EVT_TIMEOUT:
      // Desconecta ante un timeout del servidor GATT.
      NRF_LOG_DEBUG("GATT Server Timeout.");
      err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gatts_evt.conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
      APP_ERROR_CHECK(err_code);
      break;

    default:
      break;
  }
}

/** @brief Función para gestionar los eventos del módulo de escaneo.
 *
 * @param   p_scan_evt   Evento de escaneo.
 */
static void scan_evt_handler(scan_evt_t const *p_scan_evt) {
  ble_gap_evt_adv_report_t const *p_adv;

  switch (p_scan_evt->scan_evt_id) {
    case NRF_BLE_SCAN_EVT_FILTER_MATCH:
      p_adv = p_scan_evt->params.filter_match.p_adv_report;
    break;

    case NRF_BLE_SCAN_EVT_NOT_FOUND:
      p_adv = p_scan_evt->params.p_not_found;
    break;
    case NRF_BLE_SCAN_EVT_SCAN_TIMEOUT:
    break;

    default:
    break;
  }
}

/** @brief Manejador de eventos SoC del SoftDevice.
 *
 * @param   evt_id   Evento SoC.
 * @param   p_context   Contexto.
 */
static void soc_evt_handler( uint32_t evt_id, void * p_context ){
  switch( evt_id ){
    default:
    break;
  }
}

/** @brief Función para inicializar la pila BLE.
 *
 * @details Inicializa el SoftDevice y la interrupción de eventos BLE.
 */
static void ble_stack_init(void) {
  ret_code_t err_code;

  err_code = nrf_sdh_enable_request();
  APP_ERROR_CHECK(err_code);

  // Configura la pila BLE con los ajustes por defecto.
  // Obtiene la dirección de inicio de la RAM de la aplicación.
  uint32_t ram_start = 0;
  err_code = nrf_sdh_ble_default_cfg_set(APP_BLE_CONN_CFG_TAG, &ram_start);
  APP_ERROR_CHECK(err_code);

  ble_cfg_t ble_cfg;
  memset(&ble_cfg, 0, sizeof(ble_cfg));

  ble_cfg.conn_cfg.conn_cfg_tag = APP_BLE_CONN_CFG_TAG;
  ble_cfg.conn_cfg.params.gatts_conn_cfg.hvn_tx_queue_size = 12;

  err_code = sd_ble_cfg_set(BLE_CONN_CFG_GATTS, &ble_cfg, ram_start);
  APP_ERROR_CHECK(err_code);

  // Habilita la pila BLE.
  err_code = nrf_sdh_ble_enable(&ram_start);
  APP_ERROR_CHECK(err_code);

  // Registra los manejadores de eventos BLE y SoC.
  NRF_SDH_BLE_OBSERVER(m_ble_observer, APP_BLE_OBSERVER_PRIO, ble_evt_handler, NULL);
  NRF_SDH_SOC_OBSERVER(m_soc_observer, APP_SOC_OBSERVER_PRIO, soc_evt_handler, NULL);
}

/** @brief Función para inicializar el escaneo y configurar los filtros.
 *
 * @details Inicializa el módulo de escaneo y configura los filtros para
 * buscar paquetes de advertising específicos.
 */
static void scan_init(void) {
  ret_code_t err_code;
  nrf_ble_scan_init_t init_scan;

  memset(&init_scan, 0, sizeof(init_scan));

  init_scan.connect_if_match = false;
  init_scan.conn_cfg_tag = APP_BLE_CONN_CFG_TAG;
  init_scan.p_scan_param = &m_scan_param;

  err_code = nrf_ble_scan_init(&m_scan, &init_scan, scan_evt_handler);
  APP_ERROR_CHECK(err_code);
}

/** @brief Función para iniciar el módulo de escaneo.
 *
 * @details Esta función configura los filtros e inicia el escaneo.
 */
void ble_scan_start(void) {
  ret_code_t err_code;

  uint16_t duty = configuracion_app.duty_escaneo;
  uint32_t window = (uint32_t)m_scan_param.interval * duty/100;

  if(window < 4) window = 4; // El minimo del SD es de 2.5 ms
  if(window > m_scan_param.interval) window = m_scan_param.interval;

  m_scan_param.window = window;
  m_scan_param.timeout = (uint16_t)(configuracion_app.duracion_escaneo + 5)*100; // Unidades de 10 ms -> Margen de 5 s

  err_code = nrf_ble_scan_params_set(&m_scan, &m_scan_param);
  APP_ERROR_CHECK(err_code);

  err_code = nrf_ble_scan_start(&m_scan);
  APP_ERROR_CHECK(err_code);
}

/** @brief Función para detener el módulo de escaneo.
 *
 * @details Esta función detiene el escaneo y procesa los beacons almacenados.
 */
void ble_scan_stop(void) {
  nrf_ble_scan_stop();

  device_storage_save();
  device_storage_reset();
}

/** @brief Función para inicializar la funcionalidad de Advertising.
 *
 * @details Inicializa el advertising y sus manejadores de eventos.
 */
static void advertising_init(void) {
  uint32_t err_code;
  ble_advertising_init_t     init;

  memset(&init, 0, sizeof(init));

  init.advdata.name_type = BLE_ADVDATA_FULL_NAME;
  init.advdata.include_appearance = false;
  init.advdata.uuids_complete.uuid_cnt = sizeof(m_adv_uuids) / sizeof(m_adv_uuids[0]);
  init.advdata.uuids_complete.p_uuids = m_adv_uuids;

  advertising_config_get(&init.config);

  init.evt_handler = on_adv_evt;

  err_code = ble_advertising_init(&m_advertising, &init);
  APP_ERROR_CHECK(err_code);

  ble_advertising_conn_cfg_tag_set(&m_advertising, APP_BLE_CONN_CFG_TAG);
}

/** @brief Función para gestionar los eventos de la librería GATT.
 *
 * @param   p_gatt   Instancia del módulo GATT.
 * @param   p_evt    Evento GATT.
 */
void gatt_evt_handler(nrf_ble_gatt_t *p_gatt, nrf_ble_gatt_evt_t const *p_evt) {
  if ((m_conn_handle == p_evt->conn_handle) && (p_evt->evt_id == NRF_BLE_GATT_EVT_ATT_MTU_UPDATED)) { }

  NRF_LOG_DEBUG("ATT MTU exchange completed. central 0x%x peripheral 0x%x", p_gatt->att_mtu_desired_central,  p_gatt->att_mtu_desired_periph);
}

/** @brief Función para inicializar el módulo GATT.
 *
 * @details El módulo GATT gestiona automáticamente los procedimientos de
 * actualización de ATT_MTU y Data Length.
 */
static void gatt_init(void) {
  ret_code_t err_code = nrf_ble_gatt_init(&m_gatt, NULL);
  // ret_code_t err_code = nrf_ble_gatt_init( &m_gatt, gatt_evt_handler );
  APP_ERROR_CHECK(err_code);
  err_code = nrf_ble_gatt_att_mtu_periph_set(&m_gatt, NRF_SDH_BLE_GATT_MAX_MTU_SIZE);
  APP_ERROR_CHECK(err_code);
}

/** @brief Función para iniciar el advertising  */
void advertising_start(void *p_erase_bonds) {
  uint32_t err_code = ble_advertising_start(&m_advertising, BLE_ADV_MODE_FAST);
  APP_ERROR_CHECK(err_code);
  NRF_LOG_DEBUG("advertising is started");
}

/** @brief Función para inicializar los servicios BLE e iniciar el advertising */
void app_ble_all_init(void) {
  ble_stack_init();
  gap_params_init();
  gatt_init();
  db_discovery_init();
  services_init();
  advertising_init();
  conn_params_init();
  scan_init();
  app_ble_advertising_start();
}

/** @brief Función para iniciar el advertising */
void app_ble_advertising_start(void) {
  advertising_start(NULL);
}

/** @brief Función para detener el advertising */
void app_ble_advertising_stop(void) {
  sd_ble_gap_adv_stop(m_advertising.adv_handle);
}

/** @brief Función para comprobar si la conexión BLE está actualmente desconectada.
 *
 * @return true si la conexión está desconectada, false si está conectada.
 */
bool app_ble_is_disconnected(void) {
  if (app_ble_state == BLE_GAP_EVT_DISCONNECTED) return true;
  else if (app_ble_state == BLE_GAP_EVT_CONNECTED) return false;
  
  return true;
}

/** @brief Función para desconectar una conexión BLE activa.
 *
 * Esta función intenta desconectar la conexión BLE actual si está activa.
 * Lo intentará hasta 10 veces, esperando 100 microsegundos entre intentos,
 * para asegurar que la desconexión tenga éxito.
 */
void app_ble_disconnect(void) {
  uint8_t i = 0;
  while (true) {
    if (!app_ble_is_disconnected()) {
      disconnect(m_conn_handle, NULL);
    } else {
      break;
    }

    hal_mcu_wait_us(100);

    i++;

    if (i > 10) {
      break;
    }
  }
}

/** @brief Función para enviar datos por el servicio Nordic UART (NUS).
 *
 * @param   p_data   Puntero al buffer de datos a enviar.
 * @param   length   Longitud de los datos a enviar.
 *
 * @details Esta función comprueba si hay una conexión BLE activa e intenta
 * enviar los datos por el NUS. Si no hay conexión activa, no lo intenta.
 */
void app_enviar_datos_nus(uint8_t * p_data, uint16_t length) {
  ret_code_t err_code;
  uint8_t intentos = 0;

  // Solo intentamos enviar si hay un dispositivo conectado
  if (m_conn_handle != BLE_CONN_HANDLE_INVALID) {
    do{
      err_code = ble_nus_data_send(&m_nus, p_data, &length, m_conn_handle);
      if(err_code == NRF_ERROR_RESOURCES){
        intentos++;  
        nrf_pwr_mgmt_run();
      }
    } while(err_code == NRF_ERROR_RESOURCES && intentos < 200);

    if (err_code == NRF_SUCCESS) m_notif_encoladas++;
  }
}

/** @brief Función para enviar buffers de datos grandes por NUS, fragmentando si es necesario.
 *
 * @param   buffer   Puntero al buffer de datos a enviar.
 * @param   tamano_datos   Longitud de los datos a enviar.
 *
 * @details Esta función envía buffers de datos grandes fragmentándolos en
 * trozos que caben en el MTU BLE. También comprueba la conexión activa y
 * gestiona los errores de recursos reintentando tras una breve espera.
 */
bool transmision_NUS(uint8_t *buffer, uint16_t tamano_datos) {
  ret_code_t err_code;
  size_t offset = 0;
 
  uint32_t deadline = hal_rtc_get_time_ms() + TIMEOUT_TX_NUS_MS;
 
  while (offset < tamano_datos) {
    if (app_ble_is_disconnected()) return false;
 
    size_t datos_restantes = tamano_datos - offset;
    uint16_t send_len = (datos_restantes < BLE_NUS_MAX_DATA_LEN) ? datos_restantes : BLE_NUS_MAX_DATA_LEN;
 
    err_code = ble_nus_data_send(&m_nus, (uint8_t *)(buffer + offset), &send_len, m_conn_handle);
 
    if (err_code == NRF_ERROR_RESOURCES) {
      // Cola del SoftDevice llena: esperamos a que drene, con limite de TIEMPO, no de intentos
      while (err_code == NRF_ERROR_RESOURCES) {
        if (app_ble_is_disconnected()) return false;
        if ((int32_t)(hal_rtc_get_time_ms() - deadline) > 0) return false;
 
        nrf_pwr_mgmt_run();
        hal_watchdog_reload();
 
        // Reinicializar: sd_ble_gatts_hvx puede haber modificado send_len
        send_len = (datos_restantes < BLE_NUS_MAX_DATA_LEN) ? datos_restantes : BLE_NUS_MAX_DATA_LEN;
        err_code = ble_nus_data_send(&m_nus, (uint8_t *)(buffer + offset), &send_len, m_conn_handle);
      }
    }
 
    if (err_code != NRF_SUCCESS) return false;
 
    m_notif_encoladas++;
    offset += send_len;
  }
 
  return true;
}

/** @brief Espera a que el SoftDevice transmita todas las notificaciones/indicaciones encoladas
 *
 * @param   timeout_ms   Tiempo máximo de espera, en milisegundos.
 * @return  true si se vació la cola antes del timeout, false si se desconectó
 * o se agotó el tiempo.
 */
bool app_ble_esperar_tx_vacio(uint32_t timeout_ms){
  uint32_t deadline = hal_rtc_get_time_ms() + timeout_ms;

  while(m_notif_encoladas - m_notif_confirmadas != 0){
    if(app_ble_is_disconnected()) return false;
    hal_watchdog_reload();
    if((int32_t)(hal_rtc_get_time_ms() - deadline) > 0) return false;
    nrf_pwr_mgmt_run();
  }
  return true;
}

/** @brief Envía datos como indication NUS y espera la confirmación del central
 *
 * @param   p_data         Puntero al buffer de datos a enviar.
 * @param   length         Longitud de los datos a enviar.
 * @param   max_intentos   Número máximo de reintentos si no llega confirmación a tiempo.
 * @param   timeout_s      Tiempo máximo de espera de confirmación por intento, en segundos.
 * @return  true si el central confirmó la indicación, false si se desconectó o
 * se agotaron los intentos sin confirmación.
 *
 * @details Reintenta el envío si la cola del SoftDevice está llena; si no llega
 * confirmación dentro de @p timeout_s, reintenta hasta @p max_intentos veces.
 */
bool app_enviar_indicacion_confirmada(uint8_t *p_data, uint16_t length, uint8_t max_intentos, uint32_t timeout_s){
  if(m_conn_handle == BLE_CONN_HANDLE_INVALID) return false;

  for (uint8_t intento = 0; intento < max_intentos; intento++){
    flags_globales.m_flag_indication_confirmada = false;

    uint16_t len = length;
    ret_code_t err_code;
    uint8_t intentos_recursos = 0;

    do{
      err_code = ble_nus_indication_send(&m_nus, p_data, &len, m_conn_handle);
      if(err_code == NRF_ERROR_RESOURCES){
        intentos_recursos++;
        nrf_pwr_mgmt_run();
      }
    }while(err_code == NRF_ERROR_RESOURCES && intentos_recursos < 200);

    if(err_code != NRF_SUCCESS){
      if(app_ble_is_disconnected()) return false;
      continue;
    }

    uint32_t deadline = hal_rtc_get_time_s() + timeout_s;

    while(!flags_globales.m_flag_indication_confirmada){
      hal_watchdog_reload();
      if(app_ble_is_disconnected()) return false;
      if(hal_rtc_get_time_s() > deadline) break;
      nrf_pwr_mgmt_run();
    }

    if(flags_globales.m_flag_indication_confirmada) return true;
  }
  return false; // se agotaron los intentos sin confirmacion
}

/** @brief Obtiene la dirección MAC BLE del dispositivo
 *
 * @param   mac_out   Buffer de 6 bytes donde se copia la dirección MAC.
 */
void get_ble_mac(uint8_t *mac_out) {
    ble_gap_addr_t mac_addr;
    sd_ble_gap_addr_get(&mac_addr);
    memcpy(mac_out, mac_addr.addr, BLE_GAP_ADDR_LEN);  // 6 bytes
}

/** @brief Procesa los comandos BLE pendientes mientras hay una conexión activa
 *
 * @details Atiende, si están pendientes: la descarga de datos por NUS, el
 * borrado de flash, y el reinicio del dispositivo (enviando primero un ACK).
 */
void procesar_comandos_ble(void){
  if (flags_globales.m_flag_descargar_datos) {
    flags_globales.m_flag_descargar_datos = false;
    transmision_memoria_ble();
  }

  if (flags_globales.m_flag_borrado_flash){
    borrar_flash();
  }

  if (flags_globales.m_flag_reiniciar){
    flags_globales.m_flag_reiniciar = false;
    uint8_t ack_data[] = "ACK_RESET_COMPLETE";
    app_enviar_datos_nus(ack_data, sizeof(ack_data)-1);
    
    hal_mcu_wait_ms(500);
    hal_mcu_reset();
  }
}
