#include "main_flash.h"
#include "app_at_fds_datas.h"
#include "app_ble_all.h"
#include "main_LSM6DSOX.h"
#include "nrf_drv_qspi.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrfx_qspi.h"
#include "smtc_hal.h"
#include "crc16.h"
#include "nrf_pwr_mgmt.h"

/** @brief Dirección de escritura actual en la partición de flash de Lora. */
static uint32_t direccion_flash_lora = ADDR_LORA_START;
/** @brief Dirección de escritura actual en la partición de flash de IMU. */
static uint32_t direccion_flash_imu = ADDR_IMU_START;
/** @brief Dirección de lectura usada para transmisión o búsqueda de datos. */
static uint32_t direccion_lectura;

/** @brief Indica si la interfaz QSPI ya ha sido inicializada. */
static bool m_flash_init = false;
/** @brief Señal de finalización para operaciones QSPI asíncronas. */
static volatile bool m_finished = false;
/** @brief Indica si la memoria flash está en modo de bajo consumo. */
static bool m_flash_sleeping = false;

/** @brief Buffer circular temporal para almacenar paquetes antes de escribirlos en flash. */
static buffer_temporal_t buffer_temporal_paquetes[buffer_temporal_tamano];
/** @brief Índice de escritura dentro del buffer temporal. */
static volatile uint8_t puntero_buftemp_escritura = 0;
/** @brief Índice de lectura dentro del buffer temporal. */
static volatile uint8_t puntero_buftemp_lectura = 0;
/** @brief Dirección de borrado actual en la memoria flash. */
volatile uint32_t puntero_borrado = ADDR_LORA_START;
/** @brief Número de paquetes pendientes en el buffer temporal. */
static volatile uint8_t contador_paquetes = 0;
/** @brief Contador de paquetes descartados por overflow del buffer temporal. */
static volatile uint8_t contador_overflow = 0;
/** @brief Indica si la partición de Lora ha experimentado wrap-around. */
static bool wrap_lora = false;
/** @brief Indica si la partición de IMU ha experimentado wrap-around. */
static bool wrap_imu = false;

/** @brief Espera a que termine la operación de periférico QSPI activa. */
#define WAIT_FOR_PERIPH()                                                      \
  do {                                                                         \
    while (!m_finished) {                                                      \
    }                                                                          \
    m_finished = false;                                                        \
  } while (0)

/** @brief Espera a que el chip de flash termine el ciclo completo de la última operación
 *  WAIT_FOR_PERIPH() solo espera a que el periférico (QSPI) termine de mandar el comando.
 *  El chip externo puede seguir ocupado hasta que WIP baje a 0
 */
static void esperar_wip_flash(void){
  while (nrf_drv_qspi_mem_busy_check() != NRF_SUCCESS){
    hal_watchdog_reload();
  }
}

/** @brief Controlador de eventos QSPI.
 *
 * Se utiliza como callback del driver QSPI para indicar que una operación
 * asíncrona ha finalizado.
 *
 * @param event    Evento QSPI recibido.
 * @param p_context Contexto de usuario asociado al callback.
 */
static void qspi_handler(nrf_drv_qspi_evt_t event, void *p_context) {
  UNUSED_PARAMETER(event);
  UNUSED_PARAMETER(p_context);
  m_finished = true;
}

/** @brief Configura la memoria flash para operaciones QSPI.
 *
 * Envía la secuencia de instrucciones de control necesarias para preparar la
 * memoria flash externa:
 *   - RSTEN: habilitar reset.
 *   - RST: ejecutar reset.
 *   - WRSR: escribir el registro de estado para poner la flash en modo QSPI.
 */
static void configure_memory(void) {
  uint8_t temporary = 0x40;
  uint32_t err_code;
  nrf_qspi_cinstr_conf_t cinstr_cfg = {.opcode = QSPI_STD_CMD_RSTEN,
                                       .length = NRF_QSPI_CINSTR_LEN_1B,
                                       .io2_level = true,
                                       .io3_level = true,
                                       .wipwait = true,
                                       .wren = true};

  // Send reset enable
  err_code = nrf_drv_qspi_cinstr_xfer(&cinstr_cfg, NULL, NULL);
  APP_ERROR_CHECK(err_code);

  // Send reset command
  cinstr_cfg.opcode = QSPI_STD_CMD_RST;
  err_code = nrf_drv_qspi_cinstr_xfer(&cinstr_cfg, NULL, NULL);
  APP_ERROR_CHECK(err_code);

  // Switch to qspi mode
  cinstr_cfg.opcode = QSPI_STD_CMD_WRSR;
  cinstr_cfg.length = NRF_QSPI_CINSTR_LEN_2B;
  err_code = nrf_drv_qspi_cinstr_xfer(&cinstr_cfg, &temporary, NULL);
  APP_ERROR_CHECK(err_code);
}

