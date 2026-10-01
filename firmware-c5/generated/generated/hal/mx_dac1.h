/**
  ******************************************************************************
  * @file           : mx_dac1.h
  * @brief          : Header for mx_dac1.c file.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the mx_stm32c5xx_hal_drivers_license.md file
  * in the same directory as the generated code.
  * If no mx_stm32c5xx_hal_drivers_license.md file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef MX_DAC1_H
#define MX_DAC1_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Includes ------------------------------------------------------------------*/
#include "stm32_hal.h"

/* Exported types ------------------------------------------------------------*/
/* Exported constants --------------------------------------------------------*/

/* Exported macros -----------------------------------------------------------*/
/* Exported variables --------------------------------------------------------*/
/* Exported functions ------------------------------------------------------- */
/******************************************************************************/
/* Exported functions for DAC in HAL layer */
/******************************************************************************/
/**
  * @brief mx_dac1 init function
  * This function configures the hardware resources used in this example
  * @retval pointer to handle or NULL in case of failure
  */
hal_dac_handle_t *mx_dac1_init(void);

/**
  * @brief  De-initialize dac1 instance and return it.
  */
void mx_dac1_deinit(void);

/**
  * @brief  Get the DAC1 object.
  * @retval Pointer on the DAC1 Handle
  */
hal_dac_handle_t *mx_dac1_gethandle(void);

/******************************************************************************/
/*                      LPDMA1 channel1 global interrupt                      */
/******************************************************************************/
void LPDMA1_CH1_IRQHandler(void);

/******************************************************************************/
/*                            DAC global interrupt                            */
/******************************************************************************/
void DAC1_IRQHandler(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* MX_DAC1_H */
