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

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);
//#include "usb_thread.h"

//#include "ina228_stream_thread.h"

/* 1000 msec = 1 sec */
#define SLEEP_TIME_MS   500

#define LED0_NODE DT_ALIAS(led1)

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
const struct device *uart_dev = DEVICE_DT_GET(DT_NODELABEL(usart1));

int main(void)
{
if (!device_is_ready(uart_dev)) {
        LOG_ERR("USART1 not ready!");
        
    }
	LOG_INF("SYSTETM START 1");
	char *msg = "Hello via raw UART PA9!\r\n";
int ret;
	bool led_state = true;

	if (!gpio_is_ready_dt(&led)) {
		return 0;
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		return 0;
	}

	while (1) {
		ret = gpio_pin_toggle_dt(&led);
		if (ret < 0) {
			return 0;
		}

		led_state = !led_state;
		//LOG_INF("LED state: %s\n", led_state ? "ON" : "OFF");
		k_msleep(SLEEP_TIME_MS);
		for (int i = 0; msg[i] != '\0'; i++) {
            uart_poll_out(uart_dev, msg[i]);
        }
	}
	return 0;
}
