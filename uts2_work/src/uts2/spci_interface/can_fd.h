#pragma once

/***************************************************************************************************
 *                                         INCLUDED FILES
 **************************************************************************************************/
#include "scpi/scpi.h"



scpi_result_t cmd_can_baud(scpi_t *context);
scpi_result_t cmd_can_baud_q(scpi_t *context);
scpi_result_t cmd_can_mode(scpi_t *context);
scpi_result_t cmd_can_state(scpi_t *context);
scpi_result_t cmd_can_transmit(scpi_t *context);
scpi_result_t cmd_can_receive_q(scpi_t *context);
scpi_result_t cmd_can_count_q(scpi_t *context);
scpi_result_t cmd_can_status_q(scpi_t *context);
scpi_result_t cmd_can_clear(scpi_t *context);