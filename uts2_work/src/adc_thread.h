#ifndef ADC_THREAD_H
#define ADC_THREAD_H


#include <zephyr/kernel.h>

#define MUX_PORT GPIOE
#define MUX_A0  GPIO_PIN_12
#define MUX_A1  GPIO_PIN_13
#define MUX_A2  GPIO_PIN_14
#define GPIO_MASK   (MUX_A0 | MUX_A1 | MUX_A2)

#define MUX_PORT_RCC  LL_AHB4_GRP1_PERIPH_GPIOE


#define ADC_PORT  GPIOA
#define MUX1_ADC_VSense GPIO_PIN_0
#define MUX2_ADC_VSense GPIO_PIN_1
#define MUX3_ADC_VSense GPIO_PIN_3
#define MUX4_ADC_VSense GPIO_PIN_4
#define MUX5_ADC_VSense GPIO_PIN_5

#define ADC_PORT_RCC  LL_AHB4_GRP1_PERIPH_GPIOA

#define ADC_CHANNELS     5
#define SAMPLES_PER_CH   16   /* 16 сэмплов на канал в одном цикле */
#define BUF_SIZE         (ADC_CHANNELS * SAMPLES_PER_CH)

int sync_capture_start(void);




#endif