/** @brief Inicializa el periférico QSPI para la memoria flash externa.
 *
 * Configura los pines, la velocidad, el modo de transferencia y el modo de
 * direccionamiento para que la flash pueda ser operada en modo quad SPI.
 *
 * No hace nada si QSPI ya está inicializado.
 */
void qspi_init(void) {
  uint32_t err_code;

  if (m_flash_init)
    return;

  nrfx_qspi_config_t config = NRFX_QSPI_DEFAULT_CONFIG;

  // Asignación de pines según tus defines
  config.pins.sck_pin = FLASH_SPI_SCK_PIN;
  config.pins.csn_pin = FLASH_SPI_CS_PIN;
  config.pins.io0_pin = FLASH_SPI_MOSI_PIN; // MOSI es IO0
  config.pins.io1_pin = FLASH_SPI_MISO_PIN; // MISO es IO1
  config.pins.io2_pin = FLASH_SPI_IO2_PIN;
  config.pins.io3_pin = FLASH_SPI_IO3_PIN;

  // Configuración para velocidad
  config.phy_if.sck_freq = NRF_QSPI_FREQ_32MDIV1;

  // Modo de lectura/escritura en 4 líneas (Quad)
  config.prot_if.readoc = NRF_QSPI_READOC_READ4IO;
  config.prot_if.writeoc = NRF_QSPI_WRITEOC_PP4IO;
  config.prot_if.addrmode = NRF_QSPI_ADDRMODE_24BIT; // 24 bit direccionan hasta 16MB (128Mb)

  err_code = nrf_drv_qspi_init(&config, qspi_handler, NULL);
  APP_ERROR_CHECK(err_code);
  m_flash_init = true;


}

/** @brief Desinicializa el periférico QSPI.
 *
 * Libera los recursos asociados al driver QSPI y vuelve a marcar el estado de
 * inicialización como no validado.
 */
void qspi_uninit(void) {
  nrf_drv_qspi_uninit();
  m_flash_init = false;
}

/** @brief Lleva la memoria flash a modo de bajo consumo profundo.
 *
 * Envía la instrucción DPD (Deep Power Down) a la memoria externa y desinicializa
 * la interfaz QSPI. Los pines QSPI se reconfiguran para reducir el consumo al
 * mínimo.
 */
void flash_deep_sleep(void) {
  if (!m_flash_init || m_flash_sleeping)
    return;
  
  uint32_t err_code;
  nrf_qspi_cinstr_conf_t cinstr_cfg = {
      .opcode = QSPI_STD_CMD_DPD,
      .length = NRF_QSPI_CINSTR_LEN_1B,
      .io2_level = true,
      .io3_level = true,
      .wipwait = false, // No esperamos a que termine una escritura previa, asumimos idle
      .wren = false     // No requiere Write Enable
  };

  // Enviamos instrucción de apagado
  err_code = nrf_drv_qspi_cinstr_xfer(&cinstr_cfg, NULL, NULL);
  APP_ERROR_CHECK(err_code);

  qspi_uninit();
  
  // Para evitar pines flotantes tras apagar el periferico. El consumo pasa de 2.2 mA a 720 uA
  hal_gpio_init_in(FLASH_SPI_MOSI_PIN, HAL_GPIO_PULL_MODE_DOWN, HAL_GPIO_IRQ_MODE_OFF, NULL);
  hal_gpio_init_in(FLASH_SPI_MISO_PIN, HAL_GPIO_PULL_MODE_DOWN, HAL_GPIO_IRQ_MODE_OFF, NULL);
  hal_gpio_init_in(FLASH_SPI_SCK_PIN, HAL_GPIO_PULL_MODE_DOWN, HAL_GPIO_IRQ_MODE_OFF, NULL);
  hal_gpio_init_in(FLASH_SPI_IO2_PIN, HAL_GPIO_PULL_MODE_DOWN, HAL_GPIO_IRQ_MODE_OFF, NULL);
  hal_gpio_init_in(FLASH_SPI_IO3_PIN, HAL_GPIO_PULL_MODE_DOWN, HAL_GPIO_IRQ_MODE_OFF, NULL);
  hal_gpio_init_out(FLASH_SPI_CS_PIN, HAL_GPIO_SET);

  m_flash_sleeping = true;
}

/** @brief Despierta la memoria flash desde modo de bajo consumo.
 *
 * Re-inicializa el periférico QSPI y envía la instrucción RDPD para activar
 * la memoria flash. Espera el tiempo necesario tras el comando.
 */
