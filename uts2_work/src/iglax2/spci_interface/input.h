#pragma once

/***************************************************************************************************
 *                                         INCLUDED FILES
 **************************************************************************************************/

#include "scpi/scpi.h"
#include "scpi/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************************************************
 *                                    PUBLIC FUNCTION PROTOTYPES
 **************************************************************************************************/

scpi_result_t cmd_input_din_state_q(scpi_t *context);
scpi_result_t cmd_input_din_filter(scpi_t *context);
scpi_result_t cmd_input_din_pullup(scpi_t *context);
scpi_result_t cmd_input_din_pullup_q(scpi_t *context);

#ifdef __cplusplus
}
#endif