/**
 * @file app_ble_all.c
 * @brief Full implementation of the BLE module (advertising, NUS, scanning)
 *
 * Manages:
 * - BLE stack and GAP/GATT configuration
 * - Nordic UART Service (NUS) for bidirectional communication
 * - Advertising with a custom device name
 * - Passive scanning of beacons with MAC filtering
 * - Storage and transmission of detected beacons
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
#include "ble_cts_c.h"
#include "time.h"
#include "app_at_fds_datas.h"

#define APP_BLE_CONN_CFG_TAG                1 /*!< A tag identifying the SoftDevice BLE configuration. */
#define APP_BLE_OBSERVER_PRIO               3 /*!< Application's BLE observer priority. You shouldn't need to modify this value. */
#define APP_SOC_OBSERVER_PRIO               1 /*!< Applications' SoC observer priority. You shouldn't need to modify this value. */

#define APP_ADV_INTERVAL                    MSEC_TO_UNITS(5000, UNIT_0_625_MS)  /*! 5 second */
#define APP_ADV_DURATION                    0     /*! Infinite duration */

// Parámetros rápidos (descarga NUS)
#define MIN_CONN_INTERVAL                   MSEC_TO_UNITS(75, UNIT_1_25_MS)
#define MAX_CONN_INTERVAL                   MSEC_TO_UNITS(100,  UNIT_1_25_MS)
#define SLAVE_LATENCY                       4

#define CONN_SUP_TIMEOUT                    MSEC_TO_UNITS(6000, UNIT_10_MS) /*!< Connection supervisory timeout (6 seconds). */

#define FIRST_CONN_PARAMS_UPDATE_DELAY      APP_TIMER_TICKS(500)  /*!< Time from initiating event (connect or start of notification) to first time sd_ble_gap_conn_param_update is called (5 seconds). */
#define NEXT_CONN_PARAMS_UPDATE_DELAY       APP_TIMER_TICKS(5000) /*!< Time between each call to sd_ble_gap_conn_param_update after the first call (30 seconds). */
#define MAX_CONN_PARAMS_UPDATE_COUNT        3 /*!< Number of attempts before giving up the connection parameter negotiation. */

#define DEAD_BEEF                           0xDEADBEEF /*!< Value used as error code on stack dump, can be used to identify stack location on stack unwind. */

#define APP_SCAN_INTERVAL                   160 /*!< Determines scan interval(in units of 0.625 ms). */
#define APP_SCAN_WINDOW                     160 /*!< Determines scan window(in units of 0.625 ms). */
#define APP_SCAN_DURATION                   0   /*!< Duration of the scanning(in units of 10 ms). */

/*!< Scan parameters requested for scanning and connection. */
static ble_gap_scan_params_t const m_scan_param = {
    .active = 0x00,
    .interval = APP_SCAN_INTERVAL,
    .window = APP_SCAN_WINDOW,
    .timeout = 1000,
    .filter_policy = BLE_GAP_SCAN_FP_ACCEPT_ALL,
    .scan_phys = BLE_GAP_PHY_1MBPS,
};

const uint8_t TARGET_MAC_ADDRS[][6] = {
    {0x02, 0x02, 0x03, 0x04, 0x05, 0x06},
};


BLE_CTS_C_DEF(m_cts_c); /*!< Current Time service instance. */
BLE_DB_DISCOVERY_DEF(m_db_disc); /*!< Discovery instance. */
BLE_NUS_DEF(m_nus, NRF_SDH_BLE_TOTAL_LINK_COUNT); /*!< BLE NUS service instance. */
NRF_BLE_SCAN_DEF(m_scan); /*!< Scanning Module instance. */
NRF_BLE_GATT_DEF(m_gatt); /*!< GATT module instance. */
NRF_BLE_QWR_DEF(m_qwr); /*!< Context for the Queued Write module.*/
BLE_ADVERTISING_DEF(m_advertising); /*!< Advertising module instance. */
NRF_BLE_GQ_DEF(m_ble_gatt_queue, NRF_SDH_BLE_PERIPHERAL_LINK_COUNT, NRF_BLE_GQ_QUEUE_SIZE); /*!< BLE GATT Queue instance. */

