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
#include <zephyr/drivers/spi.h>
#include "settings.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);
//#include "usb_thread.h"

//#include "ina228_stream_thread.h"

/* 1000 msec = 1 sec */
#define SLEEP_TIME_MS   500

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



int main(void)
{

	LOG_INF("SYSTETM START 1");
	char *msg = "Hello via raw UART PA9!\r\n";
	int ret;
	

	if (gpio_is_ready_dt(&led)) {
		gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	}

	FRAM_Test();

    //settings_fram_init();

    while (1) 
	{
        if (gpio_is_ready_dt(&led)) 
		{
            gpio_pin_toggle_dt(&led);
        }
        k_msleep(SLEEP_TIME_MS);
		
    }
	
	return 0;
}
