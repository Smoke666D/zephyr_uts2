#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include "system_define.h"
#include "system_bus_model.h"
#include "app_worker.h"

LOG_MODULE_REGISTER(lis3dh_app, LOG_LEVEL_INF);

typedef struct mem_sens_data_t
{
    float value[3];

} mem_sens_data_t;

ZBUS_CHAN_DEFINE(acc_data_chan,
                 mem_sens_data_t,
                 NULL,
                 NULL,
                 ZBUS_OBSERVERS_EMPTY,
                 {0}
);


static void _trigger_handler(const struct device *_dev, const struct sensor_trigger *_trig)
{
    struct sensor_value accel[3];
    mem_sens_data_t _msg;
    int rc;

    // 1. По прерыванию вычитываем свежие данные из датчика по SPI
    rc = sensor_sample_fetch(_dev);
    if (rc < 0) 
    {
        printk("[LIS3DH] Trigger fetch failed: %d\n", rc);
        return;
    }

    // 2. Получаем каналы ускорения по осям X, Y, Z
    rc = sensor_channel_get(_dev, SENSOR_CHAN_ACCEL_XYZ, accel);
    if (rc < 0) 
    {
        printk("[LIS3DH] Trigger channel get failed: %d\n", rc);
        return;
    }

    _msg.value[0] = sensor_value_to_float(&accel[0]);
    _msg.value[1] = sensor_value_to_float(&accel[1]);
    _msg.value[2] = sensor_value_to_float(&accel[2]); 
    zbus_chan_pub(&acc_data_chan, &_msg, K_NO_WAIT);   
}

/**
 *  @brief      Инициализация акселерометра LIS3DH
 *  @details    Проверяет готовность датчика и регистрирует обработчик прерывания
 *              для периодического чтения данных ускорения.
 *
 *  @return     int - Результат проверки готовности датчика (1 при успехе, 0 при сбое)
 */
static int _accel_init(void)
{
    int ret;
    const struct device *const accel_dev = DEVICE_DT_GET(DT_NODELABEL(lis3dh));
    
        /* 1. Проверяем готовность датчика */
    if (!device_is_ready(accel_dev)) {
        LOG_ERR("Sensor LIS3DH device NOT ready!");
        return -ENODEV;
    }          

     struct sensor_value odr = {
        .val1 = 25, /* 25 Гц (прерывание каждые 40 мс) */
        .val2 = 0,
    };
    ret = sensor_attr_set(accel_dev, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, &odr);
    if (ret < 0) {
        LOG_INF("[LIS3DH] Failed to set ODR frequency: %d\n", ret);
    }
     
 /* 3. Привязываем прерывание Data-Ready */
    struct sensor_trigger trig = {
        .type = SENSOR_TRIG_DATA_READY,
        .chan = SENSOR_CHAN_ACCEL_XYZ,
    };

    ret = sensor_trigger_set(accel_dev, &trig, _trigger_handler);
    if (ret < 0) {
        LOG_INF("[LIS3DH] Trigger set failed: %d\n", ret);
        return ret;
    }

    LOG_INF("[LIS3DH] Interrupt initialized at 25 Hz\n");        
       
    return ret;    
}

SYS_INIT(_accel_init, ACC_DEAMON_INIT_LEVEL, ACC_DEAMON_INIT_PRIOR);

SERVER_IN_CHANNEL_DEFINE(ACC_X,  &acc_data_chan, 0, FLOAT_DATA);
SERVER_IN_CHANNEL_DEFINE(ACC_Y,  &acc_data_chan, 1, FLOAT_DATA);
SERVER_IN_CHANNEL_DEFINE(ACC_Z,  &acc_data_chan, 2, FLOAT_DATA);