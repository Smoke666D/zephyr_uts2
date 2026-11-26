#include <zephyr/kernel.h>
#include "scpi/scpi.h"
#include "system_bus_model.h"

#define DIN_CH_COUNT 4

/* Словарь имен каналов для парсера SCPI */
static const scpi_choice_def_t din_channel_choices[] = {
    {"IN_GREAY", IN_GREY},
    {"IN_IGN",   IN_IGN},
    {"IN3",      IN3},
    {"IN4",      IN4},
    SCPI_CHOICE_LIST_END
};


static const scpi_choice_def_t din_pup_choices[] = {
    {"IN3", IN3_PU_ON},
    {"IN4",   IN4_PU_ON},    
    SCPI_CHOICE_LIST_END
};


/* ========================================================================= */
/* SCPI-КОЛБЭК: INPut:DIN[:STATe]? [<channel>]                               */
/* ========================================================================= */
scpi_result_t cmd_input_din_state_q(scpi_t *context)
{
    int32_t ch_idx;
    bool val;

    // 1. Запрос конкретного входа: INP:DIN? IN_IGN
    if (SCPI_ParamChoice(context, din_channel_choices, &ch_idx, FALSE)) {
        if (SYSTEM_BUS_GET(ch_idx, &val) != 0) {
            SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR); // -200
            return SCPI_RES_ERR;
        }
        SCPI_ResultBool(context, val);
    } 
    // 2. Запрос всех 4-х входов: INP:DIN?
    else {
        for (int i = 0; i < ARRAY_SIZE(din_channel_choices) -1 ; i++) {
            if (SYSTEM_BUS_GET(din_channel_choices[i].tag, &val) != 0) 
            {
                SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
                return SCPI_RES_ERR;
            }
            SCPI_ResultBool(context, val);
        }
    }

    return SCPI_RES_OK;
}


uint32_t din_setting[4][2] =
{
    {IN_GREY_ON_GF,IN_GREY_OFF_GF},
    {IN_IGN_ON_GF, IN_IGN_OFF_GF},
    {IN_3_ON_GF,IN_3_OFF_GF},  
    {IN_4_ON_GF, IN_4_OFF_GF}
};

scpi_result_t cmd_input_din_filter(scpi_t *context)
{
    int32_t ch_idx;
    int32_t delay_on, delay_off;

    if (!SCPI_ParamChoice(context, din_channel_choices, &ch_idx, TRUE)) return SCPI_RES_ERR;
    if (!SCPI_ParamInt32(context, &delay_on, TRUE)) return SCPI_RES_ERR;
    if (!SCPI_ParamInt32(context, &delay_off, TRUE)) return SCPI_RES_ERR;

    // Диапазон антидребезга: от 1 до 5000 мс
    if (false 
        || delay_on < 1 
        || delay_on > 5000 
        || delay_off < 1 
        || delay_off > 5000
    )
     {
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
        return SCPI_RES_ERR;
    }

    SERVER_CH_SET(din_setting[ch_idx][0],(uint32_t)delay_on);
    SERVER_CH_SET(din_setting[ch_idx][1],(uint32_t)delay_off);

    return SCPI_RES_OK;
}


/* ========================================================================= */
/* КОЛБЭК УСТАНОВКИ: INPut:DIN:PULLup <channel>, <state>                     */
/* ========================================================================= */
scpi_result_t cmd_input_din_pullup(scpi_t *context)
{
    int32_t ch_idx;
    scpi_bool_t state;

    // 1. Читаем имя канала (IN_GREAY, IN_IGN, IN3, IN4)
    if (!SCPI_ParamChoice(context, din_pup_choices, &ch_idx, TRUE)) {
        return SCPI_RES_ERR;
    }

    // 2. Читаем желаемое состояние подтяжки (0/1/OFF/ON)
    if (!SCPI_ParamBool(context, &state, TRUE)) {
        return SCPI_RES_ERR;
    }   

    SERVER_CH_SET_ASYC(ch_idx, (bool)state);

    return SCPI_RES_OK;
}

/* ========================================================================= */
/* КОЛБЭК ЗАПРОСА: INPut:DIN:PULLup? <channel>                               */
/* ========================================================================= */
scpi_result_t cmd_input_din_pullup_q(scpi_t *context)
{
    
      int32_t channel;
    bool val = false;

    if (SCPI_ParamChoice(context, din_pup_choices, &channel, FALSE)) 
    {    
        SYSTEM_BUS_GET(channel, &val);
        SCPI_ResultBool(context, val);   
    }
    else 
    {
        for (int i = 0; i < ARRAY_SIZE(din_pup_choices) - 1; i++) 
        {        
            SYSTEM_BUS_GET(din_pup_choices[i].tag, &val);
            SCPI_ResultBool(context, val);
        }
    }    
    return SCPI_RES_OK;

    

    
}