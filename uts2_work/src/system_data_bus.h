#ifndef SYSTEM_DATA_BUS_H_
#define SYSTEM_DATA_BUS_H_

#include <zephyr/zbus/zbus.h>

/* Структура сообщения АЦП */
struct adc_data_msg {
    uint32_t vdda_mv;
    int32_t core_temp;
    uint32_t channels_mv[16]; // Наш тестовый буфер (8 шагов * 2 канала)
};

/* 
 * ОБЪЯВЛЕНИЕ ДЛЯ ДРУГИХ ФАЙЛОВ.
 * Эти макросы объявляют переменные канала и подписчика как "extern".
 * Благодаря этому компилятор разрешит ссылаться на них в любом .c-файле проекта.
 */
ZBUS_CHAN_DECLARE(adc_data_chan);

/**********************Каналы контроля тока, датчики IDC**************************/

#define NUM_SENSORS 8


struct sensor_reading 
{
    double voltage;
    double current;
};

struct ina_batch_msg 
{
    struct sensor_reading sensors[NUM_SENSORS];
};

#endif /* ADC_ZBUS_H_ */