static uint16_t m_conn_handle = BLE_CONN_HANDLE_INVALID; /*!< Handle of the current connection. */

static uint8_t app_ble_state = BLE_GAP_EVT_DISCONNECTED; /*!< Variable to keep track of the current state of the BLE connection. */

/**@brief UUIDs related to advertising data. */
static ble_uuid_t m_adv_uuids[] = {
  {BLE_UUID_NUS_SERVICE, BLE_UUID_TYPE_VENDOR_BEGIN}, /*!< NUS Service UUID */
  {BLE_UUID_CURRENT_TIME_SERVICE, BLE_UUID_TYPE_BLE} /*!< CTS Service UUID */
};

/**@brief Buffer of beacons detected during scanning */
static uint8_t beacon_guardados[MAX_BEACONS_SAVED][MAX_BEACON_SIZE];
/**@brief Number of beacons currently stored */
static uint8_t beacon_count = 0;

static uint32_t m_last_connection_timestamp = 0;
ble_advdata_service_data_t m_service_data;
static uint8_t m_paquete_advertising[4];

/**@brief Flag to indicate a NUS command was received and the beacon report should be sent */
static void advertising_start(void *p_erase_bonds);

/**@brief Function for assert macro callback.
 *
 * @details This function will be called in case of an assert in the SoftDevice.
 *
 * @warning This handler is an example only and does not fit a final product.
 * You need to analyse how your product is supposed to react in case of Assert.
 * @warning On assert from the SoftDevice, the system can only recover on reset.

 * @param   line_num   Line number of the failing ASSERT call.
 * @param   p_file_name   File name of the failing ASSERT call.
 */
void assert_nrf_callback(uint16_t line_num, const uint8_t *p_file_name) {
  NRF_LOG_ERROR("ASSERT en linea %d del archivo %s", line_num, (uint32_t)p_file_name);
  NRF_LOG_FLUSH();
  hal_mcu_wait_ms(500);
  app_error_handler(DEAD_BEEF, line_num, p_file_name);
}

/**@brief Function for handling the Current Time Service errors.
 *
 * @param[in]  nrf_error  Error code containing information about what went wrong.
 */
static void current_time_error_handler(uint32_t nrf_error){
  APP_ERROR_HANDLER(nrf_error);
}

/**@brief Function for handling the Disconnect event.
 *
 * @param   conn_handle   Connection handle on which the disconnect event occurred.
 * @param   p_context   Unused.
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

/**@brief Function for handling the Application's BLE Stack events.
 *
 * @param   p_ble_evt   Bluetooth stack event.
 * @param   p_context   Unused.
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

/**@brief Function for the GAP initialization.
 *
 * @details This function sets up all the necessary GAP (Generic Access Profile)
 * parameters of the device including the device name, appearance, and the
 * preferred connection parameters.
 */
