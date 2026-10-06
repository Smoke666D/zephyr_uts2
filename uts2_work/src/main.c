/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/eeprom.h>
#include <zephyr/drivers/i2c.h>
#include "ad5243.h"
#include "settings.h"
#include "out_and_power_control.h"
#include "system_bus_model.h"
#include "telemetry.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);
//#include "usb_thread.h"

//#include "ina228_stream_thread.h"

/* 1000 msec = 1 sec */
#define SLEEP_TIME_MS   500



#define SPI4_NODE DT_NODELABEL(spi4)


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
#define I2C1_NODE DT_NODELABEL(i2c1)
/*

#define AD5243_DEFAULT_ADDR 0x2F 

typedef enum {
    AD5243_CHANNEL_1 = 0,
    AD5243_CHANNEL_2 = 1
} ad5243_channel_t;


int ad5243_set_wiper(ad5243_channel_t channel, uint8_t value)
{
    const struct device *i2c_dev = DEVICE_DT_GET(I2C1_NODE);
    if (!device_is_ready(i2c_dev)) {
        LOG_ERR("I2C1 bus not ready for AD5243!");
        return -ENODEV;
    }

    struct i2c_dt_spec spec = {
        .bus = i2c_dev,
        .addr = AD5243_DEFAULT_ADDR
    };

    uint8_t tx_data[2];

    if (channel == AD5243_CHANNEL_1) {
        tx_data[0] = 0x00; // Выбор канала 1 (W1)
    } else {
        tx_data[0] = 0x80; // Выбор канала 2 (W2)
    }

    tx_data[1] = value; // Значение положения движка (0..255)

    int ret = i2c_write_dt(&spec, tx_data, sizeof(tx_data));
    if (ret < 0) {
        LOG_ERR("Failed to set AD5243 wiper: %d", ret);
        return ret;
    }

    LOG_DBG("AD5243 Ch %d set to wiper pos: %d", channel + 1, value);
    return 0;
}

*/


void send_binary_telemetry(const struct device *uart_dev) {
    struct telemetry_packet pkt;
    pkt.magic = 0xBE;

    // Заполняем данными из твоего системного буфера
    SYSTEM_BUS_GET(RF_POWER, &pkt.rf_power);
    SYSTEM_BUS_GET(NTC, &pkt.ntc);
    SYSTEM_BUS_GET(EXT_VSENSE, &pkt.ext_vsense);
    SYSTEM_BUS_GET(BRD_DETECT, &pkt.brd_detect);

    SYSTEM_BUS_GET(I2C1_TEMP1, &pkt.temp_gnd);
    SYSTEM_BUS_GET(I2C1_TEMP2, &pkt.temp_vdd);
    SYSTEM_BUS_GET(I2C1_LUX1, &pkt.light_gnd);
    SYSTEM_BUS_GET(I2C1_LUX2, &pkt.light_vdd);

    // AO 1-6, 7-12, 13-18
    SYSTEM_BUS_GET(AIN_AO1, &pkt.ao[0]);
    SYSTEM_BUS_GET(AIN_AO2, &pkt.ao[1]);
    SYSTEM_BUS_GET(AIN_AO3, &pkt.ao[2]);
    SYSTEM_BUS_GET(AIN_AO4, &pkt.ao[3]);
    SYSTEM_BUS_GET(AIN_AO5, &pkt.ao[4]);
    SYSTEM_BUS_GET(AIN_AO6, &pkt.ao[5]);
    
    SYSTEM_BUS_GET(AIN_AO7, &pkt.ao[6]);
    SYSTEM_BUS_GET(AIN_AO8, &pkt.ao[7]);
    SYSTEM_BUS_GET(AIN_AO9, &pkt.ao[8]);
    SYSTEM_BUS_GET(AIN_AO10, &pkt.ao[9]);
    SYSTEM_BUS_GET(AIN_AO11, &pkt.ao[10]);
    SYSTEM_BUS_GET(AIN_AO12, &pkt.ao[11]);
    
    SYSTEM_BUS_GET(AIN_AO13, &pkt.ao[12]);
    SYSTEM_BUS_GET(AIN_AO14, &pkt.ao[13]);
    SYSTEM_BUS_GET(AIN_AO15, &pkt.ao[14]);
    SYSTEM_BUS_GET(AIN_AO16, &pkt.ao[15]);
    SYSTEM_BUS_GET(AIN_AO17, &pkt.ao[16]);
    SYSTEM_BUS_GET(AIN_AO18, &pkt.ao[17]);

    // AIN_VSense 1-6
    SYSTEM_BUS_GET(AIN_AVsense1, &pkt.vsense[0]);
    SYSTEM_BUS_GET(AIN_AVsense2, &pkt.vsense[1]);
    SYSTEM_BUS_GET(AIN_AVsense3, &pkt.vsense[2]);
    SYSTEM_BUS_GET(AIN_AVsense4, &pkt.vsense[3]);
    SYSTEM_BUS_GET(AIN_AVsense5, &pkt.vsense[4]);
    SYSTEM_BUS_GET(AIN_AVsense6, &pkt.vsense[5]);

    // Step 7 & 8
    SYSTEM_BUS_GET(AIN_DA11_test1, &pkt.step7[0]);
    SYSTEM_BUS_GET(AIN_DA20_test1, &pkt.step7[1]);
    SYSTEM_BUS_GET(AIN_DA33_test1, &pkt.step7[2]);
    SYSTEM_BUS_GET(AIN_DA44_test1, &pkt.step7[3]);
    SYSTEM_BUS_GET(AIN_DA41_test1, &pkt.step7[4]);

    SYSTEM_BUS_GET(AIN_DA11_test2, &pkt.step8[0]);
    SYSTEM_BUS_GET(AIN_DA20_test2, &pkt.step8[1]);
    SYSTEM_BUS_GET(AIN_DA33_test2, &pkt.step8[2]);
    SYSTEM_BUS_GET(AIN_DA44_test2, &pkt.step8[3]);
    SYSTEM_BUS_GET(AIN_DA41_test2, &pkt.step8[4]);

    // Env (8 шт)
    SYSTEM_BUS_GET(ENV_P3V3,  &pkt.env[0]);
    SYSTEM_BUS_GET(ENV_P5V0,  &pkt.env[1]);
    SYSTEM_BUS_GET(ENV_VIN,   &pkt.env[2]);
    SYSTEM_BUS_GET(ENV_VDOUT1, &pkt.env[3]);
    SYSTEM_BUS_GET(ENV_P40V,  &pkt.env[4]);
    SYSTEM_BUS_GET(ENV_USB,   &pkt.env[5]);
    SYSTEM_BUS_GET(ENV_VDOUT2, &pkt.env[6]);
    SYSTEM_BUS_GET(ENV_VDOUT3, &pkt.env[7]);

    // Отправляем всю структуру байт за байтом в UART
    uint8_t *ptr = (uint8_t *)&pkt;
    for (size_t i = 0; i < sizeof(pkt); i++) {
        uart_poll_out(uart_dev, ptr[i]);
    }
}


