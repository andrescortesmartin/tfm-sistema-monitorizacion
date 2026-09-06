/**
 * @file app_at_fds_datas.h
 * @brief Driver para almacenamiento de datos en flash usando FDS
 *
 * Este módulo gestiona el almacenamiento persistente de la configuración
 * de la aplicación en la memoria flash utilizando el sistema FDS (Flash Data
 * Storage) de Nordic.
 */

#ifndef __APP_AT_AT_FDS_DATAS_H
#define __APP_AT_AT_FDS_DATAS_H

#include "fds.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @def CONFIG_FILE1
 * @brief Identificador del archivo de configuración en FDS
 */
#define CONFIG_FILE1 (0x4010)

/**
 * @def CONFIG_REC_KEY1
 * @brief Clave del registro de configuración en FDS
 */
#define CONFIG_REC_KEY1 (0x7010)

/**
 * @brief Inicializa el sistema FDS
 * @details Inicializa el Flash Data Storage, registra eventos y carga
 *          la configuración almacenada si existe
 */
void app_fds_init(void);

/**
 * @brief Guarda la configuración actual en flash
 * @details Escribe la estructura de configuración en FDS de forma persistente
 */
void app_fds_store_save(void);

#ifdef __cplusplus
}
#endif

#endif