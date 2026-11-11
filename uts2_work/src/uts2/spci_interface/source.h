#pragma once

#include <stdlib.h>
#include "scpi/ieee488.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include "system_bus_model.h"

scpi_result_t cmd_source_dac(scpi_t *context);
scpi_result_t cmd_source_dac_q(scpi_t *context);

scpi_result_t cmd_source_res(scpi_t *context);
scpi_result_t cmd_source_res_q(scpi_t *context);

scpi_result_t cmd_source_reset(scpi_t *context);