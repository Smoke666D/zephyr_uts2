#ifndef ADC_PROCESSOR_H
#define ADC_PROCESSOR_H

#include <zephyr/zbus/zbus.h>
#include "seq_mux_adc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    AO1 = 0,
    AO7 = 1,
    AO13 = 2,
    AVsense1 = 3,
    AO2 = 4,
    AO8 = 5,
    AO14 = 6,
    AVsense2 = 7,    
    AO3 = 8,
    AO9 = 9,
    AO15 = 10,
    AVsense3 = 11,        
    AO4 = 12,
    AO10 = 13,
    AO16 = 14,
    AVsense4 = 15,
    AO5 = 16,
    AO11 = 17,
    AO17 = 18,
    AVsense5 = 19,
    AO6 = 20,                    
    AO12 = 21,                
    AO18 = 22,
    AVsense6 = 23,
    DA11_test1 = 24,
    DA20_test1 = 25,
    DA33_test1 = 26,
    DA44_test1 = 27,
    DA11_test2 = 28,
    DA20_test2 = 29,
    DA33_test2 = 30,
    DA44_test2 = 31,
    TOTAL_CHANNEL_COUNT = 32,
} AIN_MUX_CHANNEL_NUMBER;

#define COMBINATIONS_COUNT 8

#define ADC_NUM_CHANNELS   4
#define ADC_SAMPLES_PER_CH 8
#define ADC_BUF_SIZE       (ADC_NUM_CHANNELS * ADC_SAMPLES_PER_CH)

/* Структура сообщения АЦП */
typedef struct adc_data_msg {
    uint32_t vdda_mv;
    int32_t raw_temp;
    uint32_t channels_mv[TOTAL_CHANNEL_COUNT]; // Наш тестовый буфер (8 шагов * 2 канала)
};

// Макрос внешнего объявления канала
#define DECLARE_ADC_OUT_CHAN_EXT(idx) ZBUS_CHAN_DECLARE(adc_out_chan_##idx)

// Явное объявление каналов для внешних модулей
DECLARE_ADC_OUT_CHAN_EXT(0);
DECLARE_ADC_OUT_CHAN_EXT(1);
DECLARE_ADC_OUT_CHAN_EXT(2);
DECLARE_ADC_OUT_CHAN_EXT(3);
DECLARE_ADC_OUT_CHAN_EXT(4);
DECLARE_ADC_OUT_CHAN_EXT(5);
DECLARE_ADC_OUT_CHAN_EXT(6);
DECLARE_ADC_OUT_CHAN_EXT(7);
DECLARE_ADC_OUT_CHAN_EXT(8);
DECLARE_ADC_OUT_CHAN_EXT(9);
DECLARE_ADC_OUT_CHAN_EXT(10);
DECLARE_ADC_OUT_CHAN_EXT(11);
DECLARE_ADC_OUT_CHAN_EXT(12);
DECLARE_ADC_OUT_CHAN_EXT(13);
DECLARE_ADC_OUT_CHAN_EXT(14);
DECLARE_ADC_OUT_CHAN_EXT(15);
DECLARE_ADC_OUT_CHAN_EXT(16);
DECLARE_ADC_OUT_CHAN_EXT(17);
DECLARE_ADC_OUT_CHAN_EXT(18);
DECLARE_ADC_OUT_CHAN_EXT(19);
DECLARE_ADC_OUT_CHAN_EXT(20);
DECLARE_ADC_OUT_CHAN_EXT(21);
DECLARE_ADC_OUT_CHAN_EXT(22);
DECLARE_ADC_OUT_CHAN_EXT(23);
DECLARE_ADC_OUT_CHAN_EXT(24);
DECLARE_ADC_OUT_CHAN_EXT(25);
DECLARE_ADC_OUT_CHAN_EXT(26);
DECLARE_ADC_OUT_CHAN_EXT(27);
DECLARE_ADC_OUT_CHAN_EXT(28);
DECLARE_ADC_OUT_CHAN_EXT(29);
DECLARE_ADC_OUT_CHAN_EXT(30);
DECLARE_ADC_OUT_CHAN_EXT(31);

#ifdef __cplusplus
}
#endif

#endif /* ADC_PROCESSOR_H */