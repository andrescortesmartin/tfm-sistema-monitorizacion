/*!
 * @file      smtc_shield_lr11x0_common.c
 *
 * @brief     LR11x0-based shield common function implementations
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2022. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted (subject to the limitations in the disclaimer
 * below) provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Semtech corporation nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE GRANTED BY
 * THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT
 * NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL SEMTECH CORPORATION BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * -----------------------------------------------------------------------------
 * --- DEPENDENCIES ------------------------------------------------------------
 */

#include "smtc_hal_gpio.h"
#include "modem_pinout.h"
#include "smtc_shield_lr11xx_common_if.h"
#include "smtc_shield_lr11xx_geoloc_if.h"

/*
 * -----------------------------------------------------------------------------
 * --- PRIVATE MACROS-----------------------------------------------------------
 */

/*
 * -----------------------------------------------------------------------------
 * --- PRIVATE CONSTANTS -------------------------------------------------------
 */

const lr11xx_system_rfswitch_cfg_t rfswitch_cfg = {
    .enable  = LR11XX_SYSTEM_RFSW0_HIGH | LR11XX_SYSTEM_RFSW1_HIGH,
    .standby = 0,
    .rx      = LR11XX_SYSTEM_RFSW0_HIGH,
    .tx      = LR11XX_SYSTEM_RFSW0_HIGH | LR11XX_SYSTEM_RFSW1_HIGH,
    .tx_hp   = LR11XX_SYSTEM_RFSW1_HIGH,
    .tx_hf   = 0,
    .gnss    = 0,
    .wifi    = 0,
};

const smtc_shield_lr11xx_tcxo_cfg_t tcxo_cfg = {
    .has_tcxo             = true,
    .supply               = LR11XX_SYSTEM_TCXO_CTRL_3_0V,
    .startup_time_in_tick = 164,
};

const smtc_shield_lr11xx_lf_clck_cfg_t lf_clk_cfg = {
    .lf_clk_cfg     = LR11XX_SYSTEM_LFCLK_XTAL,
    .wait_32k_ready = true,
};

/*
 * -----------------------------------------------------------------------------
 * --- PRIVATE TYPES -----------------------------------------------------------
 */

/*
 * -----------------------------------------------------------------------------
 * --- PRIVATE VARIABLES -------------------------------------------------------
 */

/*
 * -----------------------------------------------------------------------------
 * --- PRIVATE FUNCTIONS DECLARATION -------------------------------------------
 */

/*
 * -----------------------------------------------------------------------------
 * --- PUBLIC VARIABLES --------------------------------------------------------
 */

/*
 * -----------------------------------------------------------------------------
 * --- PUBLIC FUNCTIONS DEFINITION ---------------------------------------------
 */

void smtc_shield_lr11xx_init( void )
{
    smtc_shield_lr11xx_init_lna_scan( );
}

void smtc_shield_lr11xx_deinit( void )
{
    smtc_shield_lr11xx_deinit_lna_scan( );
}

void smtc_shield_lr11xx_handle_pre_gnss_scan( void )
{
    smtc_shield_lr11xx_set_lna_scan( );
}

void smtc_shield_lr11xx_handle_post_gnss_scan( void )
{
    smtc_shield_lr11xx_reset_lna_scan( );
}

void smtc_shield_lr11xx_handle_pre_wifi_scan( void )
{
    
}

void smtc_shield_lr11xx_handle_post_wifi_scan( void )
{
    
}

void smtc_shield_lr11xx_init_lna_scan( void )
{
    hal_gpio_init_out( RADIO_LNA_CTRL, HAL_GPIO_RESET );
}

void smtc_shield_lr11xx_deinit_lna_scan( void )
{
    hal_gpio_deinit( RADIO_LNA_CTRL );
}

void smtc_shield_lr11xx_set_lna_scan( void )
{
    hal_gpio_set_value( RADIO_LNA_CTRL, HAL_GPIO_SET );
}

void smtc_shield_lr11xx_reset_lna_scan( void )
{
    hal_gpio_set_value( RADIO_LNA_CTRL, HAL_GPIO_RESET );
}

lr11xx_system_reg_mode_t smtc_shield_lr11xx_get_reg_mode( void )
{
    return LR11XX_SYSTEM_REG_MODE_DCDC;
}

const smtc_shield_lr11xx_tcxo_cfg_t smtc_shield_lr11xx_get_tcxo_cfg( void )
{
    return tcxo_cfg;
}

const smtc_shield_lr11xx_lf_clck_cfg_t smtc_shield_lr11xx_get_lf_clk_cfg( void )
{
    return lf_clk_cfg;
}

const lr11xx_system_rfswitch_cfg_t smtc_shield_lr11xx_get_rf_switch_cfg( void )
{
    return rfswitch_cfg;
}


bool smtc_shield_lr11x0_common_rttof_recommended_rx_tx_delay_indicator(uint32_t rf_freq_in_hz, lr11xx_radio_lora_bw_t bw, lr11xx_radio_lora_sf_t sf, uint32_t* delay_indicator) {
  uint8_t row_index;
  uint8_t column_index;

  *delay_indicator = 0u;

  // Traduce el ancho de banda a un índice de fila
  switch (bw) {
    case LR11XX_RADIO_LORA_BW_125:
      row_index = 0;
      break;
    case LR11XX_RADIO_LORA_BW_250:
      row_index = 1;
      break;
    case LR11XX_RADIO_LORA_BW_500:
      row_index = 2;
      break;
    default:
      return false;
  }

  // Traduce el Spreading Factor a un índice de columna
  switch (sf) {
    case LR11XX_RADIO_LORA_SF5:
      column_index = 0;
      break;
    case LR11XX_RADIO_LORA_SF6:
      column_index = 1;
      break;
    case LR11XX_RADIO_LORA_SF7:
      column_index = 2;
      break;
    case LR11XX_RADIO_LORA_SF8:
      column_index = 3;
      break;
    case LR11XX_RADIO_LORA_SF9:
      column_index = 4;
      break;
    case LR11XX_RADIO_LORA_SF10:
      column_index = 5;
      break;
    case LR11XX_RADIO_LORA_SF11:
      column_index = 6;
      break;
    case LR11XX_RADIO_LORA_SF12:
      column_index = 7;
      break;
    default:
      return false;
  }

  // Selecciona la tabla de delay según la frecuencia
  if (rf_freq_in_hz < 600000000) {
    *delay_indicator = rttof_delay_indicator_table_below_600mhz[row_index][column_index];
  } else if ((600000000 <= rf_freq_in_hz) && (rf_freq_in_hz < 2000000000)) {
    *delay_indicator = rttof_delay_indicator_table_from_600mhz_to_2ghz[row_index][column_index];
  } else {
    *delay_indicator = rttof_delay_indicator_table_above_2ghz[row_index][column_index];
  }

  return true;
}

/*
 * -----------------------------------------------------------------------------
 * --- PRIVATE FUNCTIONS DEFINITION --------------------------------------------
 */

/* --- EOF ------------------------------------------------------------------ */
