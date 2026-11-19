#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <zephyr/kernel.h>
#include "spci_uts2.h"
#include "system_bus_model.h"

scpi_result_t scpi_cmd_idn_q(scpi_t *context) {
    for (int i = 0; i < 4; i++) {
        if (context->idn[i]) {
            SCPI_ResultMnemonic(context, context->idn[i]);
        } else {
            SCPI_ResultMnemonic(context, "0");
        }
    }
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_rst(scpi_t *context) {
    if (context && context->interface && context->interface->reset) {
        return context->interface->reset(context);
    }
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_cls(scpi_t *context) {
    SCPI_ErrorClear(context);
    return SCPI_RES_OK;
}

/* Запрос Operation Complete: возвращает 1 */
scpi_result_t scpi_cmd_opc_q(scpi_t *context) {
    SCPI_ResultInt32(context, 1);
    return SCPI_RES_OK;
}

/* Команда Operation Complete (без вопроса): просто подтверждаем выполнение */
scpi_result_t scpi_cmd_opc(scpi_t *context) {
    return SCPI_RES_OK;
}