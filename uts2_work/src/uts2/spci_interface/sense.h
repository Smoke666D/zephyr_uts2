#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "output.h"
#include "scpi/ieee488.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include "system_bus_model.h"


scpi_result_t cmd_sense_uart_data_q(scpi_t *context);
scpi_result_t cmd_sense_uart_count_q(scpi_t *context);
scpi_result_t cmd_sense_uart_clear(scpi_t *context);
scpi_result_t cmd_sense_uart_baud(scpi_t *context);
scpi_result_t cmd_sense_uart_baud_q(scpi_t *context);
void sense_uart_reset(void);