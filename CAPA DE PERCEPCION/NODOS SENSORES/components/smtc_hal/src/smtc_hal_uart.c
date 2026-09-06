
#include <string.h>
#include "app_uart.h"
#include "app_error.h"
#include "nrf_uart.h"
#include "nrf_drv_uart.h"
#include "smtc_hal_uart.h"
#include "smtc_hal_config.h"
#include "smtc_hal_gpio.h"
#include "smtc_hal_mcu.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"

static void uart0_handleEvent( app_uart_evt_t *pEvent );
static void uart1_handleEvent( app_uart_evt_t *pEvent );

APP_UART_DEF( uart0, 0, UART_TX_RX_BUF_SIZE, uart0_handleEvent );
APP_UART_DEF( uart1, 1, UART_TX_RX_BUF_SIZE, uart1_handleEvent );

static uint8_t s_uart0ReadDataBuffer[UART_TX_RX_BUF_SIZE] = {0};
static uint16_t s_index = 0;

static bool uart_0_init = false;
static bool uart_1_init = false;

static uint16_t s_current_line_index = 0;
static char gps_line_temp[256];
static uint16_t gps_line_temp_index = 0;

char gps_line_rmc[128];
char gps_line_gga[128];
volatile bool flag_gps_rmc_ready = false;
volatile bool flag_gps_gga_ready = false;

static app_uart_comm_params_t const commParams_0 =
{
    .rx_pin_no    = GPS_RXD,
    .tx_pin_no    = GPS_TXD,
    .rts_pin_no   = NRF_UART_PSEL_DISCONNECTED,
    .cts_pin_no   = NRF_UART_PSEL_DISCONNECTED,                    
    .flow_control = APP_UART_FLOW_CONTROL_DISABLED,
    .use_parity   = false,
    .baud_rate    = NRF_UART_BAUDRATE_115200
};

static app_uart_comm_params_t const commParams_1 =
{
    .rx_pin_no    = NRF_UART_PSEL_DISCONNECTED,
    .tx_pin_no    = NRF_UART_PSEL_DISCONNECTED,
    .rts_pin_no   = NRF_UART_PSEL_DISCONNECTED,
    .cts_pin_no   = NRF_UART_PSEL_DISCONNECTED,                    
    .flow_control = APP_UART_FLOW_CONTROL_DISABLED,
    .use_parity   = false,
    .baud_rate    = NRF_UART_BAUDRATE_921600
};

bool read_gps_line(char *buffer, size_t max_len){
  
    uint8_t byte;
    size_t len = 0;

    // Esperamos inicio de línea
    do {
        if (app_uart_get(&uart0, &byte) != NRF_SUCCESS) continue;
    } while (byte != '$');

    buffer[len++] = byte;

    // Leer hasta '\n' o llenar el buffer
    while (len < max_len - 1)
    {
        if (app_uart_get(&uart0, &byte) != NRF_SUCCESS) continue;

        buffer[len++] = byte;

        if (byte == '\n') {
            buffer[len] = '\0'; // Termina la cadena
            return true;
        }
    }

    buffer[0] = '\0'; // Error: línea muy larga
    return false;
}

void hal_uart_0_init( void )
{	
    uint32_t errCode = 0;
    uint8_t cnt = 0;

    if( uart_0_init == false )
    {
        uart0.comm_params = &commParams_0;
        
        while( true )
        {
            errCode = app_uart_init( &uart0, &uart0_buffers, APP_IRQ_PRIORITY_LOWEST );
            APP_ERROR_CHECK(errCode);
            if( errCode == NRF_SUCCESS )
            {
                uart_0_init = true;
                break;
            }

            hal_mcu_wait_us( 100 );
            
            cnt ++;
            if( cnt > 10 )
            {
               break;
            }
        }
    }
}

void hal_uart_0_deinit( void )
{
    if( uart_0_init == true )
    {
        uart_0_init = false;
        app_uart_close( &uart0 );
    }
}

