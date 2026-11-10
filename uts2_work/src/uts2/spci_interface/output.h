#pragma once

#include "scpi/scpi.h"
#include "scpi/error.h"


scpi_result_t cmd_output_state(scpi_t *context);
scpi_result_t cmd_output_state_q(scpi_t *context);
scpi_result_t cmd_output_all(scpi_t *context);

scpi_result_t cmd_out_reset(scpi_t *context);

scpi_result_t cmd_output_lin_gnd(scpi_t *context);
scpi_result_t cmd_output_lin_gnd_q(scpi_t *context);

scpi_result_t cmd_output_mux(scpi_t *context);
scpi_result_t cmd_output_mux_q(scpi_t *context);

scpi_result_t cmd_output_dout(scpi_t *context);
scpi_result_t cmd_output_dout_q(scpi_t *context);


scpi_result_t cmd_mux_jtag(scpi_t *context);
scpi_result_t cmd_mux_jtag_q(scpi_t *context);
scpi_result_t cmd_mux_uart(scpi_t *context);
scpi_result_t cmd_mux_uart_q(scpi_t *context);