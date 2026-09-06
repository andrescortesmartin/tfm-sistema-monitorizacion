/**
 * @file main_LSM6DSOX.h
 * @brief Driver principal para el sensor inercial LSM6DSOX (acelerómetro y
 * giroscopio)
 *
 * Este módulo proporciona la interfaz para configurar y operar el sensor
 * LSM6DSOX a través de SPI, incluyendo gestión de FIFO, detección de
 * actividad/inactividad y cálculo de ángulos de orientación.
 */

#ifndef MAIN_LSM6DSOX_H
#define MAIN_LSM6DSOX_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Número máximo de muestras que puede almacenar el buffer IMU
 * @details FIFO de 3 kB, 6 bytes por muestra (3 ejes x 2 bytes por eje): 512 muestras maximo
 */
#define IMU_MUESTRAS_MAX 512

/**
 * @brief Tamaño en bytes de cada muestra del IMU
 * @details Cada muestra contiene 3 ejes de 16 bits (6 bytes totales)
 */
#define IMU_TAMANO_MUESTRA 6

#define IMU_BUFFER_MAX_BYTES (IMU_MUESTRAS_MAX * IMU_TAMANO_MUESTRA)

extern bool lsm6dsox_int_initialized;

/**
 * @brief Buffer para almacenar las muestras del IMU
 * @details Almacena hasta IMU_MUESTRAS_MAX*IMU_TAMANO_MUESTRA valores uint16_t
 */
extern uint16_t imu_buffer[IMU_BUFFER_MAX_BYTES];

/**
 * @brief Unión para representar datos de 3 ejes en formato de 16 bits
 * @details Permite acceder a los datos tanto como array de int16_t o bytes
 */
typedef union {
  int16_t i16bit[3]; /**< Datos como array de 3 enteros de 16 bits (X, Y, Z) */
  uint8_t u8bit[6]; /**< Datos como array de 6 bytes (acceso a nivel de byte) */
} axis3bit16_t;

/**
 * @brief Configura e inicializa el sensor LSM6DSOX
 * @details Inicializa la comunicación SPI, verifica el ID del dispositivo,
 *          configura la FIFO, detección de actividad/inactividad y las
 *          interrupciones asociadas
 */
void configuracion_LSM6DSOX(void);

/**
 * @brief Gestiona las interrupciones generadas por el sensor LSM6DSOX
 * @details Lee el estado de las interrupciones y ejecuta las acciones
 * correspondientes:
 *          - Wake-up: activa la FIFO según configuración
 *          - FIFO threshold: descarga y guarda los datos de la FIFO
 *          - Sleep state: descarga datos remanentes y desactiva la FIFO
 */
void gestion_interrupcion_lsm6dsox(void);

/**
 * @brief Mide los ángulos de orientación (pitch y roll) del sensor
 * @details Lee los datos del acelerómetro y calcula los ángulos pitch y roll
 *          utilizando operaciones trigonométricas. Los datos se añaden al
 * payload de transmisión como valores enteros escalados por 100
 */
void medir_angulo(void);

/**
 * @brief Controla el estado de encendido/apagado del sensor IMU
 * @param stop true para detener el sensor (deshabilitar interrupciones y ODR),
 *             false para reactivar el sensor según la configuración de la
 * aplicación
 */
void imu_stop(bool stop);

#endif // MAIN_LSM6DSOX_H
