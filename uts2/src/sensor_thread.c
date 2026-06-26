#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>

#include "sensor_thread.h"

/* Адрес датчика на шине I2C */
#define SENSOR_I2C_ADDR 0x48

/* Устройство I2C из Devicetree */
#define I2C_DEV DEVICE_DT_GET(DT_NODELABEL(i2c1))

/* Стек и структура потока */
K_THREAD_STACK_DEFINE(sensor_stack, SENSOR_STACK_SIZE);
static struct k_thread sensor_thread_data;


static double_t _sensor_temp = 0;
/* -------------------------------------------------------------------------- */

/**
 * @brief Прочитать 2 байта из датчика (температура)
 *
 * @param i2c_dev Устройство I2C
 * @param temp Указатель на переменную для температуры (в сотых долях градуса)
 * @return 0 при успехе
 */
static int read_temperature(const struct device *i2c_dev, int16_t *temp)
{
    uint8_t buf[2];
    int ret;

    ret = i2c_burst_read(i2c_dev, SENSOR_I2C_ADDR, 0, buf, 2);
    if (ret < 0) {
        return ret;
    }

    /* Преобразование зависит от датчика! Пример для TMP116:
     * Старший байт + младший байт, результат в 0.0078125°C на бит */
    *temp = (int16_t)((buf[0] << 8) | buf[1]);

    return 0;
}




/* -------------------------------------------------------------------------- */

static void sensor_thread_func(void *arg1, void *arg2, void *arg3)
{
    const struct device *i2c_dev = I2C_DEV;
    uint32_t count = 0;

    if (!device_is_ready(i2c_dev)) {
        printf("[SENSOR] ERROR: I2C device not ready!\n");
        return;
    }

    printf("[SENSOR] Thread started (period = %d ms)\n", SENSOR_PERIOD_MS);

    while (1) {
        int16_t raw_temp;
        int ret;

        ret = read_temperature(i2c_dev, &raw_temp);
        if (ret < 0) {
            printf("[SENSOR] Read error: %d\n", ret);
        } else {
            /* Перевод в градусы Цельсия (зависит от датчика!) */
            float temp_c = raw_temp * 0.0078125f;
            printf("[SENSOR] #%u: %.2f °C (raw: 0x%04X)\n",
                   count++, (double)temp_c, raw_temp);
        }

        k_msleep(SENSOR_PERIOD_MS);
    }
}

/* -------------------------------------------------------------------------- */

int sensor_thread_start(void)
{
    k_tid_t tid = k_thread_create(
        &sensor_thread_data,
        sensor_stack,
        SENSOR_STACK_SIZE,
        sensor_thread_func,
        NULL, NULL, NULL,
        SENSOR_PRIORITY,
        0,
        K_NO_WAIT
    );

    if (tid == NULL) {
        printf("[SENSOR] ERROR: Failed to create thread!\n");
        return -1;
    }

    return 0;
}

