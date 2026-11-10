#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "output.h"
#include "scpi/ieee488.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include "system_bus_model.h"

#define NUM_PWR_CHANNELS POWER_CONTROL_LINE_CNT

static const scpi_choice_def_t power_channel_choices[] = 
{
    {"DUT2_PSU", EN_DUT2_PSU},
    {"DUT3_PSU", EN_DUT3_PSU},
    {"P12V",     EN_P12V},
    {"P40V",     EN_P24V},
    {"VA",       EN_VA},
    {"USB_OUT",  EN_USB_OUT},
    {"USB_BOOT", EN_USB_BOOT},
    {"USB_TOP",  EN_USB_TOP},
    SCPI_CHOICE_LIST_END
};

scpi_result_t cmd_output_state(scpi_t *context) 
{
    scpi_bool_t state;
    int32_t channel;


    // Считываем 1-й параметр: имя канала
    if (!SCPI_ParamChoice(context, power_channel_choices, &channel, TRUE)) 
    {
        return SCPI_RES_ERR;
    }
    // Считываем 2-й параметр: состояние (ON/OFF/1/0)
    if (!SCPI_ParamBool(context, &state, TRUE)) 
    {
        return SCPI_RES_ERR;
    }
       
    SERVER_CH_SET_ASYC(channel, (bool)state);

    return SCPI_RES_OK;
}

/* Запрос: OUTPut[:STATe]? <channel> 
 * Пример: "OUTP? P12V" -> вернет 1 или 0
 */
scpi_result_t cmd_output_state_q(scpi_t *context) 
{
    int32_t channel;
    bool val;
    if (SCPI_ParamChoice(context, power_channel_choices, &channel, TRUE)) 
    {    
        SYSTEM_BUS_GET(channel,&val);
        SCPI_ResultBool(context, val);   
    }
    else 
    {
        for (int i = 0; i < ARRAY_SIZE(power_channel_choices) - 1; i++) 
        {        
            SYSTEM_BUS_GET(power_channel_choices[i].tag,&val);
            SCPI_ResultBool(context, val);
        }
    }    
    return SCPI_RES_OK;
}

/* Команда: OUTPut:ALL <state>
 * Пример: "OUTP:ALL OFF" -> мгновенно гасит все 8 каналов
 */
scpi_result_t cmd_output_all(scpi_t *context) 
{
    scpi_bool_t state;
    if (!SCPI_ParamBool(context, &state, TRUE)) 
    {
        return SCPI_RES_ERR;
    }

    for (int i = 0; i < ARRAY_SIZE(power_channel_choices) - 1; i++) 
    {        
        SERVER_CH_SET_ASYC(power_channel_choices[i].tag, (bool)state);
    }
    return SCPI_RES_OK;
}



static const scpi_choice_def_t lin_pd_channel_choices[] = {
    {"LIN1", LIN_PD1},
    {"LIN2", LIN_PD2},
    {"LIN3", LIN_PD3},
    {"LIN4", LIN_PD4},
    SCPI_CHOICE_LIST_END
};


static K_SEM_DEFINE(lin_pb_done, 0, 1);
static uint8_t lin_pb_response = 0;

/* 2. Колбэк установки: OUTPut:LIN:GND[:STATe] <channel>, <state> */
scpi_result_t cmd_output_lin_gnd(scpi_t *context)
{
    int32_t channel;
    scpi_bool_t state;

    if (!SCPI_ParamChoice(context, lin_pd_channel_choices, &channel, TRUE)) return SCPI_RES_ERR;
    if (!SCPI_ParamBool(context, &state, TRUE)) return SCPI_RES_ERR;

    k_sem_reset(&lin_pb_done);

    SERVER_CH_SET_SYC(channel, (uint32_t)(state == true ? 1 : 0), &lin_pb_done, &lin_pb_response);

    if (false 
        || k_sem_take(&lin_pb_done, K_MSEC(100)) != 0
        || lin_pb_response != 0
    ) 
    {
        SCPI_ErrorPush(context, SCPI_ERROR_HARDWARE_ERROR);
        return SCPI_RES_ERR; 
    }
    
    return SCPI_RES_OK;
}

/* 3. Колбэк запроса: OUTPut:LIN:GND[:STATe]? [<channel>] */
scpi_result_t cmd_output_lin_gnd_q(scpi_t *context)
{
    int32_t channel;
    uint32_t val;
    // Если указан конкретный канал (например: OUTP:LIN:GND? LIN1)
    if (SCPI_ParamChoice(context, lin_pd_channel_choices, &channel, FALSE))
    {
        SYSTEM_BUS_GET(channel, &val);
        SCPI_ResultBool(context, val == 1 ? true : false);
    } 
    else 
    {
        // Если без параметров (OUTP:LIN:GND?) -> отдаем все 4 канала через запятую
        for (int i = 0; i < ARRAY_SIZE(lin_pd_channel_choices) - 1; i++) 
        {
            SYSTEM_BUS_GET(lin_pd_channel_choices[i].tag,&val);
            SCPI_ResultBool(context, val == 1 ? true : false);
        }
    }
    return SCPI_RES_OK;
}


scpi_result_t cmd_out_reset(scpi_t *context)
{
    for (int i = 0; i < ARRAY_SIZE(power_channel_choices) - 1; i++) 
    {        
        SERVER_CH_SET_ASYC(power_channel_choices[i].tag, (bool)false);
    }
    for (int i = 0; i < ARRAY_SIZE(lin_pd_channel_choices) - 1; i++) 
    {
        SERVER_CH_SET_ASYC(lin_pd_channel_choices[i].tag,(uint32_t)0);    
    }
    return SCPI_RES_OK;
}