void hal_uart_0_tx( uint8_t* buff, uint16_t len )
{
    if( uart_0_init == true )
    {
        for( uint16_t i = 0; i < len; i++ )
        {
            app_uart_put( &uart0, buff[i] );
        }
    }
}

void hal_uart_1_init( void )
{
    uint32_t errCode;
    uint8_t cnt = 0;
    
    if( uart_1_init == false )
    {
        uart1.comm_params = &commParams_1;
        
        while( true )
        {
            errCode = app_uart_init( &uart1, &uart1_buffers, APP_IRQ_PRIORITY_LOWEST );
            
            if( errCode == NRF_SUCCESS )
            {
                uart_1_init = true;
                break;
            }
            
            hal_mcu_wait_us( 100 );

            cnt ++;
            if( cnt > 10 )
            {
               break;
            }
        }
    }
}

void hal_uart_1_deinit( void )
{
    if( uart_1_init == true )
    {
        uart_1_init = false;
        app_uart_close( &uart1 );
    }
}

void hal_uart_1_flush( void )
{
    if( uart_1_init == true )
    {
        app_uart_flush( &uart1 );
    }
}

void hal_uart_1_get( uint8_t *p_byte )
{
    if( uart_1_init == true )
    {
        app_uart_get( &uart1, p_byte );
    }
}

void hal_uart_1_put( uint8_t byte )
{
    if( uart_1_init == true )
    {
        app_uart_put( &uart1, byte );
    }
}

void hal_uart_1_tx( uint8_t *buff, uint16_t len )
{
    if( uart_1_init == true )
    {
        nrf_drv_uart_tx( &uart1, buff, len );
        hal_mcu_wait_us( len * 100 );
    }
}

void hal_uart_1_rx( uint8_t *buff, uint16_t len )
{
    if( uart_1_init == true )
    {
        for( uint16_t i = 0; i < len; i++ )
        {
            app_uart_get( &uart1, &buff[i] );
        }
    }
}

static void uart0_handleEvent( app_uart_evt_t *pEvent ){
  uint8_t byte;
  
  switch( pEvent -> evt_type ){
    case APP_UART_DATA_READY:  
      while (app_uart_get(&uart0, &byte) == NRF_SUCCESS){
        if (gps_line_temp_index >= sizeof(gps_line_temp) - 1) {
            gps_line_temp_index = 0; // Resetear buffer si se desborda
            continue; 
        }
        gps_line_temp[gps_line_temp_index++] = byte;

        if (byte == '\n') {
          gps_line_temp[gps_line_temp_index] = '\0';

          // Validar que empieza por '$' y tiene longitud mínima
          if (gps_line_temp[0] == '$' && gps_line_temp_index > 6) {

            // Aislar cabecera (hasta la primera coma)
            char *comma = strchr(gps_line_temp, ',');
            if (comma != NULL) {
              char header[16] = {0};
              ptrdiff_t header_len = comma - gps_line_temp;

              if (header_len < (ptrdiff_t)sizeof(header)) {
                memcpy(header, gps_line_temp, header_len);

                if (strstr(header, "RMC") != NULL) {
                  if (!flag_gps_rmc_ready) {
                    strcpy(gps_line_rmc, gps_line_temp);
                    flag_gps_rmc_ready = true;
                  }
                } else if (strstr(header, "GGA") != NULL) {
                  if (!flag_gps_gga_ready) {
                    strcpy(gps_line_gga, gps_line_temp);
                    flag_gps_gga_ready = true;
                  }
                }
              }
            }
          }
          gps_line_temp_index = 0;
        }
      }
    break;
    case APP_UART_FIFO_ERROR:

    break;

    case APP_UART_COMMUNICATION_ERROR:

    break;

    default:
    break;
  }
}

static void uart1_handleEvent( app_uart_evt_t *pEvent )
{
    switch( pEvent -> evt_type )
    {
        case APP_UART_TX_EMPTY:
        break;

        default:
        break;
    }
}
