
#ifndef _SMTC_HAL_CONFIG_H
#define _SMTC_HAL_CONFIG_H

//------//
//LR1110//
//------//

#define LR1110_POWER              32            
#define LR1110_IRQ_PIN            40
#define LR1110_NRESER_PIN         42
#define LR1110_BUSY_PIN           43
#define LR1110_SPI_NSS_PIN        44
#define LR1110_SPI_SCK_PIN        45
#define LR1110_SPI_MOSI_PIN       46
#define LR1110_SPI_MISO_PIN       47
#define LR1110_GNSS_ANT_PIN       37

//--------//
//SENSORES//
//--------//

#define SENSOR_POWER              31

#define LSM6DSOX_SPI_SCK_PIN      5
#define LSM6DSOX_SPI_MISO_SDO_PIN 27
#define LSM6DSOX_SPI_MOSI_SDI_PIN 30
#define LSM6DSOX_SPI_CS_PIN       26
#define LSM6DSOX_INTERRUPT1       11
#define LSM6DSOX_INTERRUPT2       16

#define LPS22DF_SCL               2
#define LPS22DF_SDA               3
#define LPS22DF_INTERRUPT         28
#define LPS22DF_ADDR              0x5C

#define GPS_RXD                   6
#define GPS_TXD                   8
#define GPS_POWER                 17

#define VBAT_VALUE                29
#define VBAT_CTRL                 34
#define BATTERY_STATUS            35

//-----//
//FLASH//
//-----//

#define FLASH_SPI_SCK_PIN         24  
#define FLASH_SPI_MOSI_PIN        21  
#define FLASH_SPI_MISO_PIN        22  
#define FLASH_SPI_CS_PIN          20  
#define FLASH_SPI_IO2_PIN         36 //qspi 
#define FLASH_SPI_IO3_PIN         19 //qspi 

//-----//
//OTROS//
//-----//

#define SWITCH                    25
#define LED_R                     15
#define LED_G                     13
#define LED_B                     14

#endif
