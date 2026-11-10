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



static const scpi_choice_def_t mux_channel_choices[] = {
    {"JTAG_MUX_EN", JTAG_MUX_EN},
    {"EN_USB_UART", EN_USB_UART},
    {"EN_USB_OUT1", EN_USB_OUT1},
    {"EN_USB_OUT2", EN_USB_OUT2},
    {"EN_USB_OUT3", EN_USB_OUT3},
    {"UART_FB_EN",  UART_FB_EN},
    {"UART_RX_EN",  UART_RX_EN},
    {"OK1",         OK1},
    {"OK2",         OK2},
    SCPI_CHOICE_LIST_END
};

static K_SEM_DEFINE(mux_done, 0, 1);
static uint8_t mux_response = 0;

/* 2. Колбэк установки: OUTPut:LIN:GND[:STATe] <channel>, <state> */
scpi_result_t cmd_output_mux(scpi_t *context)
{
    int32_t channel;
    scpi_bool_t state;

    if (!SCPI_ParamChoice(context, mux_channel_choices, &channel, TRUE)) return SCPI_RES_ERR;
    if (!SCPI_ParamBool(context, &state, TRUE)) return SCPI_RES_ERR;

    k_sem_reset(&mux_done);

    SERVER_CH_SET_SYC(channel, state , &mux_done, &mux_response);

    if (false 
        || k_sem_take(&mux_done, K_MSEC(100)) != 0
        || mux_response != 0
    ) 
    {
        SCPI_ErrorPush(context, SCPI_ERROR_HARDWARE_ERROR);
        return SCPI_RES_ERR; 
    }
    
    return SCPI_RES_OK;
}

/* 3. Колбэк запроса: OUTPut:LIN:GND[:STATe]? [<channel>] */
scpi_result_t cmd_output_mux_q(scpi_t *context)
{
    int32_t channel;
    bool val;
    // Если указан конкретный канал (например: OUTP:LIN:GND? LIN1)
    if (SCPI_ParamChoice(context, mux_channel_choices, &channel, FALSE))
    {
        SYSTEM_BUS_GET(channel, &val);
        SCPI_ResultBool(context, val);
    } 
    else 
    {
        // Если без параметров (OUTP:LIN:GND?) -> отдаем все 4 канала через запятую
        for (int i = 0; i < ARRAY_SIZE(mux_channel_choices) - 1; i++) 
        {
            SYSTEM_BUS_GET(mux_channel_choices[i].tag, &val);
            SCPI_ResultBool(context, val);
        }
    }
    return SCPI_RES_OK;
}


#define DOUT_CH_COUNT 18

static K_SEM_DEFINE(dout_done, 0, 1);
static uint8_t dout_response = 0;

/* Словарь допустимых состояний */
static const scpi_choice_def_t dout_state_choices[] = {
    {"IN",    STATE_IN},
    {"LOW",   STATE_LOW},
    {"HIGH",  STATE_HIGH},
    {"SHORt", STATE_SHORT},
    SCPI_CHOICE_LIST_END
};

static uint8_t dout_states[DOUT_CH_COUNT] = {STATE_IN};

static const char * const dout_state_names[] = {
    [STATE_IN]    = "IN",
    [STATE_LOW]   = "LOW",
    [STATE_HIGH]  = "HIGH",
    [STATE_SHORT] = "SHORT"
};




// OUTPut:DOUT[:STATe] <1..18>, <state>
scpi_result_t cmd_output_dout(scpi_t *context)
{
    int32_t channel;
    int32_t state;

    // 1-й параметр: номер канала ЧИСЛОМ (1..18)
    if (!SCPI_ParamInt32(context, &channel, TRUE)) {
        return SCPI_RES_ERR;
    }

    // Проверка диапазона номеров каналов
    if (channel < 1 || channel > DOUT_CH_COUNT) 
    {
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE); // Ошибка -222
        return SCPI_RES_ERR;
    }

    // 2-й параметр: режим (IN, LOW, HIGH, SHORT)
    if (!SCPI_ParamChoice(context, dout_state_choices, &state, TRUE)) 
    {
        return SCPI_RES_ERR;
    }

    // Ограничение: каналы 17 и 18 не поддерживают SHORT
    if (channel >= 17 && state == STATE_SHORT)
     {
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE); // Ошибка -224
        return SCPI_RES_ERR;
    }

    k_sem_reset(&dout_done);

    SERVER_CH_SET_SYC(DOUT1 + channel - 1, (uint32_t)state , &dout_done, &dout_response);

    if (false 
        || k_sem_take(&dout_done, K_MSEC(100)) != 0
        || dout_response != 0
    ) 
    {
        SCPI_ErrorPush(context, SCPI_ERROR_HARDWARE_ERROR);
        return SCPI_RES_ERR; 
    }
    
    return SCPI_RES_OK;
}

