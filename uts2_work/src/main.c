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
#include <zephyr/drivers/eeprom.h>
#include <zephyr/drivers/i2c.h>
#include "driver_ads1115.h"

#include "settings.h"
#include "driver_ads1115.h"

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


#define EEPROM_1_NODE DT_NODELABEL(eeprom_1)
#define EEPROM_2_NODE DT_NODELABEL(eeprom_2)

 const struct device *dev1 = DEVICE_DT_GET(EEPROM_1_NODE);
const struct device *dev2 = DEVICE_DT_GET(EEPROM_2_NODE);

#define TMP112_GND_NODE DT_NODELABEL(tmp112_gnd)
#define TMP112_VDD_NODE DT_NODELABEL(tmp112_vdd)

const struct device *dev_gnd = DEVICE_DT_GET(TMP112_GND_NODE);
const struct device *dev_vdd = DEVICE_DT_GET(TMP112_VDD_NODE);

#define BH1750_GND_NODE DT_NODELABEL(bh1750_gnd)
#define BH1750_VDD_NODE DT_NODELABEL(bh1750_vdd)

const struct device *dev_gnd1 = DEVICE_DT_GET(BH1750_GND_NODE);
const struct device *dev_vdd1 = DEVICE_DT_GET(BH1750_VDD_NODE);

void EEPROM_Test()
{
/* Проверяем доступность первой */
    if (!device_is_ready(dev1)) {
        LOG_ERR("EEPROM 1 (0x50) device is not ready!");
        
    }
    LOG_INF("EEPROM 1 (0x50) is ready.");

    /* Проверяем доступность второй */
    if (!device_is_ready(dev2)) {
        LOG_ERR("EEPROM 2 (0x52) device is not ready!");
        
    }
    LOG_INF("EEPROM 2 (0x52) is ready.");

    char msg1[] = "Chip #1: EEPROM 0x50 OK";
    char msg2[] = "Chip #2: EEPROM 0x52 OK";
    
    char buf1[35] = {0};
    char buf2[35] = {0};
    off_t offset = 0;

    /* --- Тестируем первую микросхему (0x50) --- */
    if (eeprom_write(dev1, offset, msg1, sizeof(msg1)) < 0) {
        LOG_ERR("Failed to write to EEPROM 1");
    } else {
        LOG_INF("EEPROM 1 write: '%s'", msg1);
    }

    k_msleep(10); // Время на внутреннюю запись цикла EEPROM

    if (eeprom_read(dev1, offset, buf1, sizeof(msg1)) < 0) {
        LOG_ERR("Failed to read from EEPROM 1");
    } else {
        LOG_INF("EEPROM 1 read:  '%s'", buf1);
    }

    /* --- Тестируем вторую микросхему (0x52) --- */
    if (eeprom_write(dev2, offset, msg2, sizeof(msg2)) < 0) {
        LOG_ERR("Failed to write to EEPROM 2");
    } else {
        LOG_INF("EEPROM 2 write: '%s'", msg2);
    }

    k_msleep(10);

    if (eeprom_read(dev2, offset, buf2, sizeof(msg2)) < 0) {
        LOG_ERR("Failed to read from EEPROM 2");
    } else {
        LOG_INF("EEPROM 2 read:  '%s'", buf2);
    }

    
}
#define I2C2_NODE DT_NODELABEL(i2c1)





