/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

/* 1000 msec = 1 sec */
#define SLEEP_TIME_MS   1000

/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)

#define LED1_NODE DT_ALIAS(led1)

/*
 * A build error on this line means your board is unsupported.
 * See the sample documentation for information on how to fix this.
 */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(LED1_NODE, gpios);

/* Размер стека для потоков */
#define STACK_SIZE 1024

/* Периоды мигания */
#define LED0_PERIOD_MS 300   /* Быстрое мигание зеленым */
#define LED1_PERIOD_MS 700   /* Медленное мигание желтым */

/* Приоритеты потоков (чем меньше число, тем выше приоритет) */
#define LED0_PRIORITY 5
#define LED1_PRIORITY 5

void led0_thread(void *arg1, void *arg2, void *arg3)
{
    const struct gpio_dt_spec *led = (const struct gpio_dt_spec *)arg1;
    uint32_t count = 0;

    /* Настройка пина */
    gpio_pin_configure_dt(led, GPIO_OUTPUT_ACTIVE);
    printf("[LED0 Thread] Started (period = %d ms)\n", LED0_PERIOD_MS);

    while (1) {
        gpio_pin_toggle_dt(led);
        count++;
        printf("[LED0] Blink #%u\n", count);
        k_msleep(LED0_PERIOD_MS);
    }
}

void led1_thread(void *arg1, void *arg2, void *arg3)
{
    const struct gpio_dt_spec *led = (const struct gpio_dt_spec *)arg1;
    uint32_t count = 0;

    /* Настройка пина */
    gpio_pin_configure_dt(led, GPIO_OUTPUT_ACTIVE);
    printf("[LED1 Thread] Started (period = %d ms)\n", LED1_PERIOD_MS);

    while (1) {
        gpio_pin_toggle_dt(led);
        count++;
        printf("[LED1] Blink #%u\n", count);
        k_msleep(LED1_PERIOD_MS);
    }
}

K_THREAD_STACK_DEFINE(led0_stack, STACK_SIZE);
K_THREAD_STACK_DEFINE(led1_stack, STACK_SIZE);

static struct k_thread led0_thread_data;
static struct k_thread led1_thread_data;


int main(void)
{
	int ret;
	

	if (!gpio_is_ready_dt(&led)) {
		return 0;
	}

	if (!gpio_is_ready_dt(&led1)) {
		return 0;
	}
	/* Создаем поток для зеленого светодиода */
    k_thread_create(
        &led0_thread_data,    /* Структура потока */
        led0_stack,           /* Стек */
        STACK_SIZE,           /* Размер стека */
        led0_thread,          /* Функция потока */
        (void *)&led,        /* Аргумент 1 — указатель на led0 */
        NULL,                 /* Аргумент 2 */
        NULL,                 /* Аргумент 3 */
        LED0_PRIORITY,        /* Приоритет */
        0,                    /* Флаги */
        K_NO_WAIT             /* Задержка перед запуском */
    );

    /* Создаем поток для желтого светодиода */
    k_thread_create(
        &led1_thread_data,
        led1_stack,
        STACK_SIZE,
        led1_thread,
        (void *)&led1,
        NULL,
        NULL,
        LED1_PRIORITY,
        0,
        K_NO_WAIT
    );
	
 	while (1) {
        k_msleep(5000);
        printf("[Main] Still alive... (LED0 fast, LED1 slow)\n");
    }

	return 0;
}