void flash_wake_up(void) {
  if (!m_flash_sleeping)
    return;

  qspi_init();

  uint32_t err_code;
  nrf_qspi_cinstr_conf_t cinstr_cfg = {.opcode = QSPI_STD_CMD_RDPD,
                                       .length = NRF_QSPI_CINSTR_LEN_1B,
                                       .io2_level = true,
                                       .io3_level = true,
                                       .wipwait = false,
                                       .wren = false};

  // Enviamos instrucción de despertar
  err_code = nrf_drv_qspi_cinstr_xfer(&cinstr_cfg, NULL, NULL);
  APP_ERROR_CHECK(err_code);

  // Segun la hoja de datos, es de 35 us.
  hal_mcu_wait_us(40);

  m_flash_sleeping = false;
}

/** @brief Busca la primera dirección libre válida en la partición de flash.
 *
 * Escanea la región de flash entre start_addr y end_addr para localizar la
 * primera dirección disponible para escritura. Detecta wrap-around cuando
 * encuentra una cabecera válida en el siguiente sector borrado.
 *
 * @param start_addr Dirección de inicio de la partición.
 * @param end_addr   Dirección final de la partición.
 * @param wrap       Salida que indica si se detectó wrap-around.
 * @return Dirección de la primera posición válida para escribir.
 */
static uint32_t primera_direccion(uint32_t start_addr, uint32_t end_addr, bool *wrap) {
  uint32_t current_addr = start_addr;
  uint32_t check_word;
  uint32_t err;
  uint8_t header_buf[4] __attribute__ ((aligned(4))); // Para leer Header + Len

  while (current_addr < end_addr) { // Lo hacemos para cada direccion dentro del rango fijado
    // Comprobamos si es el principio de un sector y si esta vacio
    if ((current_addr % SECTOR_SIZE) == 0) {
      err = nrf_drv_qspi_read(&check_word, 4, current_addr);
      if (err != NRF_SUCCESS)
        return start_addr;
      WAIT_FOR_PERIPH();

      if (check_word == 0xFFFFFFFF){ // Si el principio de sector no tiene nada escrito o ha sido borrado no tendra cabecera, sera 0xFF
        uint32_t next_sector = current_addr + SECTOR_SIZE; // Si este sector ha sido borrado (por wrap around), compruebo el siguiente
        if (next_sector < end_addr){ // Mientras no haya llegado al final
          uint32_t next_word;
          err = nrf_drv_qspi_read(&next_word, 4, next_sector); // Leo la cabecerea del sector
          if(err == NRF_SUCCESS){
            WAIT_FOR_PERIPH();
            uint8_t next_header = next_word & 0xFF; // Si coincide con un header conocido, entonces es que hay wrap around
            if ((next_header == HEADER_LORA) || (next_header == HEADER_IMU_XL) || (next_header == HEADER_IMU_GY) || (next_header == HEADER_IMU_XL_GY)){
              *wrap = true;
            }
          } 
        }
        
        return current_addr;  
      } 
    }

    // Si no estamos al principio de sector, leemos la cabecera
    err = nrf_drv_qspi_read(header_buf, 4, current_addr);
    if (err != NRF_SUCCESS)
      return start_addr; // Error HW, reiniciamos
    WAIT_FOR_PERIPH();

    uint8_t header = header_buf[0];
    uint16_t data_len = header_buf[1] | (header_buf[2] << 8);
    
    if (header == 0xFF) { // Esa direccion esta vacia, compruebo el siguiente sector por si hubi wrap around
      uint32_t next_sector = (current_addr/SECTOR_SIZE + 1)*SECTOR_SIZE;
      if (next_sector < end_addr){ // Mientras no haya llegado al final
        uint32_t next_word;
        err = nrf_drv_qspi_read(&next_word, 4, next_sector); // Leo la cabecerea del sector
        if(err == NRF_SUCCESS){
          WAIT_FOR_PERIPH();
          uint8_t next_header = next_word & 0xFF; // Si coincide con un header conocido, entonces es que hay wrap around
          if ((next_header == HEADER_LORA) || (next_header == HEADER_IMU_XL) || (next_header == HEADER_IMU_GY) || (next_header == HEADER_IMU_XL_GY)){
            *wrap = true;
          }
        } 
      }

      return current_addr;
    } else if (header == 0xEE) {
      uint32_t offset = current_addr % SECTOR_SIZE;
      current_addr += (SECTOR_SIZE - offset);
      continue;
    } else if (header == HEADER_LORA || header ==  HEADER_IMU_XL || header == HEADER_IMU_GY || header == HEADER_IMU_XL_GY) { // Si la cabezera tiene un dato valido
      if (data_len > SECTOR_SIZE) {
        // Corrupción: asumimos sector perdido y saltamos
        uint32_t offset = current_addr % SECTOR_SIZE;
        current_addr += (SECTOR_SIZE - offset);
        continue;
      }

      uint32_t tamano_util;

      if (header == HEADER_LORA) {
        tamano_util = 1 + 2 + data_len;       //  Cabezera (1) + Longitud de datos (2) + datos (N) (timestamp incluido en el paquete)
      } else {
        tamano_util = 1 + 2 + 1 + 1 + data_len + 4;   // Cabezera (1) + Longitud de datos (2) + BDR (1) + actividada_flag (1) + datos (N) + timestamp (4)
      }

      uint8_t padding = (4 - (tamano_util % 4)) % 4;

      current_addr += (tamano_util + padding);
    } else {
      // Si encontramos algo que no es ni dato, ni skip, ni FF...
      // Por seguridad, saltamos al siguiente sector.
      uint32_t offset = current_addr % SECTOR_SIZE;
      current_addr += (SECTOR_SIZE - offset);
    }
  }

  return start_addr; // Si esta lleno vuelve al principio
}

