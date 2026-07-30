/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include "usb_thread.h"
#include "coorutines.hpp"
#include "ina228_stream_thread.h"
#include "coorutines.hpp"
#include "adc_mux_processor.hpp"

K_MSGQ_DEFINE(drv_sensor_msgq, sizeof(SensorRawData), 10, 4);


SensorDispatcher g_sensor_disp("sensor_disp", os::priority::normal);

/* 1000 msec = 1 sec */
#define SLEEP_TIME_MS   1000

int main(void)
{

	usb_thread_start();
	
	start_ina228_poller_thread();
    g_sensor_disp.start();
	sensor_bridge_daemon(g_sensor_disp);
	while (1) 
	{	
		k_msleep(SLEEP_TIME_MS);
	}
	return 0;
}