int main(void)
{


 

    

    
	LOG_INF("SYSTETM START 2");	
/*	int ret;
	if (!device_is_ready(dev_gnd)) {
        LOG_ERR("TMP112 (0x48, ADD0=GND) is not ready!");
        return 0;
    }
    LOG_INF("TMP112 (0x48) is ready.");

    if (!device_is_ready(dev_vdd)) {
        LOG_ERR("TMP112 (0x49, ADD0=VDD) is not ready!");
        return 0;
    }
    LOG_INF("TMP112 (0x49) is ready.");

 


    EEPROM_Test();
     if (!device_is_ready(dev_gnd1)) {
        LOG_ERR("BH1750 (0x23, ADDR=GND) is not ready!");
       // return 0;
    }
    LOG_INF("BH1750 (0x23) is ready.");

    if (!device_is_ready(dev_vdd1)) {
        LOG_ERR("BH1750 (ADDR=VDD) is not ready!");
        //return 0;
    }
    LOG_INF("BH1750 (VDD) is ready.");

	FRAM_Test();

	init_all_sensors();
    //settings_fram_init();
   int current_led = 0;*/
    while (1) 
	{
       
      struct ads1115_snapshot snapshot;
if (zbus_chan_read(&ads_channel, &snapshot, K_NO_WAIT) == 0) {
            LOG_INF("=== ADS1115 SNAPSHOT ===");
            LOG_INF("AIN0: %.2f mV (ready: %d)", snapshot.voltages_mv[0], snapshot.channel_ready[0]);
            LOG_INF("AIN1: %.2f mV (ready: %d)", snapshot.voltages_mv[1], snapshot.channel_ready[1]);
            LOG_INF("AIN2: %.2f mV (ready: %d)", snapshot.voltages_mv[2], snapshot.channel_ready[2]);
            LOG_INF("AIN3: %.2f mV (ready: %d)", snapshot.voltages_mv[3], snapshot.channel_ready[3]);
        } else {
            LOG_WRN("No data in Zbus channel yet.");
        }

      struct sensor_value lux_gnd, lux_vdd;

 
/* Читаем датчик 1 (ADDR = GND) */
       /* if (sensor_sample_fetch(dev_gnd1) == 0) {
            sensor_channel_get(dev_gnd1, SENSOR_CHAN_LIGHT, &lux_gnd);
            LOG_INF("Light (ADDR = GND, 0x23): %d.%02d lx", 
                     lux_gnd.val1, lux_gnd.val2 / 10000);
        } else {
            LOG_ERR("Failed to fetch sample from BH1750 (0x23)");
        }

        /* Читаем датчик 2 (ADDR = VDD) */
       /* if (sensor_sample_fetch(dev_vdd1) == 0) {
            sensor_channel_get(dev_vdd1, SENSOR_CHAN_LIGHT, &lux_vdd);
            LOG_INF("Light (ADDR = VDD): %d.%02d lx", 
                     lux_vdd.val1, lux_vdd.val2 / 10000);
        } else {
            LOG_ERR("Failed to fetch sample from BH1750 (VDD)");
        }*/


        led_manager_set_states_sync(false, false, true, K_MSEC(100));
        k_msleep(SLEEP_TIME_MS);
        led_manager_set_states_sync(false, true, false, K_MSEC(100));
       k_msleep(SLEEP_TIME_MS);
		//poll_all_sensors();


        /*if (sensor_sample_fetch(dev_gnd) == 0) {
            sensor_channel_get(dev_gnd, SENSOR_CHAN_AMBIENT_TEMP, &temp_gnd);
            LOG_INF("Temp (ADD0 = GND, 0x48): %d.%02d °C", 
                     temp_gnd.val1, temp_gnd.val2 / 10000); // val2 хранится в микро-единицах
        } else {
            LOG_ERR("Failed to fetch sample from TMP112 (0x48)");
        }

        /* Читаем датчик 2 (ADD0 = VDD) */
/*        if (sensor_sample_fetch(dev_vdd) == 0) {
            sensor_channel_get(dev_vdd, SENSOR_CHAN_AMBIENT_TEMP, &temp_vdd);
            LOG_INF("Temp (ADD0 = VDD, 0x49): %d.%02d °C", 
                     temp_vdd.val1, temp_vdd.val2 / 10000);
        } else {
            LOG_ERR("Failed to fetch sample from TMP112 (0x49)");
        }
            */
   }
	
	return 0;
}
