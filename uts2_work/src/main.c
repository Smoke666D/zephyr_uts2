/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/spi.h>
#include "led.h"

#include "settings.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);
//#include "usb_thread.h"

//#include "ina228_stream_thread.h"

/* 1000 msec = 1 sec */
#define SLEEP_TIME_MS   1000

#define LED0_NODE DT_ALIAS(led1)

#define SPI4_NODE DT_NODELABEL(spi4)

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
const struct device *uart_dev = DEVICE_DT_GET(DT_NODELABEL(usart1));


// Объявляем буферы глобально (в статической памяти), чтобы они не лежали на стеке
static uint8_t wr_cmd[] = { 0x02, 0x00, 0x00, 0x55 }; // WEN/Write массив
static uint8_t rd_cmd[] = { 0x03, 0x00, 0x00, 0x00 }; // Read массив
static uint8_t rd_resp[4] = { 0 };
static uint8_t wren_cmd = 0x06;

static void FRAM_Test(void)
{
    LOG_INF("Starting safe SPI test for FRAM (Static buffers)...");

    const struct device *spi_dev = DEVICE_DT_GET(SPI4_NODE);
    if (!device_is_ready(spi_dev)) {
        LOG_ERR("SPI4 bus not ready!");
        return;
    }

    const struct device *gpioe_dev = DEVICE_DT_GET(DT_NODELABEL(gpioe));
    if (!device_is_ready(gpioe_dev)) {
        LOG_ERR("GPIOE device not ready!");
        return;
    }

    // Настраиваем PE4 как выход для CS (изначально высокий уровень - неактивен)
    gpio_pin_configure(gpioe_dev, 4, GPIO_OUTPUT_ACTIVE);
    gpio_pin_set(gpioe_dev, 4, 1);

    struct spi_config spi_cfg = {
        .frequency = 1000000, // 1 МГц
        .operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
        .slave = 0,
    };

    // 1. Команда WREN (Write Enable = 0x06)
    {
        struct spi_buf tx_buf = { .buf = &wren_cmd, .len = 1 };
        struct spi_buf_set tx = { .buffers = &tx_buf, .count = 1 };
        
        gpio_pin_set(gpioe_dev, 4, 0);
        spi_transceive(spi_dev, &spi_cfg, &tx, NULL);
        gpio_pin_set(gpioe_dev, 4, 1);
    }

    k_busy_wait(10);

    // 2. Запись байта 0x55 по адресу 0x0000
    {
        struct spi_buf tx_buf = { .buf = wr_cmd, .len = sizeof(wr_cmd) };
        struct spi_buf_set tx = { .buffers = &tx_buf, .count = 1 };

        gpio_pin_set(gpioe_dev, 4, 0);
        spi_transceive(spi_dev, &spi_cfg, &tx, NULL);
        gpio_pin_set(gpioe_dev, 4, 1);
    }

    k_busy_wait(100);

    // 3. Чтение байта по адресу 0x0000
    {
        struct spi_buf tx_buf = { .buf = rd_cmd, .len = sizeof(rd_cmd) };
        struct spi_buf_set tx = { .buffers = &tx_buf, .count = 1 };

        struct spi_buf rx_buf = { .buf = rd_resp, .len = sizeof(rd_resp) };
        struct spi_buf_set rx = { .buffers = &rx_buf, .count = 1 };

        gpio_pin_set(gpioe_dev, 4, 0);
        spi_transceive(spi_dev, &spi_cfg, &tx, &rx);
        gpio_pin_set(gpioe_dev, 4, 1);

        LOG_INF("FRAM Read result -> Opcode resp: 0x%02X, AddrHi: 0x%02X, AddrLo: 0x%02X, DATA: 0x%02X", 
                rd_resp[0], rd_resp[1], rd_resp[2], rd_resp[3]);
    }
}

/* Структура для хранения информации о датчике */
struct ina_sensor_desc {
    const char *name;
    const struct device *dev;
};



