#pragma once


#include "scpi/scpi.h"

scpi_result_t cmd_led_reset(scpi_t *context);
scpi_result_t cmd_disp_led_state(scpi_t *context);
scpi_result_t cmd_disp_led_state_q(scpi_t *context);