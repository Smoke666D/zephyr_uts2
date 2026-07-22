#ifndef SYSTEM_DATA_BUS_H_
#define SYSTEM_DATA_BUS_H_

#include <zephyr/zbus/zbus.h>

#include "adc_thread.h"



/* Структура сообщения АЦП */
struct adc_data_msg {
    uint32_t vdda_mv;
    int32_t raw_temp;
    uint32_t channels_mv[TOTAL_CHANNEL_COUNT]; // Наш тестовый буфер (8 шагов * 2 канала)
};

/* 
 * ОБЪЯВЛЕНИЕ ДЛЯ ДРУГИХ ФАЙЛОВ.
 * Эти макросы объявляют переменные канала и подписчика как "extern".
 * Благодаря этому компилятор разрешит ссылаться на них в любом .c-файле проекта.
 */
ZBUS_CHAN_DECLARE(adc_data_chan);


/**********************Каналы контроля тока, датчики IDC**************************/

#define NUM_SENSORS 8


/* Названия датчиков для понятного логирования */
typedef enum
 {
    BRD_LOW     = 0,
    BRD_HIGH    = 1,
    VDUT2       = 2,
    VDUT3       = 3,
    VIN         = 4,
    DCDC_3_3    = 5,
    VDOUT_PWR   = 6,
    VD5         = 7,
} CUR_SENS_CHANEL;

struct sensor_reading 
{
    double voltage;
    double current;
};

struct ina_batch_msg 
{
    struct sensor_reading sensors[NUM_SENSORS];
};

ZBUS_CHAN_DECLARE(ina_batch_chan);



/***********************Конфигурация для драйвера слаботочных каналов и питания LIN************************************/
#define LOW_CUR_DRIVER_COUNT 18  // Количество каналов слаботочных выходов
#define LIN_COUNT            4   // Количество каналов управлением питания LIN

// Состояния слаботочных каналов
typedef enum 
{
    INPUT  = 0,
    OUT_LOW = 1,
    OUT_HIGH = 2,
} LOW_CUR_DRIVER_STATE;

// Стуктура сообщения управления для канала ZBUS для управления драйвером
struct hc595_channels_msg
{
    LOW_CUR_DRIVER_STATE low_cur_driver_channels[LOW_CUR_DRIVER_COUNT];
    bool lin_pb[LIN_COUNT];
};


/* Статусы каналов управления */
typedef enum 
{
    CHAN_STATUS_NORMAL        = 0,  /* Нормальная работа (или канал в режиме INPUT) */
    CHAN_STATUS_SHORT_TO_GND  = 1,  /* Короткое замыкание на землю (OUT_HIGH, но напряжение упало) */
    CHAN_STATUS_SHORT_TO_PWR  = 2,  /* Короткое замыкание на питание (OUT_LOW, но напряжение поднялось) */
} HC595_CHAN_STATUS;

/* Структура сообщения обратной связи в Zbus */
struct hc595_status_msg 
{
    HC595_CHAN_STATUS channel_status[LOW_CUR_DRIVER_COUNT]; /* Статус каждого из 18 каналов */
    bool global_overcurrent;                                /* Общий повышенный ток потребления */
    bool system_fault_locked;                               /* Флаг блокировки системы по аварии */
};

#endif /* ADC_ZBUS_H_ */