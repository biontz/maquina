/* Host stub for the firmware's main.h: only the jack-detect pins the mixer reads. */
#ifndef __MAIN_H
#define __MAIN_H
#include "stm32f7xx_hal.h"
#define switch_l1_Pin GPIO_PIN_6
#define switch_l1_GPIO_Port GPIOD
#define switch_r1_Pin GPIO_PIN_7
#define switch_r1_GPIO_Port GPIOD
#define switch_l2_Pin GPIO_PIN_3
#define switch_l2_GPIO_Port GPIOB
#define switch_r2_Pin GPIO_PIN_4
#define switch_r2_GPIO_Port GPIOB
#define USE_SD_CARD 1
void Error_Handler(void);
#endif