/** @brief Inicializa la capa de gestión de la memoria flash.
 *
 * Inicializa QSPI, configura la memoria externa y determina las direcciones
 * de escritura actuales para las particiones de Lora e IMU.
 */
void flash_init(void) {
  uint32_t err_code;

  qspi_init();
  configure_memory();

  // Ante reinicios, para recuperar la ultima direccion valida y no empezar desde el principio
  wrap_lora = false;
  wrap_imu = false;
  direccion_flash_lora = primera_direccion(ADDR_LORA_START, ADDR_LORA_END, &wrap_lora); 
  direccion_flash_imu = primera_direccion(ADDR_IMU_START, ADDR_IMU_END, &wrap_imu);

  flash_deep_sleep();
}

/** @brief Escribe un paquete en la región de flash correspondiente.
 *
 * Gestiona el salto de sector, el wrap-around y el borrado antes de escribir
 * los datos proporcionados en la flash externa.
 *
 * @param head_ptr  Puntero a la dirección de escritura actual.
 * @param start_addr Dirección de inicio de la partición.
 * @param end_addr   Dirección final de la partición.
 * @param len        Longitud de los datos a escribir en bytes.
 * @param data       Puntero a los datos que se escribirán.
 * @param wrap_flag  Salida que se marca cuando se produce wrap-around.
 */
static void escritura_en_flash(uint32_t *head_ptr, uint32_t start_addr, uint32_t end_addr, uint16_t len, uint8_t *data, bool *wrap_flag) {
  uint32_t err_code;
  uint32_t current_addr = *head_ptr;

  // Espacio libre en el sector
  uint32_t sector_offset = current_addr % SECTOR_SIZE;
  uint32_t sector_espacio_libre = SECTOR_SIZE - sector_offset;

  if (len > sector_espacio_libre) { // Si no cabe en el espacio libre, rellenamos con una cabecera conocida y pasamos al siguiente sector
    // El hueco siempre será múltiplo de 4, podemos escribir skip marker
    if (sector_espacio_libre >= 4) {
      static uint8_t skip_buf[SECTOR_SIZE] __attribute__((aligned(4)));
      m_finished = false;
      memset(skip_buf, 0xEE, sector_espacio_libre);
      err_code = nrf_drv_qspi_write(skip_buf, sector_espacio_libre, current_addr);
      APP_ERROR_CHECK(err_code);
      WAIT_FOR_PERIPH();
      esperar_wip_flash();
    }
    // Si sector_espacio_libre == 0, no hay hueco, no hay nada que marcar

    current_addr += sector_espacio_libre; // Actualizamos la direccion

    // Comprobamos que no nos hayamos pasado del limite
    if (current_addr >= end_addr) {
      current_addr = start_addr;
      *wrap_flag = true;
    }

    // Hacemos un borrado al cambiar de sector
    m_finished = false;
    err_code = nrf_drv_qspi_erase(NRF_QSPI_ERASE_LEN_4KB, current_addr);
    APP_ERROR_CHECK(err_code);
    if (err_code != NRF_SUCCESS) {
      return;
    }
    WAIT_FOR_PERIPH();
    esperar_wip_flash();
  }

  // Gestion puntero del buffer circular
  if (current_addr + len > end_addr) {
    current_addr = start_addr; // Volvemos al principio de la partición
    *wrap_flag = true;

    // Hacemos un borrado al cambiar de sector
    m_finished = false;
    err_code = nrf_drv_qspi_erase(NRF_QSPI_ERASE_LEN_4KB, current_addr);
    APP_ERROR_CHECK(err_code);
    if (err_code != NRF_SUCCESS) {
      return;
    }
    WAIT_FOR_PERIPH();
    esperar_wip_flash();
  }

  // Si estamos al principio de un sector
  if ((current_addr % SECTOR_SIZE) == 0) {
    // Pero por robustez, forzamos borrado.
    m_finished = false;
    err_code = nrf_drv_qspi_erase(NRF_QSPI_ERASE_LEN_4KB, current_addr);
    APP_ERROR_CHECK(err_code);
    if (err_code != NRF_SUCCESS) {
      return;
    }
    WAIT_FOR_PERIPH();
    esperar_wip_flash();
  }
  
  m_finished = false;
  err_code = nrf_drv_qspi_write(data, len, current_addr);
  APP_ERROR_CHECK(err_code);
  if (err_code != NRF_SUCCESS) {
    return;
  }
  WAIT_FOR_PERIPH();
  esperar_wip_flash();

  *head_ptr = current_addr + len;
}

