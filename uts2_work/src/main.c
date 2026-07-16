/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include "usb_thread.h"
#include "dio_thread.h"
#include "adc_thread.h"

/* 1000 msec = 1 sec */
#define SLEEP_TIME_MS   1000

int main(void)
{
	//dio_thread_start();
	usb_thread_start();
    StartDmaGPIO();
	while (1) 
	{	
		k_msleep(SLEEP_TIME_MS);
	}
	return 0;
}
