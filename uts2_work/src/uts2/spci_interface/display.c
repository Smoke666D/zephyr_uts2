#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "scpi/ieee488.h"
#include <zephyr/kernel.h>
#include "display.h"
#include "system_bus_model.h"



/* 2. Словарь доступных идентификаторов для SCPI */
static const scpi_choice_def_t led_targets[] = {
    {"RED",   LED1},
    {"GREen", LED2},
    {"BLUe",  LED3},
    SCPI_CHOICE_LIST_END
};


/* ========================================================================= */
/* SCPI-КОЛБЭКИ                                                              */
/* ========================================================================= */

/* Команда установки: DISPlay:LED:STATe <TARGET>, <STATE> */
scpi_result_t cmd_disp_led_state(scpi_t *context)
{
    int32_t target;
    scpi_bool_t state;

    /* Читаем 1-й параметр: имя целевого индикатора */
    if (!SCPI_ParamChoice(context, led_targets, &target, TRUE)) 
    {
        return SCPI_RES_ERR;
    }

    /* Читаем 2-й параметр: булево состояние (0/1/OFF/ON) */
    if (!SCPI_ParamBool(context, &state, TRUE)) 
    {
        return SCPI_RES_ERR;
    }

    SERVER_CH_SET_ASYC(target, (bool)state);

    
    return SCPI_RES_OK;
}

/* Команда запроса: DISPlay:LED:STATe? <TARGET> */
scpi_result_t cmd_disp_led_state_q(scpi_t *context)
{
    int32_t target;
    bool val;
    /* Читаем параметр запроса: имя целевого индикатора */
    if (!SCPI_ParamChoice(context, led_targets, &target, TRUE)) {
        return SCPI_RES_ERR;
    }
     SYSTEM_BUS_GET(target, &val);
    /* Возвращаем текущее состояние (1 или 0) */

    SCPI_ResultBool(context, val);
    return SCPI_RES_OK;
}


scpi_result_t cmd_led_reset(scpi_t *context)
{    
    SERVER_CH_SET_ASYC(LED1, (bool)false);
    SERVER_CH_SET_ASYC(LED2, (bool)false);
    SERVER_CH_SET_ASYC(LED3, (bool)false);
}
