
#include <sample_usbd.h>

#include <zephyr/kernel.h>
#include <stdio.h>
#include <string.h>
#include "usb_thread.h"
#include <zephyr/drivers/gpio.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>

#include <zephyr/usb/usbd.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(cdc_acm_echo, LOG_LEVEL_INF);

const struct device *const uart_dev = DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED1_NODE, gpios);

K_THREAD_STACK_DEFINE(stack, STACK_SIZE);
static struct k_thread thread_data;

static void func(void *arg1, void *arg2, void *arg3)
{
    


	bool led_state = true;
    int count = 0;
    while (1) {
         gpio_pin_toggle_dt(&led);
		

		led_state = !led_state;
      //  printf("[WORKER] %d\n", count++);
        k_msleep(700);
    }
}

int usb_thread_start(void)
{
    int ret;
	bool led_state = true;

    k_thread_create(&thread_data, stack, STACK_SIZE,
                    func, NULL, NULL, NULL, PRIORITY, 0, K_NO_WAIT);

    if (!gpio_is_ready_dt(&led)) {
		return 0;
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		return 0;
	}

   
    return 0;
}