/* Массив всех ваших 8 датчиков по нодлейблам из DTS */
static const struct ina_sensor_desc ina_sensors[] = {
    { "BRD_LOW",   DEVICE_DT_GET(DT_NODELABEL(cur_sens_brd_low)) },
    { "BRD_HIGH",  DEVICE_DT_GET(DT_NODELABEL(cur_sens_brd_high)) },
    { "VDUT2",     DEVICE_DT_GET(DT_NODELABEL(cur_sens_vdut2)) },
    { "VDUT3",     DEVICE_DT_GET(DT_NODELABEL(cur_sens_vdut3)) },
    { "VIN",       DEVICE_DT_GET(DT_NODELABEL(cur_sens_vin)) },
    { "3_3_DCDC",  DEVICE_DT_GET(DT_NODELABEL(cur_sens_3_3_dcdc)) },
    { "VDOUT_PWR", DEVICE_DT_GET(DT_NODELABEL(cur_sens_vdout_pwr)) },
    { "5VD",       DEVICE_DT_GET(DT_NODELABEL(cur_sens_5vd)) },
};

#define SENSORS_COUNT ARRAY_SIZE(ina_sensors)

static int init_all_sensors(void)
{
    int ready_count = 0;

    printk("\n=== Проверка инициализации датчиков INA228 ===\n");
    for (size_t i = 0; i < SENSORS_COUNT; i++) {
        if (!device_is_ready(ina_sensors[i].dev)) {
            printk("[ERR] Датчик '%s' НЕ готов (нет связи по I2C)!\n", ina_sensors[i].name);
        } else {
            printk("[ OK] Датчик '%s' готов.\n", ina_sensors[i].name);
            ready_count++;
        }
    }
    printk("Готово: %d из %d датчиков\n\n", ready_count, SENSORS_COUNT);

    return ready_count;
}

/* Функция опроса и вывода данных со всех датчиков */
void poll_all_sensors(void)
{
    struct sensor_value v_bus;
    struct sensor_value current;
    struct sensor_value power;
    int ret;

    printk("----------------------------------------------------------------------\n");
    printk("| %-12s | %-12s | %-14s | %-14s |\n", "Sensor", "Vbus (V)", "Current (mA)", "Power (mW)");
    printk("----------------------------------------------------------------------\n");

    for (size_t i = 0; i < SENSORS_COUNT; i++) {
        const struct device *dev = ina_sensors[i].dev;

        if (!device_is_ready(dev)) {
            printk("| %-12s |   [ДАТЧИК НЕ ДОСТУПЕН]                            |\n", 
                   ina_sensors[i].name);
            continue;
        }

        /* 1. Запрашиваем новую выборку данных у микросхемы */
        ret = sensor_sample_fetch(dev);
        if (ret != 0) {
            printk("| %-12s |   [ОШИБКА I2C: %d]                                 |\n", 
                   ina_sensors[i].name, ret);
            continue;
        }

        /* 2. Извлекаем значения каналов */
        sensor_channel_get(dev, SENSOR_CHAN_VOLTAGE, &v_bus);
        sensor_channel_get(dev, SENSOR_CHAN_CURRENT, &current);
        sensor_channel_get(dev, SENSOR_CHAN_POWER, &power);

        /* Переводим в double (требуется CONFIG_CBPRINTF_FP_SUPPORT=y) */
        double v = sensor_value_to_double(&v_bus);
        double i_ma = sensor_value_to_double(&current) * 1000.0; /* переводим А в мА */
        double p_mw = sensor_value_to_double(&power) * 1000.0;   /* переводим Вт в мВт */

        printk("| %-12s | %10.3f V | %11.3f mA | %11.3f mW |\n",
               ina_sensors[i].name, v, i_ma, p_mw);
    }
    printk("----------------------------------------------------------------------\n\n");
}

int main(void)
{

	LOG_INF("SYSTETM START 2");	
	int ret;
	



	FRAM_Test();

	init_all_sensors();
    //settings_fram_init();
   
    while (1) 
	{
        led_manager_set_states_sync(true, false, false, K_MSEC(100));
        k_msleep(SLEEP_TIME_MS);
        led_manager_set_states_sync(false, true, false, K_MSEC(100));
        k_msleep(SLEEP_TIME_MS);
		poll_all_sensors();
    }
	
	return 0;
}