/** @brief Almacena un paquete de datos LoRa en el buffer temporal.
 *
 * Construye el encabezado del paquete LoRa y copia los datos al buffer
 * circular temporal para su posterior escritura en flash.
 *
 * @param data Puntero a los datos LoRa.
 * @param len  Longitud de los datos en bytes.
 */
void flash_log_lora(uint8_t *data, uint16_t len) {
  if (contador_paquetes >= buffer_temporal_tamano) {
    contador_overflow++;
    return; // buffer lleno, perdemos este paquete
  }
  
  // Accedo a un paquete
  buffer_temporal_t *paquete = &buffer_temporal_paquetes[puntero_buftemp_escritura];

  // Construyo el paquete de datos
  paquete->datos[0] = HEADER_LORA;
  paquete->datos[1] = (uint8_t)(len & 0xFF);
  paquete->datos[2] = (uint8_t)((len >> 8) & 0xFF);

  // Copio los datos
  memcpy(&paquete->datos[3], data, len);

  // Calcular tamaño total del paquete
  uint16_t tamano_util = 3 + len; // header(1) + len(2) + data
  uint8_t padding = (4 - (tamano_util % 4)) % 4;

  if (padding > 0) {
    memset(&paquete->datos[tamano_util], 0x00, padding);
  }

  // Guardamos el tamaño TOTAL y marcamos paquete ocupado
  paquete->tamano_datos = tamano_util + padding;
  paquete->ocupado = true;

  // Avanzamos punteros
  puntero_buftemp_escritura = (puntero_buftemp_escritura + 1) % buffer_temporal_tamano;
  contador_paquetes++;
}

/** @brief Almacena un paquete de datos IMU en el buffer temporal.
 *
 * Construye el encabezado correspondiente a los datos IMU, añade el timestamp
 * y almacena el paquete en el buffer circular para su escritura posterior.
 *
 * @param data      Puntero a los datos IMU.
 * @param len       Longitud de los datos IMU en bytes.
 * @param timestamp Marca temporal asociada a la lectura.
 */
void flash_log_imu(uint8_t *data, uint16_t len, uint32_t timestamp, uint8_t actividad_flags) {

  if (contador_paquetes >= buffer_temporal_tamano) {
    contador_overflow++;
    return; // buffer lleno, perdemos este paquete
  }

  // Accedo a un paquete
  buffer_temporal_t *paquete = &buffer_temporal_paquetes[puntero_buftemp_escritura];

  // Construyo el paquete de datos
  if((configuracion_app.flags & MODULO_IMU_XL) && !(configuracion_app.flags & MODULO_IMU_GY)){
    paquete->datos[0] = HEADER_IMU_XL;
  } else if (!(configuracion_app.flags & MODULO_IMU_XL) && (configuracion_app.flags & MODULO_IMU_GY)) {
    paquete->datos[0] = HEADER_IMU_GY; 
  } else if ((configuracion_app.flags & MODULO_IMU_XL) && (configuracion_app.flags & MODULO_IMU_GY)) {
    paquete->datos[0] = HEADER_IMU_XL_GY;
  }
  
  paquete->datos[1] = (uint8_t)(len & 0xFF);
  paquete->datos[2] = (uint8_t)((len >> 8) & 0xFF);
  paquete->datos[3] = (uint8_t)((configuracion_app.imu_fifo_xl_odr & 0x0F) << 4) | (configuracion_app.imu_fifo_gy_odr & 0x0F);

  paquete->datos[4] = actividad_flags;

  // Copio los datos
  memcpy(&paquete->datos[5], data, len);

  // Copio el timestamp
  memcpy(&paquete->datos[5 + len], &timestamp, sizeof(timestamp));

  // Calcular tamaño total del paquete
  uint16_t tamano_util = 5 + len + 4; // header(1) + len(2) + bdr (1) + flag_actividad (1)  + data + timestamp(4)
  uint8_t padding = (4 - (tamano_util % 4)) % 4;

  if (padding > 0) {
    memset(&paquete->datos[tamano_util], 0x00, padding);
  }

  // Guardamos el tamaño TOTAL y marcamos paquete ocupado
  paquete->tamano_datos = tamano_util + padding;
  paquete->ocupado = true;

  // Avanzamos punteros
  puntero_buftemp_escritura = (puntero_buftemp_escritura + 1) % buffer_temporal_tamano;
  contador_paquetes++;
}

/** @brief Procesa los paquetes pendientes del buffer temporal.
 *
 * Extrae hasta un número limitado de paquetes del buffer temporal y los
 * escribe en la flash correspondiente. Si no hay paquetes pendientes, la
 * memoria flash se pone en modo de bajo consumo.
 */
