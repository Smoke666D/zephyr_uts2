#pragma once


#include "scpi/scpi.h"

scpi_result_t cmd_led_reset(scpi_t *context);

scpi_result_t cmd_led_red(scpi_t *context);
scpi_result_t cmd_led_red_q(scpi_t *context);
scpi_result_t cmd_led_green(scpi_t *context);
scpi_result_t cmd_led_green_q(scpi_t *context);
scpi_result_t cmd_led_blue(scpi_t *context);
scpi_result_t cmd_led_blue_q(scpi_t *context);