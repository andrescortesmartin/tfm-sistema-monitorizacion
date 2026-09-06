/**
 * @file GPS.c
 * @brief Implementación del driver GPS con decodificación NMEA
 *
 * Gestiona el módulo GPS mediante UART, decodifica sentencias NMEA (RMC y GGA)
 * y extrae coordenadas para transmisión.
 */

#include "GPS.h"
#include "main_lorawan.h"
#include "minmea.h"
#include "nrf_log.h"
#include "smtc_hal.h"

/** @brief Estructura para datos de sentencia RMC */
static struct minmea_sentence_rmc frame_rmc;

/** @brief Estructura para datos de sentencia GGA */
static struct minmea_sentence_gga frame_gga;

static int32_t  gps_lat_cached = 0;
static int32_t  gps_lon_cached = 0;

/**
 * @brief Calcula checksum Fletcher-8 para mensajes UBX
 * @param data Bytes sobre los que calcular (clase+id+longitud+payload)
 * @param len  Longitud de esos bytes
 * @param ck_a Resultado CK_A
 * @param ck_b Resultado CK_B
 */
static void ubx_checksum(const uint8_t *data, uint16_t len, uint8_t *ck_a, uint8_t *ck_b) {
  *ck_a = 0;
  *ck_b = 0;
  for (uint16_t i = 0; i < len; i++) {
      *ck_a += data[i];
      *ck_b += *ck_a;
  }
}

/**
 * @brief Envía un mensaje UBX por UART0
 * @param payload Clase+ID+longitud+datos, sin header 0xB5 0x62 ni checksum
 * @param len     Longitud del payload
 */
static void ubx_send_cmd(const uint8_t *payload, uint16_t len) {
  uint8_t ck_a, ck_b;
  ubx_checksum(payload, len, &ck_a, &ck_b);

  uint8_t header[2] = {0xB5, 0x62};
  uint8_t checksum[2] = {ck_a, ck_b};

  hal_uart_0_tx(header, sizeof(header));
  hal_uart_0_tx((uint8_t *)payload, len);
  hal_uart_0_tx(checksum, sizeof(checksum));
}

/**
 * @brief Configura el módulo GPS desactivando sentencias NMEA innecesarias
 * @details Desactiva GLL, GSV, VTG y GSA por UART1 mediante CFG-VALSET en RAM.
 *          Configura la frecuencia de 10 Hz a 1 Hz
 *          Mantiene activas únicamente RMC y GGA.
 */
static void gps_configurar(void) {
  // Reduce la tasa de refresco del módulo de 10Hz (defecto FPV) a 1Hz.
  // CFG-RATE-MEAS (key 0x30210001): periodo de medida en ms, valor 1000 (0x03E8 LE)
  static const uint8_t cfg_valset_rate[] = {
      0x06, 0x8A, 0x0A, 0x00,        // CFG-VALSET, longitud 10 bytes
      0x00, 0x01, 0x00, 0x00,        // version=0, capa RAM, reserved
      0x01, 0x00, 0x21, 0x30,        // key CFG-RATE-MEAS en little-endian
      0xE8, 0x03,                    // valor 1000ms en uint16 little-endian
  };

  // Mantiene activas únicamente RMC (timestamp) y GGA (calidad+fix+HDOP).
  // Cada entrada es: key (4 bytes LE) + valor 0x00 (desactivar)
  static const uint8_t cfg_valset_msgs[] = {
      0x06, 0x8A, 0x18, 0x00,        // CFG-VALSET, longitud 24 bytes
      0x00, 0x01, 0x00, 0x00,        // version=0, capa RAM, reserved
      0xCA, 0x00, 0x91, 0x20, 0x00,  // CFG-MSGOUT-NMEA_ID_GLL_UART1  → off
      0xC5, 0x00, 0x91, 0x20, 0x00,  // CFG-MSGOUT-NMEA_ID_GSV_UART1  → off
      0xB1, 0x00, 0x91, 0x20, 0x00,  // CFG-MSGOUT-NMEA_ID_VTG_UART1  → off
      0xC0, 0x00, 0x91, 0x20, 0x00,  // CFG-MSGOUT-NMEA_ID_GSA_UART1  → off
  };

  ubx_send_cmd(cfg_valset_rate, sizeof(cfg_valset_rate));
  ubx_send_cmd(cfg_valset_msgs, sizeof(cfg_valset_msgs));
}

/**
 * @brief Procesa líneas GPS en background y apaga el módulo si el fix es válido
 * @details Llamar desde el main loop mientras el GPS está encendido.
 *          Apaga el GPS en cuanto RMC y GGA son válidos, sin esperar a COMPROBAR_FIX.
 */
void gps_process_background(void) {
  if (!(configuracion_app.flags & MODULO_GPS)) return;
  if (!flag_gps_gga_ready) return;

  medir_GPS();

  if (flags_globales.m_flag_fix_valido) {
    // Guardar coordenadas en cuanto tenemos fix
    gps_lat_cached = (int32_t)(minmea_tocoord(&frame_gga.latitude)  * 1000000);
    gps_lon_cached = (int32_t)(minmea_tocoord(&frame_gga.longitude) * 1000000);

    ultimas_medidas.gps_lat  = gps_lat_cached;
    ultimas_medidas.gps_lon  = gps_lon_cached;
    ultimas_medidas.ts_gps   = get_timestamp();

    // Sincronizar timestamp
    struct tm gps_t = {0};
    if (frame_rmc.date.year != 0 && minmea_getdatetime(&gps_t, &frame_rmc.date, &frame_rmc.time)) {
      gps_t.tm_isdst = 0;
      sync_timestamp((uint32_t)mktime(&gps_t));
    }

    gps_off();
  }
}

/**
 * @brief Enciende el módulo GPS
 * @details Inicializa la UART0 y activa la alimentación del GPS
 */
void gps_on(void) {
  hal_uart_0_init();
  hal_gpio_set_value(GPS_POWER, HAL_GPIO_SET);

  nrf_delay_ms(500);
  gps_configurar();
}

/**
 * @brief Apaga el módulo GPS
 * @details Corta la alimentación del GPS y desinicializa la UART0
 */
void gps_off(void) {
  hal_gpio_set_value(GPS_POWER, HAL_GPIO_RESET); // Apaga alimentación del GPS
  hal_uart_0_deinit();                           // Libera recursos de UART
}

/**
 * @brief Lee y decodifica datos del GPS si están disponibles
 * @details Verifica si hay una línea NMEA completa, la imprime por debug,
 *          la decodifica y limpia la bandera de línea lista
 */
void medir_GPS(void) {
  bool gga_ok = false;
  
  if(flag_gps_rmc_ready){
    minmea_parse_rmc(&frame_rmc, gps_line_rmc);
    flag_gps_rmc_ready = false;
  }

  if (flag_gps_gga_ready) {
    if (minmea_parse_gga(&frame_gga, gps_line_gga)) {
      float hdop = minmea_tofloat(&frame_gga.hdop);

      if (frame_gga.fix_quality > 0 && minmea_tofloat(&frame_gga.hdop) <= GPS_HDOP_MAX) {
        gga_ok = true;
      }
    }
    flag_gps_gga_ready = false;
  }

  // Fix válido solo si ambas sentencias pasan
  if (gga_ok) {
    flags_globales.m_flag_fix_valido = true;
  } else {
    // Solo actualizar a false si ya procesamos ambas y alguna falló
    flags_globales.m_flag_fix_valido = false;
  }
}
