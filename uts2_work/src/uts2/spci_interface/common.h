#pragma once

/***************************************************************************************************
 *                                         INCLUDED FILES
 **************************************************************************************************/
#include "scpi/scpi.h"


scpi_result_t scpi_cmd_idn_q(scpi_t *context);
scpi_result_t scpi_cmd_rst(scpi_t *context);
scpi_result_t scpi_cmd_cls(scpi_t *context);
scpi_result_t scpi_cmd_opc_q(scpi_t *context);
scpi_result_t scpi_cmd_opc(scpi_t *context);
