/**
 * @file app_at_fds_datas.c
 * @brief Implementación del sistema de almacenamiento en flash usando FDS
 *
 * Gestiona la persistencia de configuración mediante el sistema Flash Data
 * Storage de Nordic. Incluye inicialización, lectura, escritura, actualización
 * y garbage collection automático.
 */

#include "app_at_fds_datas.h"
#include "fds.h"
#include "nrf_fstorage.h"
#include "nrf_log.h"
#include "nrf_pwr_mgmt.h"
#include "smtc_hal.h"
#include "main_lorawan.h"


#ifdef SOFTDEVICE_PRESENT
#include "nrf_fstorage_sd.h"
#include "nrf_sdh.h"
#include "nrf_sdh_ble.h"
#include "nrf_soc.h"
#else
#include "nrf_drv_clock.h"
#include "nrf_fstorage_nvmc.h"
#endif

/** @brief Bandera que indica si FDS está inicializado */
static volatile bool m_fds_initialized = false;

/** @brief Bandera para sincronización de operaciones FDS (escritura/GC) */
static volatile bool m_fds_op_success = false;

/**
 * @brief Escribe un nuevo registro en flash
 * @return true si la operación fue exitosa, false en caso contrario
 */
static bool internal_write(void);

/**
 * @brief Actualiza un registro existente en flash
 * @return true si la operación fue exitosa, false en caso contrario
 */
static bool internal_update(void);

static void waste_detect_recycle(void);

/** @brief Descriptor del registro de configuración para FDS */
fds_record_t m_record = {.file_id = CONFIG_FILE1,
                         .key = CONFIG_REC_KEY1,
                         .data.p_data = &anchor_config,
                         .data.length_words = (sizeof(anchor_config_t) + 3) / 4
                        };

/**
 * @brief Manejador de eventos FDS
 * @param p_evt Puntero al evento FDS recibido
 * @details Procesa eventos de inicialización, escritura, borrado, actualización
 *          y garbage collection, actualizando las banderas de control
 */
static void fds_evt_handler(fds_evt_t const *p_evt) {
  switch (p_evt->id) {
  case FDS_EVT_INIT:
    if (p_evt->result == NRF_SUCCESS) {
      PRINTF("FDS_EVT_INIT success\r\n");
      m_fds_initialized = true;
    }
    break;

  case FDS_EVT_WRITE:
    if (p_evt->result == NRF_SUCCESS) {
      PRINTF("write----->>>Record ID:\t0x%04x\r\n", p_evt->write.record_id);
      PRINTF("File ID:\t0x%04x\r\n", p_evt->write.file_id);
      PRINTF("Record key:\t0x%04x\r\n", p_evt->write.record_key);
      PRINTF("FDS_EVT_WRITE success\r\n");
      m_fds_op_success = true;
    }
    break;

  case FDS_EVT_DEL_RECORD:
    if (p_evt->result == NRF_SUCCESS) {
      PRINTF("del----->>>Record ID:\t0x%04x", p_evt->del.record_id);
      PRINTF("File ID:\t0x%04x", p_evt->del.file_id);
      PRINTF("Record key:\t0x%04x\r\n", p_evt->del.record_key);
      PRINTF("FDS_EVT_DEL_RECORD success");
    }
    break;

  case FDS_EVT_UPDATE:
    if (p_evt->result == NRF_SUCCESS) {
      PRINTF("update----->>>Record ID:\t0x%04x\r\n", p_evt->del.record_id);
      PRINTF("File ID:\t0x%04x\r\n", p_evt->del.file_id);
      PRINTF("Record key:\t0x%04x\r\n", p_evt->del.record_key);
      PRINTF("FDS_EVT_UPDATE success\r\n");
      m_fds_op_success = true;
    }
    break;

  case FDS_EVT_DEL_FILE:
    if (p_evt->result == NRF_SUCCESS) {
      PRINTF("FDS_EVT_DEL_FILE success\r\n");
    }
    break;

  case FDS_EVT_GC:
    if (p_evt->result == NRF_SUCCESS) {
      PRINTF("FDS_EVT_GC success\r\n");
      m_fds_op_success = true;
    }
    break;

  default:
    break;
  }
}

