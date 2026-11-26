#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include "scpi/scpi.h"
#include "sense.h"
#include "system_bus_model.h"



/* Словарь параметров для парсера SCPI */
static const scpi_choice_def_t accel_axis_choices[] = {
    {"X",   ACC_X},
    {"Y",   ACC_Y},
    {"Z",   ACC_Z},    
    SCPI_CHOICE_LIST_END
};

/* ========================================================================= */
/* SCPI-КОЛБЭК                                                               */
/* ========================================================================= */

// SENSe:ACCeleration[:DATA]? [<X|Y|Z|ALL>]
scpi_result_t cmd_sense_accel_q(scpi_t *context)
{
    int32_t axis;

    // Считываем опциональный параметр оси (X, Y, Z, ALL)
    if (SCPI_ParamChoice(context, accel_axis_choices, &axis, FALSE)) 
    {    
        float data;
        SYSTEM_BUS_GET(axis,&data);        
        // Возвращаем результат
        SCPI_ResultFloat(context, (float)data);
        
    }
    else
    {
        float _val;
        for (int i = 0; i < ARRAY_SIZE(accel_axis_choices) - 1; i++) 
        {        
            SYSTEM_BUS_GET(accel_axis_choices[i].tag, &_val);
            SCPI_ResultFloat(context, _val);
        }
    }  
    return SCPI_RES_OK;
}