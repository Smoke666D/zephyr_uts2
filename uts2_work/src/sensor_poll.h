#pragma once

#include <zephyr/kernel.h>
#include <stdbool.h>

#define SENSOR_IDX_TMP112_GND  0
#define SENSOR_IDX_TMP112_VDD  1
#define SENSOR_IDX_BH1750_GND  2
#define SENSOR_IDX_BH1750_VDD  3

bool app_worker_get_sensor_float(int sensor_index, float *out_value);