void app_fds_init(void) {
  ret_code_t rc;
  fds_record_desc_t desc = {0};
  fds_find_token_t tok = {0};

  (void)fds_register(fds_evt_handler); // FDS callback
  rc = fds_init();                     // fds
  APP_ERROR_CHECK(rc);

  while (!m_fds_initialized) {
    nrf_pwr_mgmt_run();
  }

  waste_detect_recycle();

  fds_record_desc_t desc_borrado = {0};
  fds_find_token_t  tok_borrado  = {0};
  if (fds_record_find(CONFIG_FILE1, CONFIG_REC_KEY1, &desc_borrado, &tok_borrado) == NRF_SUCCESS) {
    fds_record_delete(&desc_borrado);
    waste_detect_recycle();
    PRINTF("FDS: Config borrada forzosamente\n");
  }
  
  rc = fds_record_find(CONFIG_FILE1, CONFIG_REC_KEY1, &desc, &tok);
  if (rc == NRF_SUCCESS) {
    // CASO A: Encontramos datos guardados -> Los copiamos a RAM
    fds_flash_record_t temp = {0};
    rc = fds_record_open(&desc, &temp);
    if (rc == NRF_SUCCESS) {
      // Recuperamos los datos tras un reinicio
      memcpy(&anchor_config, temp.p_data, sizeof(anchor_config_t));
      PRINTF("Configuracion RECUPERADA de Flash\n");
      fds_record_close(&desc);
    }
  } else {
    // CASO B: No hay datos (Chip nuevo) -> Guardamos los valores por defecto
    PRINTF("Memoria vacia. Guardando configuracion por defecto\n");
    internal_write();
  }
}

void app_fds_store_save(void) {
  fds_record_desc_t desc = {0};
  fds_find_token_t tok = {0};

  // Siempre comprobar espacio antes de escribir
  waste_detect_recycle();

  // Si ya existe -> Actualizar (Update). Si no -> Escribir (Write)
  if (fds_record_find(CONFIG_FILE1, CONFIG_REC_KEY1, &desc, &tok) ==
      NRF_SUCCESS) {
    internal_update();
  } else {
    internal_write();
  }
}

/**
 * @brief Escribe un nuevo registro en flash (uso interno)
 * @return true si la escritura fue exitosa, false en caso contrario
 * @details Bloquea hasta que la operación termine usando power management
 */
static bool internal_write(void) {
  fds_record_desc_t desc = {0};
  ret_code_t rc;

  m_fds_op_success = false;
  rc = fds_record_write(&desc, &m_record);
  if (rc != NRF_SUCCESS)
    return false;

  // Bloqueamos hasta que termine (seguro para TFM)
  while (!m_fds_op_success) {
    nrf_pwr_mgmt_run();
  }
  return true;
}

/**
 * @brief Actualiza un registro existente en flash (uso interno)
 * @return true si la actualización fue exitosa, false en caso contrario
 * @details Busca el registro actual y lo actualiza, bloqueando hasta finalizar
 */
static bool internal_update(void) {
  fds_record_desc_t desc = {0};
  fds_find_token_t tok = {0};
  ret_code_t rc;

  // Buscamos el registro actual para saber qué actualizar
  rc = fds_record_find(CONFIG_FILE1, CONFIG_REC_KEY1, &desc, &tok);
  if (rc != NRF_SUCCESS)
    return false;

  m_fds_op_success = false;
  rc = fds_record_update(&desc, &m_record);
  if (rc != NRF_SUCCESS)
    return false;

  while (!m_fds_op_success) {
    nrf_pwr_mgmt_run();
  }
  return true;
}

void waste_detect_recycle(void) {
  ret_code_t rc;
  fds_stat_t stat = {0};

  rc = fds_stat(&stat);
  APP_ERROR_CHECK(rc);

  // Si queda poco espacio (menos de 3 veces el tamaño de tu config), limpiamos basura
  if (stat.largest_contig < ((sizeof(anchor_config_t) + 3)/4)*3) {
    if (stat.dirty_records > 0) {
      PRINTF("FDS: Ejecutando Garbage Collection\n");
      m_fds_op_success = false;
      rc = fds_gc();
      APP_ERROR_CHECK(rc);

      while (!m_fds_op_success) {
        nrf_pwr_mgmt_run();
      }
    }
  }
}