static void gap_params_init(void) {
  uint32_t err_code;
  ble_gap_conn_params_t gap_conn_params;
  ble_gap_conn_sec_mode_t sec_mode;
  uint8_t adv_device_name[24] = {0};
  sprintf(adv_device_name, "D2HOWL");

  ble_gap_addr_t new_mac_addr;
  new_mac_addr.addr_type = BLE_GAP_ADDR_TYPE_PUBLIC;
  
  // Dispositivo 1
  //new_mac_addr.addr[0] = 0x02; // LSB
  //new_mac_addr.addr[1] = 0x4F;
  //new_mac_addr.addr[2] = 0x4F;
  //new_mac_addr.addr[3] = 0x48;
  //new_mac_addr.addr[4] = 0x31;
  //new_mac_addr.addr[5] = 0x44; // MSB

  // Dispositivo 2
  new_mac_addr.addr[0] = 0x01; // LSB
  new_mac_addr.addr[1] = 0x4F;
  new_mac_addr.addr[2] = 0x4F;
  new_mac_addr.addr[3] = 0x48;
  new_mac_addr.addr[4] = 0x31;
  new_mac_addr.addr[5] = 0x44; // MSB  

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

/**@brief Function for handling Queued Write Module errors.
 *
 * @details A pointer to this function will be passed to each service which may
 * need to inform the application about an error.
 *
 * @param   nrf_error   Error code containing information about what went wrong.
 */
static void nrf_qwr_error_handler(uint32_t nrf_error) {
  APP_ERROR_HANDLER(nrf_error);
}

/**@brief NUS (Nordic UART Service) event handler
 *
 * @param   p_evt   Received NUS event
 * @details Processes received commands:
 *   - 0x01: Update configuration
 *   - 0x02: Request data download
 *   - 0x03: Read current configuration
 *   - 0x04: Reset MCU
 *   - 0x05: Erase flash
 */
static void nus_data_handler(ble_nus_evt_t *p_evt) {
  uint32_t err_code;
  const uint8_t *p_data = p_evt->params.rx_data.p_data; 
}

/**@brief Function for handling the Current Time Service client events.
 *
 * @details This function will be called for all events in the Current Time Service client that
 *          are passed to the application.
 *
 * @param[in] p_evt Event received from the Current Time Service client.
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

    case BLE_CTS_C_EVT_CURRENT_TIME:
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

    case BLE_CTS_C_EVT_INVALID_TIME:
      NRF_LOG_INFO("Invalid Time received.");
    break;

    default:
    break;
  }
}

/**@brief Function for initializing services that will be used by the application.
 *
 * @details Initialize the Queued Write Module and the Nordic UART Service.
 */
static void services_init(void) {
  uint32_t err_code;
  nrf_ble_qwr_init_t qwr_init = {0};
  ble_nus_init_t     nus_init;
  ble_cts_c_init_t   cts_init = {0};

  // Initialize Queued Write Module.
  qwr_init.error_handler = nrf_qwr_error_handler;
  err_code = nrf_ble_qwr_init(&m_qwr, &qwr_init);
  APP_ERROR_CHECK(err_code);

  // Initialize NUS.
  memset(&nus_init, 0, sizeof(nus_init));

  nus_init.data_handler = nus_data_handler;

  err_code = ble_nus_init(&m_nus, &nus_init);
  APP_ERROR_CHECK(err_code);

  // Initialize CTS.
  cts_init.evt_handler   = on_cts_c_evt;
  cts_init.error_handler = current_time_error_handler;
  cts_init.p_gatt_queue  = &m_ble_gatt_queue;
  err_code               = ble_cts_c_init(&m_cts_c, &cts_init);
  APP_ERROR_CHECK(err_code);
}

/**@brief Function for handling Database Discovery events.
 *
 * @details This function is a callback function to handle events from the database discovery module.
 *          Depending on the UUIDs that are discovered, this function should forward the events
 *          to their respective service instances.
 *
 * @param[in] p_event  Pointer to the database discovery event.
 */
static void db_disc_handler(ble_db_discovery_evt_t * p_evt){
  ble_cts_c_on_db_disc_evt(&m_cts_c, p_evt);
}

/**
 * @brief Database discovery collector initialization.
 */
static void db_discovery_init(void){
  ble_db_discovery_init_t db_init;

  memset(&db_init, 0, sizeof(ble_db_discovery_init_t));

  db_init.evt_handler  = db_disc_handler;
  db_init.p_gatt_queue = &m_ble_gatt_queue;

  ret_code_t err_code = ble_db_discovery_init(&db_init);
  APP_ERROR_CHECK(err_code);
}

/**@brief Function for handling the Connection Parameters Module.
 *
 * @details This function will be called for all events in the Connection
 * Parameters Module which are passed to the application.
 *          @note All this function does is to disconnect. This could have been
 * done by simply setting the disconnect_on_fail config parameter, but instead
 * we use the event handler mechanism to demonstrate its use.
 *
 * @param   p_evt   Event received from the Connection Parameters Module.
 */
static void on_conn_params_evt(ble_conn_params_evt_t *p_evt) {
  uint32_t err_code;

  if (p_evt->evt_type == BLE_CONN_PARAMS_EVT_FAILED) {
    err_code = sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_CONN_INTERVAL_UNACCEPTABLE);
    APP_ERROR_CHECK(err_code);
  }
}

