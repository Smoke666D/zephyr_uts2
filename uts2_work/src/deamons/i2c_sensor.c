/**
 *  @file       i2c_sensor.c
 *
 *  @date       2026.09.26
 *  @author     Dymov Igor
 *
 *  @brief      Планировщик фонового опроса I2C-датчиков температуры и освещенности
 *  @details    Реализует периодический опрос датчиков TMP112 и BH1750 на базе
 *              отложенной задачи воркера Zephyr RTOS и публикацию результатов в ZBUS.
 */

/***************************************************************************************************
 *                                          INCLUDED FILES
 **************************************************************************************************/
#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/gpio.h>

#include "app_worker.h"
#include "system_bus_model.h"
#include "system_define.h"

/***************************************************************************************************
 *                                           DEFINITIONS
 **************************************************************************************************/
LOG_MODULE_REGISTER(sensor_poll, LOG_LEVEL_INF);

/***************************************************************************************************
 *                                          PRIVATE TYPES
 **************************************************************************************************/
typedef struct
{
    float value[4];
} float_array_4_t;

/**
 *  @brief Структура кэша данных датчика
 */
typedef struct sensor_cache_slot 
{
    const struct device *dev;
    enum sensor_channel  channel;
    hw_error_channel_1_t error_ch;
} sensor_cache_slot_t;

/***************************************************************************************************
 *                                   PRIVATE FUNCTION PROTOTYPES
 **************************************************************************************************/
static void _sensor_poll_handler(struct k_work *_work);
static int  _app_worker_system_init(void);

/***************************************************************************************************
 *                                           PRIVATE DATA
 **************************************************************************************************/
static struct k_work_delayable sensor_poll_dwork;

ZBUS_CHAN_DEFINE(
    sensor_channel_poll,
    float_array_4_t,
    NULL,
    NULL,
    ZBUS_OBSERVERS_EMPTY,
    ZBUS_MSG_INIT(0)
);

// Массив слотов кэша датчиков
static sensor_cache_slot_t sensors_cache_slots[] = 
{
    {
        .dev      = DEVICE_DT_GET(DT_NODELABEL(tmp112_gnd)), 
        .channel  = SENSOR_CHAN_AMBIENT_TEMP, 
        .error_ch = TMP112_MU_ERROR
    },
    {
        .dev      = DEVICE_DT_GET(DT_NODELABEL(tmp112_vdd)), 
        .channel  = SENSOR_CHAN_AMBIENT_TEMP, 
        .error_ch = TMP112_IU_ERROR
    }, 
    {
        .dev      = DEVICE_DT_GET(DT_NODELABEL(bh1750_gnd)), 
        .channel  = SENSOR_CHAN_LIGHT, 
        .error_ch = BH1750_1_IU_ERROR
    },
    {
        .dev      = DEVICE_DT_GET(DT_NODELABEL(bh1750_vdd)), 
        .channel  = SENSOR_CHAN_LIGHT, 
        .error_ch = BH1750_2_IU_ERROR
    }
};

/***************************************************************************************************
 *                                        PRIVATE FUNCTIONS
 **************************************************************************************************/

/**
 *  @brief      Обработчик отложенного воркера опроса датчиков
 *  @details    Последовательно опрашивает все активные датчики в слотах кэша,
 *              конвертирует сырые показания в float и отправляет их в ZBUS.
 *
 *  @param      _work - Указатель на структуру задачи
 */
static void _sensor_poll_handler(struct k_work *_work)
{
    ARG_UNUSED(_work);
    float_array_4_t msg;

    for (int i = 0; i < ARRAY_SIZE(sensors_cache_slots); i++) 
    {
        sensor_cache_slot_t *slot = &sensors_cache_slots[i];
        struct sensor_value val;

        if (true  
            && sensor_sample_fetch(slot->dev) == 0 
            && sensor_channel_get(slot->dev, slot->channel, &val) == 0
           ) 
        {
            // Перевод struct sensor_value в стандартный float
            msg.value[i] = (float)val.val1 + (float)val.val2 / 1000000.0f;  
            SERVER_CH_CLEAR_BIT_IF_NEEDED(I2C_SENS_ERROR_CH, slot->error_ch);
        } 
        else 
        {
            SERVER_CH_SET_BIT_IF_NEEDED(I2C_SENS_ERROR_CH, slot->error_ch);
            msg.value[i] = NAN;
        }
    }

    zbus_chan_pub(&sensor_channel_poll, &msg, K_MSEC(10));
    
    // Перезапуск воркера ровно через 500 мс
    app_worker_reschedule_submit(&sensor_poll_dwork, K_MSEC(500), COMMON_WORKER);
}

/**
 *  @brief      Инициализация системного планировщика опроса датчиков
 *  @details    Инициализирует структуру отложенной задачи и ставит её
 *              в очередь на первый запуск через 100 мс.
 *
 *  @return     int - Ноль при успешной инициализации
 */
static int _app_worker_system_init(void)
{
    for (int i = 0; i < ARRAY_SIZE(sensors_cache_slots); i++) 
    {
        if (!device_is_ready(sensors_cache_slots[i].dev))
        {
            LOG_ERR("I2C Sensor not ready!");
        }  
    }

    k_work_init_delayable(&sensor_poll_dwork, _sensor_poll_handler);
    
    // Запуск первого опроса через 100 мс
    app_worker_reschedule_submit(&sensor_poll_dwork, K_MSEC(100), COMMON_WORKER);

    LOG_INF("Background sensor monitoring successfully initialized!");

    return 0;
}

SYS_INIT(_app_worker_system_init, I2C_SENS_DEAMON_INIT_LEVEL, I2C_SENS_DEAMON_INIT_PRIOR);

SERVER_CHANNEL_DEFINE(I2C1_TEMP1, &sensor_channel_poll, 0, FLOAT_DATA, 0);
SERVER_CHANNEL_DEFINE(I2C1_TEMP2, &sensor_channel_poll, 1, FLOAT_DATA, 0);
SERVER_CHANNEL_DEFINE(I2C1_LUX1,  &sensor_channel_poll, 2, FLOAT_DATA, 0);
SERVER_CHANNEL_DEFINE(I2C1_LUX2,  &sensor_channel_poll, 3, FLOAT_DATA, 0);

/***************************************************************************************************
 *                                           END OF FILE
 **************************************************************************************************/