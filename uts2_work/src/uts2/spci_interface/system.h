#pragma once

#include <stdlib.h>
#include "scpi/ieee488.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include "system_bus_model.h"

scpi_result_t cmd_system_wait(scpi_t *context);
scpi_result_t cmd_system_uid_q(scpi_t *context);
scpi_result_t cmd_system_serial_q(scpi_t *context);
scpi_result_t cmd_system_serial(scpi_t *context);

scpi_result_t cmd_system_hardware(scpi_t *context);
scpi_result_t cmd_system_hardware_q(scpi_t *context);

scpi_result_t cmd_system_user(scpi_t *context);
scpi_result_t cmd_system_user_q(scpi_t *context);