void flash_procesar_pendientes(void) {
  uint8_t procesados = 0;
  const uint8_t maximo = 5; // Para no saturar, procesamos 5 por cada llamada

  // Si no hay paquetes pendientes, comprobamos si la memoria esta dormida
  if (contador_paquetes == 0) {
    if (!m_flash_sleeping) {
      flash_deep_sleep();
    }
    return;
  }

  // Si hay paquetes y esta dormida, la deespertamos
  if (m_flash_sleeping) {
    flash_wake_up();
  }

  while (contador_paquetes > 0 && procesados < maximo) {
    buffer_temporal_t *paquete = &buffer_temporal_paquetes[puntero_buftemp_lectura];
  
    if (!paquete->ocupado) {
      break; // Por seguridad
    }
    
    if(paquete->datos[0] == HEADER_IMU_XL || paquete->datos[0] == HEADER_IMU_GY || paquete->datos[0] == HEADER_IMU_XL_GY){
      // Escribimos los datos en la flash
      escritura_en_flash(&direccion_flash_imu, ADDR_IMU_START, ADDR_IMU_END, paquete->tamano_datos, paquete->datos, &wrap_imu);
    }else if (paquete->datos[0] == HEADER_LORA) {
      // Escribimos los datos en la flash
      escritura_en_flash(&direccion_flash_lora, ADDR_LORA_START, ADDR_LORA_END, paquete->tamano_datos, paquete->datos, &wrap_lora);
    }

    // Marcamos que el paquete esta libre
    paquete->ocupado = false;

    // Avanzamos punteros
    puntero_buftemp_lectura = (puntero_buftemp_lectura + 1) % buffer_temporal_tamano;
    contador_paquetes--;
    procesados++;
  }

  // Cuando no hay paquetes pendientes, entonces dormimos (si hay alguno pendiente la dejamos despierta para la siguiente iteracion)
  if (contador_paquetes == 0) {
    flash_deep_sleep();
  }
}

/** @brief Lee paquetes de un sector de flash hacia un buffer.
 *
 * Lee paquetes válidos a partir de current_addr hasta end_addr y los copia en
 * el buffer proporcionado. Actualiza el puntero de lectura y devuelve la
 * cantidad de bytes leídos. Únicamente lee hasta donde haya datos útiles.
 *
 * @param current_addr Puntero a la dirección de lectura actual.
 * @param end_addr     Dirección límite de lectura.
 * @param buffer       Buffer de destino.
 * @param buffer_size  Tamaño del buffer en bytes.
 * @param bytes_read   Salida con el número de bytes leídos.
 * @return true si se leyó al menos un paquete válido, false en caso contrario.
 */
bool leer_sector_flash(uint32_t *current_addr, uint32_t end_addr, uint8_t *buffer, uint16_t buffer_size, uint16_t *bytes_read) {
  ret_code_t err;
  uint8_t header_buf[4];
  uint16_t total_bytes_read = 0;
  *bytes_read = 0;

  while (*current_addr < end_addr) {
    // Leemos la cabecera
    m_finished = false;
    err = nrf_drv_qspi_read(header_buf, 4, *current_addr); // Aunque la cabecera sean 3 bytes, leemos 4 por la alineacion a 4 bytes.
    if (err != NRF_SUCCESS) {
      NRF_LOG_ERROR("Error leyendo cabecera en 0x%08X", *current_addr);
      return false;
    }
    WAIT_FOR_PERIPH();

    uint8_t header = header_buf[0];
    uint16_t data_len = header_buf[1] | (header_buf[2] << 8);

    // Si encontramos FF, el resto del sector está vacío
    if (header == 0xFF)  {
      uint32_t offset = *current_addr % SECTOR_SIZE;
      *current_addr += (SECTOR_SIZE - offset);
      break;
    }
    if (header == 0xEE) {
      uint32_t offset = *current_addr % SECTOR_SIZE;
      *current_addr += (SECTOR_SIZE - offset);
      break;
    }

    // Comprobamos los tipos de datos que puede haber
    bool header_valido = (header == HEADER_LORA || header == HEADER_IMU_XL || header == HEADER_IMU_GY || header == HEADER_IMU_XL_GY);
    // Validación extra para evitar leer longitudes absurdas
    bool len_valida = (data_len > 0 && data_len <= SECTOR_SIZE);

    // Si no hay datos validos, saltamos al siguiente sector
    if (!header_valido || !len_valida) {
      uint32_t offset = *current_addr % SECTOR_SIZE;
      uint32_t bytes_siguiente_sector = SECTOR_SIZE - offset;

      *current_addr += bytes_siguiente_sector;
      break;
    }

    // Reconstruir tamaño total del paquete
    uint16_t packet_size;
    if (header == HEADER_LORA) {
      packet_size = 3 + data_len;     // header + len + data
    } else {                          // Cualquier HEADER_IMU
      packet_size = 5 + data_len + 4; // header + len + BDR + actividad_flags + data + timestamp
    }

    // Añadir padding
    uint8_t padding = (4 - (packet_size % 4)) % 4;
    uint16_t packet_size_aligned = packet_size + padding;

    // Verificar que cabe en el buffer
    if (total_bytes_read + packet_size_aligned > buffer_size)
      break;

    // Leemos el sector
    m_finished = false;
    err = nrf_drv_qspi_read(&buffer[total_bytes_read], packet_size_aligned, *current_addr);
    if (err != NRF_SUCCESS) {
      NRF_LOG_ERROR("Error leyendo paquete en 0x%08X", *current_addr);
      return false;
    }
    WAIT_FOR_PERIPH();

    total_bytes_read += packet_size_aligned;
    *current_addr += packet_size_aligned; // Actualizamos puntero de lectura
  }

  *bytes_read = total_bytes_read;

  if (total_bytes_read == 0) {
    return false;
  }

  return true;
}

