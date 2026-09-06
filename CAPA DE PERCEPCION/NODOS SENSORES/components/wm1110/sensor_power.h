/**
 * @file sensor_power.h
 * @brief Gestión de alimentación y encendido/apagado de los sensores y el LR1110
 *
 * Este módulo controla el pin de alimentación compartido de los sensores I2C/SPI
 * (LSM6DSOX, LPS22DF) y la secuencia de encendido/apagado del LR1110.
 */

#ifndef SENSOR_POWER_H
#define SENSOR_POWER_H

/**
 * @brief Enciende el LSM6DSOX y lo configura
 * @details Activa la alimentación de sensores si estaba apagada, inicializa el
 * bus SPI y configura el sensor; si la configuración falla, lo vuelve a apagar
 */
void lsm6dsox_encender(void);

/**
 * @brief Apaga el LSM6DSOX y libera sus recursos
 * @details Detiene el sensor, libera el bus SPI, pone sus pines en alta impedancia
 * y actualiza la alimentación compartida si ya no hace falta
 */
void lsm6dsox_apagar(void);

/**
 * @brief Enciende el LPS22DF y lo configura
 * @details Activa la alimentación de sensores si estaba apagada, inicializa el
 * bus I2C y configura el sensor; si la configuración falla, lo vuelve a apagar
 */
void lps22df_encender(void);

/**
 * @brief Apaga el LPS22DF y libera sus recursos
 * @details Libera el bus I2C, pone sus pines en alta impedancia y actualiza la
 * alimentación compartida si ya no hace falta
 */
void lps22df_apagar(void);

/**
 * @brief Enciende o apaga cada sensor según la configuración de la aplicación
 * @details Enciende/apaga el LSM6DSOX según los módulos de IMU/ángulo, y el
 * LPS22DF según el módulo de ambiente; reinicia el LSM6DSOX si ya estaba activo
 * para partir de un estado limpio
 */
void configurar_sensores(void);

/**
 * @brief Enciende el LR1110 y prepara sus pines para operación normal
 * @details No hace nada si ya estaba encendido. Alimenta el chip, pone todos
 * sus pines a nivel bajo durante el rearranque y luego los configura para uso
 * normal (SPI, IRQ, reset), e inicializa el bus SPI
 */
void encender_lr1110( void );

/**
 * @brief Apaga el LR1110 de forma limpia
 * @details No hace nada si ya estaba apagado. Libera el SPI, resetea el chip y
 * pone sus pines en bajo antes de cortar la alimentación, para evitar corrientes
 * de fuga
 */
void apagar_lr1110( void );

#endif // SENSOR_POWER_H