/**@brief Function for handling a Connection Parameters error.
 *
 * @param   nrf_error   Error code containing information about what went wrong.
 */
static void conn_params_error_handler(uint32_t nrf_error) {
  APP_ERROR_HANDLER(nrf_error);
}

/**@brief Function for initializing the Connection Parameters module.
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

/**@brief Function for putting the chip into sleep mode.
 *
 * @note This function will not return.
 */
static void sleep_mode_enter(void) { 
  nrf_pwr_mgmt_run(); 
}

/**@brief Function for handling advertising events.
 *
 * @details This function will be called for advertising events which are passed
 * to the application.
 *
 * @param   ble_adv_evt   Advertising event.
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

/**@brief Reset the beacon storage buffer
 *
 * @details Zeroes the counter and clears the beacon array
 */
void beacon_storage_reset(void) {
  beacon_count = 0;
  memset(beacon_guardados, 0, sizeof(beacon_guardados));
}

/**@brief Processes and stores a detected beacon packet
 *
 * @param   data   Beacon data (includes length, ID, etc.)
 * @param   rssi   RSSI of the detected beacon
 * @details Avoids duplicates by comparing the beacon ID
 */
void beacon_process_packet(uint8_t *data, int8_t rssi) {
  uint8_t len = data[0];
  if (len >= MAX_BEACON_SIZE) return;
  if (beacon_count >= MAX_BEACONS_SAVED) return;

  uint16_t beacon_id = (data[3] << 8) | data[2];

  // Para evitar duplicados
  for (int i = 0; i < beacon_count; i++) {
    uint16_t beacon_id_guardado = (beacon_guardados[i][3] << 8) | beacon_guardados[i][2];
    if (beacon_id_guardado == beacon_id) {
      return;
    }
  }

  memcpy(beacon_guardados[beacon_count], data, len + 1);
  beacon_guardados[beacon_count][len + 1] = (uint8_t)rssi;
  beacon_count++;
}

void beacon_storage_save(void) {

}

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

/**@brief Function for handling BLE events.
 *
 * @param   p_ble_evt   Bluetooth stack event.
 * @param   p_context   Unused.
 */
static void ble_evt_handler(ble_evt_t const *p_ble_evt, void *p_context) {
  ret_code_t err_code;
  uint32_t last, now = 0;
  ble_gap_evt_t const *p_gap_evt = &p_ble_evt->evt.gap_evt;

  uint32_t rssi = 0;

  switch (p_ble_evt->header.evt_id) {
  case BLE_GAP_EVT_ADV_REPORT:
    ble_gap_evt_adv_report_t const *p_adv_report = &p_gap_evt->params.adv_report;
    ble_gap_addr_t const *p_peer_addr = &p_adv_report->peer_addr;

    if (p_peer_addr->addr[5] == 0x48 && // 'H'
        p_peer_addr->addr[4] == 0x4F && // 'O'
        p_peer_addr->addr[3] == 0x57 && // 'W'
        p_peer_addr->addr[2] == 0x4C)   // 'L'
    {

      beacon_process_packet((uint8_t *)p_adv_report->data.p_data, p_adv_report->rssi);

      break;
    }
    break;

  case BLE_GAP_EVT_DISCONNECTED:
    app_ble_state = BLE_GAP_EVT_DISCONNECTED;
    NRF_LOG_DEBUG("BLE_GAP_EVT_DISCONNECTED\r\n");
    
    m_last_connection_timestamp = get_timestamp();
    update_timestamp_advertising();

    flags_globales.m_flag_borrado_flash = false;
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
    break;

  case BLE_GATTS_EVT_SYS_ATTR_MISSING:
    err_code = sd_ble_gatts_sys_attr_set(m_conn_handle, NULL, 0, 0);
    APP_ERROR_CHECK(err_code);
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
    // Disconnect on GATT Client timeout event.
    NRF_LOG_DEBUG("GATT Client Timeout.");
    err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gattc_evt.conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
    APP_ERROR_CHECK(err_code);
    break;

  case BLE_GATTS_EVT_TIMEOUT:
    // Disconnect on GATT Server timeout event.
    NRF_LOG_DEBUG("GATT Server Timeout.");
    err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gatts_evt.conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
    APP_ERROR_CHECK(err_code);
    break;

  default:
    break;
  }
}

