#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#define AIN_TASK_STACK_SIZE 2024
#define AIN_TASK_PRIORITY 10

#include "stdint.h"


#define COMBINATIONS_COUNT 8

#define ADC_NUM_CHANNELS   5
#define ADC_SAMPLES_PER_CH 8
#define ADC_BUF_SIZE       (ADC_NUM_CHANNELS * ADC_SAMPLES_PER_CH)
#define TOTAL_CHANNEL_COUNT ADC_BUF_SIZE 

#define ADC2_BUF_SIZE  8

/* Структура сообщения АЦП */
typedef struct adc_data_msg {
    float channels_mv[TOTAL_CHANNEL_COUNT]; // Наш тестовый буфер (8 шагов * 2 канала)
};

typedef struct adc_scan_msg {
    float channels_mv[ADC2_BUF_SIZE]; // Наш тестовый буфер (8 шагов * 2 канала)
};

#ifdef __cplusplus
}
#endif