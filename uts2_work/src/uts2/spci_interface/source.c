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


#define AD5243_CH_COUNT      2
#define AD5243_MAX_RAW_VAL  255

static K_SEM_DEFINE(dac_done, 0, 1);
static uint8_t dac_response = 0;

// SOURce:RESistance[:VALue] <1..2>, <0..255>
scpi_result_t cmd_source_res(scpi_t *context)
{
    int32_t channel;
    int32_t val;

    // 1-й параметр: номер канала (1 или 2)
    if (!SCPI_ParamInt32(context, &channel, TRUE)) 
    {
        return SCPI_RES_ERR;
    }
    if (channel < 1 || channel > AD5243_CH_COUNT) 
    {
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE); // -222
        return SCPI_RES_ERR;
    }

    // 2-й параметр: 8-битный код (0..255)
    if (!SCPI_ParamInt32(context, &val, TRUE)) 
    {
        return SCPI_RES_ERR;
    }
    if (val < 0 || val > AD5243_MAX_RAW_VAL) {
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE); // -222
        return SCPI_RES_ERR;
    }

    k_sem_reset(&dac_done);

    SERVER_CH_SET_SYC(channel == 1 ? ADJ_RES1 : ADJ_RES2, (uint32_t)val, &dac_done, &dac_response);
     if (false 
        || k_sem_take(&dac_done, K_MSEC(300)) != 0
        || dac_response != 0
       ) 
    {
        SCPI_ErrorPush(context, SCPI_ERROR_HARDWARE_ERROR);
        return SCPI_RES_ERR; 
    }    
    return SCPI_RES_OK;
}

// SOURce:RESistance[:VALue]? [<1..2>]
scpi_result_t cmd_source_res_q(scpi_t *context)
{
    int32_t channel;
    uint32_t _val;
    // Запрос одного канала: SOUR:RES? 1
    if (SCPI_ParamInt32(context, &channel, FALSE)) 
    {
        if (channel < 1 || channel > AD5243_CH_COUNT) 
        {
            SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
            return SCPI_RES_ERR;
        }       
        SYSTEM_BUS_GET(channel == 1 ? ADJ_RES1 : ADJ_RES2,&_val);
        SCPI_ResultUInt32(context, _val);
    }     
    else 
    {
        SYSTEM_BUS_GET(ADJ_RES1 ,&_val);
        SCPI_ResultUInt32(context, _val);
        SYSTEM_BUS_GET(ADJ_RES2,&_val);
        SCPI_ResultUInt32(context, _val);                
    }
    return SCPI_RES_OK;
}


// Сброс по *RST: обнуление выхода ЦАП (безопасное состояние 0 В)
scpi_result_t cmd_source_reset(scpi_t *context)
{
    (void)context;
    SERVER_CH_SET_ASYC(DAC_VALUE, (uint32_t)DAC_MAX_RAW_VALUE ); 
    SERVER_CH_SET_ASYC(ADJ_RES1, (uint32_t)0); 
    SERVER_CH_SET_ASYC(ADJ_RES2, (uint32_t)0); 
    return SCPI_RES_OK;
}