#pragma once

#include "scpi/scpi.h"
#include "scpi/error.h"


scpi_result_t cmd_output_state(scpi_t *context);
scpi_result_t cmd_output_state_q(scpi_t *context);
scpi_result_t cmd_output_all(scpi_t *context);

scpi_result_t cmd_out_reset(scpi_t *context);

scpi_result_t cmd_output_lin_gnd(scpi_t *context);
scpi_result_t cmd_output_lin_gnd_q(scpi_t *context);