// OUTPut:DOUT[:STATe]? [<1..18>]
scpi_result_t cmd_output_dout_q(scpi_t *context)
{
    int32_t channel;

    // Проверяем, передан ли номер канала (опциональный параметр)
    if (SCPI_ParamInt32(context, &channel, FALSE)) 
    {
        if (channel < 1 || channel > DOUT_CH_COUNT) 
        {
            SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
            return SCPI_RES_ERR;
        }
        // Запрос одного канала: OUTP:DOUT? 1 -> "HIGH"
        uint32_t _val;
        SYSTEM_BUS_GET((DOUT1 + channel - 1),&_val);

        SCPI_ResultMnemonic(context, dout_state_names[_val]);
    } 
    else 
    {
        // Если параметра не было, отдаем все 18 состояний через запятую
        for (int i = 0; i < DOUT_CH_COUNT; i++) 
        {
            uint32_t _val;
            SYSTEM_BUS_GET((DOUT1 + i),&_val);
            SCPI_ResultMnemonic(context, dout_state_names[_val]);
        }
    }
    return SCPI_RES_OK;
}


#define JTAG_MAX_CHANNELS  5
#define UART_MAX_CHANNELS  4



// OUTPut:MUX:JTAG[:CHANnel] <1..5>
scpi_result_t cmd_mux_jtag(scpi_t *context)
{
    int32_t _ch;
    if (!SCPI_ParamInt32(context, &_ch, TRUE)) 
    {
        return SCPI_RES_ERR;
    }

    if (_ch < 1 || _ch > JTAG_MAX_CHANNELS) 
    {
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE); // Ошибка -222
        return SCPI_RES_ERR;
    }

    k_sem_reset(&mux_done);

    SERVER_CH_SET_SYC(JTAG_MUX0, (uint32_t)_ch , &mux_done, &mux_response);

    if (false 
        || k_sem_take(&mux_done, K_MSEC(100)) != 0
        || mux_response != 0
    ) 
    {
        SCPI_ErrorPush(context, SCPI_ERROR_HARDWARE_ERROR);
        return SCPI_RES_ERR; 
    }
    return SCPI_RES_OK;
}

scpi_result_t cmd_mux_jtag_q(scpi_t *context)
{
    uint32_t _val;
    SYSTEM_BUS_GET(JTAG_MUX0,&_val);
    SCPI_ResultInt32(context, _val);
    return SCPI_RES_OK;
}

// OUTPut:MUX:UART[:CHANnel] <1..4>
scpi_result_t cmd_mux_uart(scpi_t *context)
{
    int32_t _ch;
    if (!SCPI_ParamInt32(context, &_ch, TRUE)) 
    {
        return SCPI_RES_ERR;
    }

    if (_ch < 1 || _ch > UART_MAX_CHANNELS) 
    {
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE); // Ошибка -222
        return SCPI_RES_ERR;
    }

    k_sem_reset(&mux_done);

    SERVER_CH_SET_SYC(USB_FB_MUX, (uint32_t)_ch , &mux_done, &mux_response);

    if (false 
        || k_sem_take(&mux_done, K_MSEC(100)) != 0
        || mux_response != 0
    ) 
    {
        SCPI_ErrorPush(context, SCPI_ERROR_HARDWARE_ERROR);
        return SCPI_RES_ERR; 
    }
    
    return SCPI_RES_OK;
}

scpi_result_t cmd_mux_uart_q(scpi_t *context)
{
    uint32_t _val;
    SYSTEM_BUS_GET(USB_FB_MUX,&_val);
    SCPI_ResultInt32(context, _val);
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
     for (int i = 0; i < ARRAY_SIZE(mux_channel_choices) - 1; i++) 
    {
        SERVER_CH_SET_ASYC(mux_channel_choices[i].tag,(bool)false);    
    }
    for (int i = 0; i < DOUT_CH_COUNT; i++) 
    {    
        SERVER_CH_SET_ASYC(DOUT1 + i,(uint32_t)STATE_IN);     
    }
    SERVER_CH_SET_ASYC(JTAG_MUX0,(uint32_t)1);     
    SERVER_CH_SET_ASYC(USB_FB_MUX,(uint32_t)1);     
    return SCPI_RES_OK;
}
