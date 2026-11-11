#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "source.h"
#include "scpi/ieee488.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include "system_bus_model.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/dac.h>
#include "scpi/scpi.h"

#define DAC_MAX_RAW_VALUE 4095



/* ========================================================================= */
/* SCPI-КОЛБЭКИ                                                              */
/* ========================================================================= */

// SOURce:DAC[:VALue] <0..4095>
scpi_result_t cmd_source_dac(scpi_t *context)
{
    int32_t val;

    // Считываем целочисленный код
    if (!SCPI_ParamInt32(context, &val, TRUE))
     {
        return SCPI_RES_ERR;
    }

    // Проверка диапазона 12-битного ЦАП
    if (val < 0 || val > DAC_MAX_RAW_VALUE) 
    {
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE); // Ошибка -222
        return SCPI_RES_ERR;
    }

    SERVER_CH_SET_ASYC(DAC_VALUE, (uint32_t)val); 

    return SCPI_RES_OK;
}

// SOURce:DAC[:VALue]?
scpi_result_t cmd_source_dac_q(scpi_t *context)
{
    uint32_t _val;
    SYSTEM_BUS_GET(DAC_VALUE, &_val);
    SCPI_ResultUInt32(context, _val);
    return SCPI_RES_OK;
}

// Сброс по *RST: обнуление выхода ЦАП (безопасное состояние 0 В)
scpi_result_t cmd_dac_reset(scpi_t *context)
{
    (void)context;
    SERVER_CH_SET_ASYC(DAC_VALUE, (uint32_t)DAC_MAX_RAW_VALUE ); 
    return SCPI_RES_OK;
}