/**@brief Function for handling Scanning Module events.
 *
 * @param   p_scan_evt   Scanning event.
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

/**@brief SoftDevice SoC event handler.
 *
 * @param   evt_id   SoC event.
 * @param   p_context   Context.
 */
static void soc_evt_handler( uint32_t evt_id, void * p_context ){
  switch( evt_id ){
    default:
    break;
  }
}

/**@brief Function for initializing the BLE stack.
 *
 * @details Initializes the SoftDevice and the BLE event interrupt.
 */
static void ble_stack_init(void) {
  ret_code_t err_code;

  err_code = nrf_sdh_enable_request();
  APP_ERROR_CHECK(err_code);

  // Configure the BLE stack using the default settings.
  // Fetch the start address of the application RAM.
  uint32_t ram_start = 0;
  err_code = nrf_sdh_ble_default_cfg_set(APP_BLE_CONN_CFG_TAG, &ram_start);
  APP_ERROR_CHECK(err_code);

  ble_cfg_t ble_cfg;
  memset(&ble_cfg, 0, sizeof(ble_cfg));

  ble_cfg.conn_cfg.conn_cfg_tag = APP_BLE_CONN_CFG_TAG;
  ble_cfg.conn_cfg.params.gatts_conn_cfg.hvn_tx_queue_size = 12;

  err_code = sd_ble_cfg_set(BLE_CONN_CFG_GATTS, &ble_cfg, ram_start);
  APP_ERROR_CHECK(err_code);

  // Enable BLE stack.
  err_code = nrf_sdh_ble_enable(&ram_start);
  APP_ERROR_CHECK(err_code);

  // Register handlers for BLE and SoC events.
  NRF_SDH_BLE_OBSERVER(m_ble_observer, APP_BLE_OBSERVER_PRIO, ble_evt_handler, NULL);
  NRF_SDH_SOC_OBSERVER(m_soc_observer, APP_SOC_OBSERVER_PRIO, soc_evt_handler, NULL);
}

/**@brief Function for initializing the scanning and setting filters.
 *
 * @details Initializes the scanning module and sets filters to look for
 * specific advertising packets.
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

/**@brief Function for starting the scanning module.
 *
 * @details This function sets the filters and starts scanning.
 */
void ble_scan_start(void) {
  ret_code_t err_code;

  err_code = nrf_ble_scan_params_set(&m_scan, &m_scan_param);
  APP_ERROR_CHECK(err_code);

  err_code = nrf_ble_scan_start(&m_scan);
  APP_ERROR_CHECK(err_code);
}

/**@brief Function for stopping the scanning module.
 *
 * @details This function stops the scanning and processes the stored beacons.
 */
void ble_scan_stop(void) {
  nrf_ble_scan_stop();

  beacon_storage_save();
  beacon_storage_reset();
}

void empaquetar_beacons(void) {
  payload_add_data(&ultimas_medidas.beacon_count, 1);
  for (int i = 0; i < ultimas_medidas.beacon_count; i++) {
      uint8_t packet_len = ultimas_medidas.beacons[i][0];
      uint8_t total_len  = 1 + packet_len + 1;
      payload_add_data(ultimas_medidas.beacons[i], total_len);
  }
}

/**@brief Function for initializing the Advertising functionality.
 *
 * @details Initializes the advertising functionality and its event handlers.
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

/**@brief Function for handling events from the GATT library.
 *
 */
void gatt_evt_handler(nrf_ble_gatt_t *p_gatt, nrf_ble_gatt_evt_t const *p_evt) {
  if ((m_conn_handle == p_evt->conn_handle) && (p_evt->evt_id == NRF_BLE_GATT_EVT_ATT_MTU_UPDATED)) { }

  NRF_LOG_DEBUG("ATT MTU exchange completed. central 0x%x peripheral 0x%x", p_gatt->att_mtu_desired_central,  p_gatt->att_mtu_desired_periph);
}