/** @brief Transmite por BLE todos los datos válidos de una partición de flash
 *
 * Recorre la partición en orden cronológico (empezando tras el punto de
 * wrap-around si lo hay), leyendo sector a sector con leer_sector_flash() y
 * reintentando cada sector hasta MAX_REINTENTOS_SECTOR veces con el protocolo
 * ACK_SECTOR/NACK_SECTOR, hasta agotar la partición o abortar por desconexión.
 *
 * @param start_addr Dirección de inicio de la partición.
 * @param end_addr   Dirección final de la partición.
 * @param write_ptr  Dirección de escritura actual en la partición (cabeza del buffer circular).
 * @param wrap       Indica si la partición ya ha dado la vuelta (wrap-around).
 * @param hay_datos  Salida que se marca a true si se transmitió al menos un sector con datos.
 * @param buffer     Buffer de trabajo para leer y transmitir cada sector.
 * @param buf_size   Tamaño de @p buffer en bytes.
 * @return true si se recorrió toda la partición con éxito (incluido el caso sin datos),
 * false si la sesión se abortó por desconexión o por agotar los reintentos de un sector.
 */
static bool transmitir_region(uint32_t start_addr, uint32_t end_addr, uint32_t write_ptr, bool wrap, bool *hay_datos, uint8_t *buffer, uint16_t buf_size){
  uint16_t bytes_leidos;
 
  uint32_t punto_giro = wrap ? (((write_ptr / SECTOR_SIZE) + 1) * SECTOR_SIZE) : end_addr;
 
  bool primera_vuelta_completa = !wrap;
  direccion_lectura = wrap ? punto_giro : start_addr;
 
  while (true) {
    uint32_t limite_actual = primera_vuelta_completa ? punto_giro : end_addr;
 
    uint32_t sector_start = direccion_lectura;
    if (app_ble_is_disconnected()) return false;
    hal_watchdog_reload();
 
    flash_wake_up();
    bool tiene_datos = leer_sector_flash(&direccion_lectura, limite_actual, buffer, buf_size, &bytes_leidos);
    flash_deep_sleep();
 
    if (!tiene_datos || bytes_leidos == 0) {
      if (wrap && !primera_vuelta_completa) {
        // Terminada la vuelta de los datos antiguos, empezamos la de los recientes
        primera_vuelta_completa = true;
        direccion_lectura = start_addr;
        continue;
      }
      return true; // no queda nada mas que mandar en esta region: terminado con exito
    }
 
    *hay_datos = true;
 
    bool confirmado = false;
    for (uint8_t intento = 0; intento < MAX_REINTENTOS_SECTOR && !confirmado; intento++) {
      if (app_ble_is_disconnected()) return false;
 
      if(!transmision_NUS(buffer, bytes_leidos)) continue; // no se transmitio todo, no mandamos ack
      if(!app_ble_esperar_tx_vacio(TIMEOUT_DRENADO_MS)) continue; // el ack no puede adelantar a los datos
       
      uint16_t crc = crc16_compute(buffer, bytes_leidos, NULL);
      char ack_sector[48];
      int ack_len = snprintf(ack_sector, sizeof(ack_sector), "ACK_SECTOR:%06lX:%u:%u", (unsigned long)sector_start, bytes_leidos, crc);
      
      flags_globales.m_flag_sector_ack_recibido = false; // Limpiar antes de mandar

      app_enviar_datos_nus((uint8_t*)ack_sector, (uint16_t)ack_len);
     
      uint32_t deadline = hal_rtc_get_time_s() + TIMEOUT_SECTOR_S;
      while (!flags_globales.m_flag_sector_ack_recibido) {
        hal_watchdog_reload();
        if (app_ble_is_disconnected()) return false;
        if (hal_rtc_get_time_s() > deadline) break;
        nrf_pwr_mgmt_run();
      }
 
      if (!flags_globales.m_flag_sector_ack_recibido) {
        continue; // timeout esperando CMD_SECTOR_ACK, reintentamos el sector
      }
 
      if (flags_globales.m_sector_ack_status == 0) {
        confirmado = true;
      }
      // status != 0: vuelve a intentar el mismo sector (mismo buffer, no se relee flash)
    }
 
    if (!confirmado) {
      char nack_msg[24];
      int nack_len = snprintf(nack_msg, sizeof(nack_msg), "NACK_SECTOR:%06lX", (unsigned long)sector_start);
      app_enviar_datos_nus((uint8_t*)nack_msg, (uint16_t)nack_len);
      return false; // se agotaron los reintentos: abortamos la sesion
    }
  }
}

