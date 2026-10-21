/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f7xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define T_IRQ_Pin GPIO_PIN_2
#define T_IRQ_GPIO_Port GPIOE
#define T_CLK_Pin GPIO_PIN_3
#define T_CLK_GPIO_Port GPIOE
#define T_DO_Pin GPIO_PIN_4
#define T_DO_GPIO_Port GPIOE
#define T_DI_Pin GPIO_PIN_5
#define T_DI_GPIO_Port GPIOE
#define T_CS_Pin GPIO_PIN_6
#define T_CS_GPIO_Port GPIOE
#define USER_BTN_Pin GPIO_PIN_13
#define USER_BTN_GPIO_Port GPIOC
#define AMP_SD_Pin GPIO_PIN_13
#define AMP_SD_GPIO_Port GPIOF
#define BTN_LEFT_Pin GPIO_PIN_0
#define BTN_LEFT_GPIO_Port GPIOG
#define BTN_RIGHT_Pin GPIO_PIN_1
#define BTN_RIGHT_GPIO_Port GPIOG
#define BTN_ENTER_Pin GPIO_PIN_12
#define BTN_ENTER_GPIO_Port GPIOE
#define BTN_BACK_Pin GPIO_PIN_14
#define BTN_BACK_GPIO_Port GPIOE
#define BTN_TALK_Pin GPIO_PIN_15
#define BTN_TALK_GPIO_Port GPIOE
#define BTN_HOME_Pin GPIO_PIN_10
#define BTN_HOME_GPIO_Port GPIOB
#define BTN_UP_Pin GPIO_PIN_2
#define BTN_UP_GPIO_Port GPIOG
#define BTN_DOWN_Pin GPIO_PIN_3
#define BTN_DOWN_GPIO_Port GPIOG
#define SD_CS_Pin GPIO_PIN_2
#define SD_CS_GPIO_Port GPIOD
#define RESET_Pin GPIO_PIN_3
#define RESET_GPIO_Port GPIOD
#define DC_RS_Pin GPIO_PIN_4
#define DC_RS_GPIO_Port GPIOD
#define CS_Pin GPIO_PIN_5
#define CS_GPIO_Port GPIOD

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
