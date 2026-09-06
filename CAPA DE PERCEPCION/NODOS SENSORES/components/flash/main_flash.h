/**
 * @file main_flash.h
 * @brief Driver de almacenamiento en memoria flash externa QSPI
 *
 * Gestiona la memoria flash externa (QSPI) usada para guardar de forma
 * persistente los envíos periódicos LoRa y los datos de IMU, incluyendo un
 * buffer temporal en RAM, escritura/lectura por sectores con detección de
 * wrap-around, descarga por BLE y borrado incremental.
 */

#ifndef MAIN_FLASH_H
#define MAIN_FLASH_H

#include <stdbool.h>
#include <stdint.h>
#include "main_LSM6DSOX.h"

#define QSPI_STD_CMD_WRSR 0x01  /**< Comando Write Status Register: escribe el registro de estado */
#define QSPI_STD_CMD_RSTEN 0x66 /**< Comando Reset Enable: habilita el siguiente comando de reset */
#define QSPI_STD_CMD_RST 0x99   /**< Comando Reset: ejecuta el reset del chip de flash */
#define QSPI_STD_CMD_RDSR 0x05  /**< Comando Read Status Register: lee el registro de estado */
#define QSPI_STD_CMD_DPD 0xB9   /**< Comando Deep Power Down: entra en bajo consumo profundo */
#define QSPI_STD_CMD_RDPD 0xAB  /**< Comando Release from Deep Power Down: sale de bajo consumo profundo */

#define ADDR_LORA_START 0x001000 /**< Inicio de la partición Lora (deja el primer sector libre) */
#define ADDR_LORA_END 0x0FFFFF   /**< Fin de la partición Lora (1 MB para los envíos periódicos) */
#define ADDR_IMU_START 0x100000  /**< Inicio de la partición IMU (7 MB para los datos de IMU) */
#define ADDR_IMU_END 0x7FFFFF    /**< Fin de la partición IMU */

#define buffer_temporal_tamano 10 /**< Cuántos "paquetes" puede guardar como máximo el buffer temporal en RAM */

#define MAX_REINTENTOS_SECTOR 5      /**< Reintentos de un mismo sector antes de abortar la descarga BLE */
#define MAX_REINTENTOS_INDICATION 3  /**< Reintentos de la indicación de ACK_SECTOR */
#define TIMEOUT_SECTOR_S 10          /**< Timeout de espera por confirmación (indication o CMD_SECTOR_ACK), en segundos */
#define TIMEOUT_DRENADO_MS 5000      /**< Espera máxima a que se termine de vaciar la cola de TX antes del ACK, en ms */

/** @brief Dirección de borrado actual en la memoria flash */
extern volatile uint32_t puntero_borrado;

/** @brief Paquete pendiente de escritura en flash, almacenado en el buffer temporal en RAM */
typedef struct{
  uint8_t datos[5 + IMU_MUESTRAS_MAX*IMU_TAMANO_MUESTRA + 4 + 3] __attribute__((aligned(4))); /**< Paquete ya construido (cabecera + payload + relleno) listo para escribir */
  uint16_t tamano_datos; /**< Tamaño total del paquete en @c datos, en bytes */
  bool ocupado;          /**< true si esta posición del buffer contiene un paquete pendiente de escribir */
}buffer_temporal_t;

/**
 * @brief Inicializa la capa de gestión de la memoria flash
 * @details Inicializa QSPI, configura la memoria externa y determina las
 * direcciones de escritura actuales para las particiones de Lora e IMU, y
 * deja la flash en modo de bajo consumo
 */
void flash_init(void);

/**
 * @brief Almacena un paquete de datos LoRa en el buffer temporal
 * @param data Puntero a los datos LoRa
 * @param len Longitud de los datos en bytes
 */
void flash_log_lora(uint8_t *data, uint16_t len);

/**
 * @brief Almacena un paquete de datos IMU en el buffer temporal (no bloqueante)
 * @param data Puntero a los datos IMU
 * @param len Longitud de los datos IMU en bytes
 * @param timestamp Marca temporal asociada a la lectura
 * @param actividad_flags Flags @c ACTIVITY_* que describen el origen del paquete dentro del burst
 */
void flash_log_imu(uint8_t *data, uint16_t len, uint32_t timestamp, uint8_t actividad_flags);

/** @brief Reinicia los punteros de escritura y wrap-around a su estado inicial */
void reiniciar_punteros_flash(void);

/**
 * @brief Inicia la transmisión BLE de los datos almacenados en flash
 * @details Envía primero los datos de Lora y luego los de IMU por BLE. Al
 * final, pone la memoria flash en modo de bajo consumo
 */
void transmision_memoria_ble(void);

/**
 * @brief Procesa los paquetes pendientes del buffer temporal
 * @details Extrae hasta un número limitado de paquetes del buffer temporal y
 * los escribe en la flash correspondiente. Si no hay paquetes pendientes, la
 * memoria flash se pone en modo de bajo consumo
 */
void flash_procesar_pendientes(void);

/**
 * @brief Borra sectores de flash de forma secuencial
 * @details Borra sectores en la dirección actual de borrado y gestiona el
 * recorrido entre las particiones de Lora e IMU. Cuando se completa todo el
 * borrado, reinicia los punteros y envía una confirmación por BLE
 */
void borrar_flash(void);

/**
 * @brief Devuelve las direcciones de escritura actuales de cada partición
 * @param lora Salida con la dirección de escritura en la partición Lora
 * @param imu Salida con la dirección de escritura en la partición IMU
 */
void flash_get_ocupacion(uint32_t *lora, uint32_t *imu);

/**
 * @brief Devuelve el número de paquetes descartados y resetea el contador
 * @return Número de paquetes descartados por overflow del buffer temporal
 */
uint8_t flash_get_descarte(void);
#endif // MAIN_FLASH_H
