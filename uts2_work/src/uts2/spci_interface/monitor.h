
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

scpi_result_t cmd_monitor_start(scpi_t *context);
scpi_result_t cmd_monitor_stop(scpi_t *context);
scpi_result_t cmd_monitor_state_q(scpi_t *context);
scpi_result_t cmd_monitor_reset(scpi_t *context);
void scpi_monitor_init(void);

#ifdef __cplusplus
}
#endif

/***************************************************************************************************
 *                                           END OF FILE
 **************************************************************************************************/