/** @brief Inicia la transmisión BLE de los datos almacenados en flash.
 *
 * Envía primero los datos de Lora y luego los de IMU por BLE. Al final, pone
 * la memoria flash en modo de bajo consumo.
 */
void transmision_memoria_ble(void) {
  uint8_t buffer[SECTOR_SIZE] __attribute__((aligned(4)));
  bool hay_datos = false;
  
  uint8_t ack_sector[] = "ACK_START";
  app_enviar_datos_nus(ack_sector, sizeof(ack_sector) - 1);
  
  if(!transmitir_region(ADDR_LORA_START, ADDR_LORA_END, direccion_flash_lora, wrap_lora, &hay_datos, buffer, SECTOR_SIZE)){
    flash_deep_sleep();
    return; // Sesion abortada
  }

  uint8_t msg[] = "ACK_LORA_COMPLETE";
  app_enviar_datos_nus(msg, sizeof(msg)-1);
  
  if(!transmitir_region(ADDR_IMU_START, ADDR_IMU_END, direccion_flash_imu, wrap_imu , &hay_datos, buffer, SECTOR_SIZE)){
    flash_deep_sleep();
    return; // Sesion abortada
  }
 
  if(!hay_datos){
    uint8_t msg[] = "ACK_EMPTY";
    app_enviar_datos_nus(msg, sizeof(msg)-1);
  }else{
    uint8_t msg[] = "ACK_DOWNLOAD_COMPLETE";
    app_enviar_datos_nus(msg, sizeof(msg)-1);
  }

  flash_deep_sleep(); // Dormimos memoria
}

/** @brief Borra sectores de flash de forma secuencial.
 *
 * Borra sectores en la dirección actual de borrado y gestiona el recorrido entre
 * las particiones de Lora e IMU. Cuando se completa todo el borrado, reinicia
 * los punteros y envía una confirmación por BLE.
 */
void borrar_flash(void){
  ret_code_t err_code;

  if (m_flash_sleeping) {
    flash_wake_up();
  }

  m_finished = false;
  err_code = nrf_drv_qspi_erase(NRF_QSPI_ERASE_LEN_4KB, puntero_borrado);
  if (err_code == NRF_ERROR_BUSY) {
    return; 
  }
  APP_ERROR_CHECK(err_code);
  WAIT_FOR_PERIPH();
  esperar_wip_flash();

  puntero_borrado += SECTOR_SIZE;

  if(!wrap_lora && puntero_borrado >= direccion_flash_lora && puntero_borrado < ADDR_IMU_START){    
    puntero_borrado = ADDR_IMU_START;
  }else if((!wrap_imu && puntero_borrado >= direccion_flash_imu) || (wrap_imu && puntero_borrado >= ADDR_IMU_END)){
    reiniciar_punteros_flash();
    flags_globales.m_flag_borrado_flash = false;
    puntero_borrado = ADDR_LORA_START;

    uint8_t ack_data[] = "ACK_ERASE_COMPLETE";
    app_enviar_datos_nus(ack_data, sizeof(ack_data)-1);

    flash_deep_sleep();
  }
}

/** @brief Reinicia los punteros de escritura y wrap-around a su estado inicial. */
void reiniciar_punteros_flash(void) {
  direccion_flash_lora = ADDR_LORA_START;
  direccion_flash_imu = ADDR_IMU_START;
  wrap_lora = false;
  wrap_imu = false;
}

/** @brief Devuelve las direcciones de escritura actuales de cada partición.
 *
 * @param lora Salida con la dirección de escritura en la partición Lora.
 * @param imu  Salida con la dirección de escritura en la partición IMU.
 */
void flash_get_ocupacion(uint32_t *lora, uint32_t *imu) {
  *lora = direccion_flash_lora;
  *imu  = direccion_flash_imu;
}

/** @brief Devuelve el número de paquetes descartados y resetea el contador.
 *
 * @return Número de paquetes descartados por overflow del buffer temporal.
 */
uint8_t flash_get_descarte(void){
  uint8_t val = contador_overflow;
  contador_overflow = 0;
  return val;
}