void _send_log()
{
        float ain[16];
        
    SYSTEM_BUS_GET(SENS_BRD_LOW_CURRENT, &ain[0]);
    SYSTEM_BUS_GET(SENS_BRD_LOW_VOLTAGE,  &ain[1]);
    SYSTEM_BUS_GET(SENS_BRD_HIGH_CURRENT, &ain[2]);
    SYSTEM_BUS_GET(SENS_BRD_HIGH_VOLTAGE, &ain[3]);
    SYSTEM_BUS_GET(SENS_VDUT2_CURRENT,    &ain[4]);
    SYSTEM_BUS_GET(SENS_VDUT2_VOLTAGE,   &ain[5]);
    SYSTEM_BUS_GET(SENS_VDUT3_CURRENT,   &ain[6]);
    SYSTEM_BUS_GET(SENS_VDUT3_VOLTAGE,   &ain[7]);
    SYSTEM_BUS_GET(SENS_VIN_CURRENT,     &ain[8]);
    SYSTEM_BUS_GET(SENS_VIN_VOLTAGE,     &ain[9]);
    SYSTEM_BUS_GET(SENS_DCDC_3_3_CURRENT, &ain[10]);
    SYSTEM_BUS_GET(SENS_DCDC_3_3_VOLTAGE, &ain[11]);
    SYSTEM_BUS_GET(SENS_VDOUT_PWR_CURRENT, &ain[12]);
    SYSTEM_BUS_GET(SENS_VDOUT_PWR_VOLTAGE, &ain[13]);
    SYSTEM_BUS_GET(SENS_VD5_CURRENT,       &ain[14]);
    SYSTEM_BUS_GET(SENS_VD5_VOLTAGE,       &ain[15]);
        
        
        

    printk("ina1  %.2f %.6f \n", (double)ain[0],(double)ain[1]);
    printk("ina2  %.2f %.6f \n", (double)ain[2],(double)ain[3]);
    printk("ina3  %.2f %.6f \n", (double)ain[4],(double)ain[5]);
    printk("ina4  %.2f %.6f \n", (double)ain[6],(double)ain[7]);
    printk("ina5  %.2f %.6f \n", (double)ain[8],(double)ain[9]);
    printk("ina6  %.2f %.6f \n", (double)ain[10],(double)ain[11]);
    printk("ina7  %.2f %.6f \n", (double)ain[12],(double)ain[13]);
    printk("ina8  %.2f %.6f \n", (double)ain[14],(double)ain[15]);

}


static const struct device *const telemetry_uart = DEVICE_DT_GET(DT_NODELABEL(usart1));

int main(void)
{
	LOG_INF("SYSTETM START 3");	
    ad5243_init(DEVICE_DT_GET(I2C1_NODE), 0, 0);
/*	int ret;
    EEPROM_Test();
	FRAM_Test();
	init_all_sensors();
    //settings_fram_init();

    */
   
   
    while (1) 
	{
         _send_log();
        

       // send_binary_telemetry(telemetry_uart);
        // poll_all_sensors();
       
        k_msleep(SLEEP_TIME_MS);
             
        k_msleep(SLEEP_TIME_MS);

       /* if (!ad5243_set_wiper(AD5243_CHANNEL_2, i))
        {
            LOG_INF("AD5243 Ch1 -> %d\n",i);
            i=i+10;
            if (i >=255)  i = 0;
        }
        else 
        {
            LOG_INF("AD5243 Ch1 error\n");
        }*/

 
   }
	
	return 0;
}