/**@brief Function for initializing the GATT module.
 *
 * @details The GATT module handles ATT_MTU and Data Length update procedures
 * automatically.
 */
static void gatt_init(void) {
  ret_code_t err_code = nrf_ble_gatt_init(&m_gatt, NULL);
  // ret_code_t err_code = nrf_ble_gatt_init( &m_gatt, gatt_evt_handler );
  APP_ERROR_CHECK(err_code);
  err_code = nrf_ble_gatt_att_mtu_periph_set(&m_gatt, NRF_SDH_BLE_GATT_MAX_MTU_SIZE);
  APP_ERROR_CHECK(err_code);
}

/**@brief Function for starting advertising.
 *
 */
void advertising_start(void *p_erase_bonds) {
  uint32_t err_code = ble_advertising_start(&m_advertising, BLE_ADV_MODE_FAST);
  APP_ERROR_CHECK(err_code);
  NRF_LOG_DEBUG("advertising is started");
}

/**@brief Function for initializing the BLE services and starting advertising.
 *
 */
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

/**@brief Function for starting advertising.
 *
 */
void app_ble_advertising_start(void) { 
  advertising_start(NULL); 
}

/**@brief Function for stopping advertising.
 *
 */
void app_ble_advertising_stop(void) {
  sd_ble_gap_adv_stop(m_advertising.adv_handle);
}

/**@brief Function for checking if the BLE connection is currently disconnected.
 *
 * @return true if the connection is disconnected, false if it is connected.
 */
bool app_ble_is_disconnected(void) {
  if (app_ble_state == BLE_GAP_EVT_DISCONNECTED) return true;
  else if (app_ble_state == BLE_GAP_EVT_CONNECTED) return false;
  
  return true;
}

/**@brief Function for disconnecting an active BLE connection.
 *
 * This function attempts to disconnect the current BLE connection if it is active.
 * It will try up to 10 times, waiting 100 microseconds between attempts, to ensure
 * that the disconnection is successful.
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

/**@brief Function for sending data over the Nordic UART Service (NUS).
 *
 * @param   p_data   Pointer to the data buffer to be sent.
 * @param   length   Length of the data to be sent.
 *
 * @details This function checks if there is an active BLE connection and attempts
 * to send the provided data over the NUS. If there is no active connection, it will not attempt to send.
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
  }
}

/**@brief Function for sending large data buffers over NUS, handling fragmentation if necessary.
 *
 * @param   buffer   Pointer to the data buffer to be sent.
 * @param   tamano_datos   Length of the data to be sent.
 *
 * @details This function handles sending large data buffers by fragmenting them into chunks that fit within the BLE MTU size. It also checks for active connection and handles resource errors by retrying after a short delay.
 */
void transmision_NUS(uint8_t *buffer, uint16_t tamano_datos) {
  ret_code_t err_code;
  size_t offset = 0;
  const uint8_t *ptr_datos = buffer;

  while (offset < tamano_datos) {
    if (app_ble_is_disconnected()) return;

    size_t datos_restantes = tamano_datos - offset;
    uint16_t send_len = (datos_restantes < BLE_NUS_MAX_DATA_LEN) ? datos_restantes : BLE_NUS_MAX_DATA_LEN;

    err_code = ble_nus_data_send(&m_nus, (uint8_t *)(buffer + offset), &send_len, m_conn_handle);

    if (err_code == NRF_SUCCESS) {
      offset += send_len;
    } else if (err_code == NRF_ERROR_RESOURCES) {
      //hal_mcu_wait_ms(8); // Ajustado al intervalo de conexion minimo (7.5 ms) cuando se activa la descarga
      nrf_pwr_mgmt_run();
    } else {
      return;
    }
  }
}

void get_ble_mac(uint8_t *mac_out) {
    ble_gap_addr_t mac_addr;
    sd_ble_gap_addr_get(&mac_addr);
    memcpy(mac_out, mac_addr.addr, BLE_GAP_ADDR_LEN);  // 6 bytes
}

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
