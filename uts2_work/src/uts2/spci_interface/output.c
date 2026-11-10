/**
 *  @file       output.c
 *  @headerfile output.h
 *
 *  @date       2026.11.10
 *  @author     Dymov Igor
 *
 *  @brief      Обработчики SCPI-команд управления силовыми выходами и мультиплексорами
 *  @details    Реализует команды установки и запроса состояния каналов питания,
 *              линий LIN, мультиплексоров JTAG/UART и дискретных выходов DOUT.
 */

/***************************************************************************************************
 *                                          INCLUDED FILES
 **************************************************************************************************/
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include "output.h"
#include "scpi/ieee488.h"
#include "system_bus_model.h"

/***************************************************************************************************
 *                                           DEFINITIONS
 **************************************************************************************************/
#define NUM_PWR_CHANNELS   POWER_CONTROL_LINE_CNT
#define DOUT_CH_COUNT      18
#define JTAG_MAX_CHANNELS  5
#define UART_MAX_CHANNELS  4

/***************************************************************************************************
 *                                          PRIVATE TYPES
 **************************************************************************************************/

/***************************************************************************************************
 *                                   PRIVATE FUNCTION PROTOTYPES
 **************************************************************************************************/

/***************************************************************************************************
 *                                           PRIVATE DATA
 **************************************************************************************************/
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

static const scpi_choice_def_t lin_pd_channel_choices[] = 
{
    {"LIN1", LIN_PD1},
    {"LIN2", LIN_PD2},
    {"LIN3", LIN_PD3},
    {"LIN4", LIN_PD4},
    SCPI_CHOICE_LIST_END
};

static K_SEM_DEFINE(sem_lin_pb_done, 0, 1);
static uint8_t lin_pb_response = 0;

static const scpi_choice_def_t mux_channel_choices[] = 
{
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

static K_SEM_DEFINE(sem_mux_done, 0, 1);
static uint8_t mux_response = 0;

static K_SEM_DEFINE(sem_dout_done, 0, 1);
static uint8_t dout_response = 0;

// Словарь допустимых состояний
static const scpi_choice_def_t dout_state_choices[] = 
{
    {"IN",    STATE_IN},
    {"LOW",   STATE_LOW},
    {"HIGH",  STATE_HIGH},
    {"SHORt", STATE_SHORT},
    SCPI_CHOICE_LIST_END
};

static const char *const dout_state_names[] = 
{
    [STATE_IN]    = "IN",
    [STATE_LOW]   = "LOW",
    [STATE_HIGH]  = "HIGH",
    [STATE_SHORT] = "SHORT"
};

/***************************************************************************************************
 *                                        PUBLIC FUNCTIONS
 **************************************************************************************************/

/**
 *  @brief      SCPI-команда установки состояния канала питания
 *  @details    Устанавливает логическое состояние выбранного силового канала.
 *
 *  @param      _context - Указатель на контекст парсера SCPI
 *
 *  @return     scpi_result_t - SCPI_RES_OK при успехе, SCPI_RES_ERR при ошибке
 */
scpi_result_t cmd_output_state(scpi_t *_context) 
{
    scpi_bool_t state;
    int32_t channel;

    // Считываем 1-й параметр: имя канала
    if (!SCPI_ParamChoice(_context, power_channel_choices, &channel, TRUE)) 
    {
        return SCPI_RES_ERR;
    }

    // Считываем 2-й параметр: состояние (ON/OFF/1/0)
    if (!SCPI_ParamBool(_context, &state, TRUE)) 
    {
        return SCPI_RES_ERR;
    }
       
    SERVER_CH_SET_ASYC(channel, (bool)state);

    return SCPI_RES_OK;
}

/**
 *  @brief      SCPI-запрос состояния канала питания
 *  @details    Возвращает состояние указанного канала или всех каналов питания.
 *
 *  @param      _context - Указатель на контекст парсера SCPI
 *
 *  @return     scpi_result_t - Всегда SCPI_RES_OK
 */
scpi_result_t cmd_output_state_q(scpi_t *_context) 
{
    int32_t channel;
    bool val = false;

    if (SCPI_ParamChoice(_context, power_channel_choices, &channel, TRUE)) 
    {    
        SYSTEM_BUS_GET(channel, &val);
        SCPI_ResultBool(_context, val);   
    }
    else 
    {
        for (int i = 0; i < ARRAY_SIZE(power_channel_choices) - 1; i++) 
        {        
            SYSTEM_BUS_GET(power_channel_choices[i].tag, &val);
            SCPI_ResultBool(_context, val);
        }
    }    
    return SCPI_RES_OK;
}

/**
 *  @brief      SCPI-команда установки состояния всех каналов питания
 *  @details    Одновременно переводит все силовые каналы в указанное состояние.
 *
 *  @param      _context - Указатель на контекст парсера SCPI
 *
 *  @return     scpi_result_t - SCPI_RES_OK при успехе, SCPI_RES_ERR при ошибке
 */
scpi_result_t cmd_output_all(scpi_t *_context) 
{
    scpi_bool_t state;

    if (!SCPI_ParamBool(_context, &state, TRUE)) 
    {
        return SCPI_RES_ERR;
    }

    for (int i = 0; i < ARRAY_SIZE(power_channel_choices) - 1; i++) 
    {        
        SERVER_CH_SET_ASYC(power_channel_choices[i].tag, (bool)state);
    }

    return SCPI_RES_OK;
}

/**
 *  @brief      SCPI-команда управления подтяжкой LIN к GND
 *  @details    Синхронно устанавливает состояние ключа подтяжки LIN и ждет ответа.
 *
 *  @param      _context - Указатель на контекст парсера SCPI
 *
 *  @return     scpi_result_t - SCPI_RES_OK при успехе, SCPI_RES_ERR при ошибке
 */
scpi_result_t cmd_output_lin_gnd(scpi_t *_context)
{
    int32_t channel;
    scpi_bool_t state;

    if (!SCPI_ParamChoice(_context, lin_pd_channel_choices, &channel, TRUE))
    {
        return SCPI_RES_ERR;
    }

    if (!SCPI_ParamBool(_context, &state, TRUE))
    {
        return SCPI_RES_ERR;
    }

    k_sem_reset(&sem_lin_pb_done);

    SERVER_CH_SET_SYC(
        channel, 
        (uint32_t)(state ? 1 : 0), 
        &sem_lin_pb_done, 
        &lin_pb_response
    );

    if (false 
        || k_sem_take(&sem_lin_pb_done, K_MSEC(100)) != 0
        || lin_pb_response != 0
       ) 
    {
        SCPI_ErrorPush(_context, SCPI_ERROR_HARDWARE_ERROR);
        return SCPI_RES_ERR; 
    }
    
    return SCPI_RES_OK;
}

/**
 *  @brief      SCPI-запрос состояния подтяжки LIN к GND
 *  @details    Возвращает состояние выбранного канала LIN или всех 4 каналов.
 *
 *  @param      _context - Указатель на контекст парсера SCPI
 *
 *  @return     scpi_result_t - Всегда SCPI_RES_OK
 */
scpi_result_t cmd_output_lin_gnd_q(scpi_t *_context)
{
    int32_t channel;
    uint32_t val = 0;

    // Если указан конкретный канал (например: OUTP:LIN:GND? LIN1)
    if (SCPI_ParamChoice(_context, lin_pd_channel_choices, &channel, FALSE))
    {
        SYSTEM_BUS_GET(channel, &val);
        SCPI_ResultBool(_context, val == 1);
    } 
    else 
    {
        // Если без параметров (OUTP:LIN:GND?) -> отдаем все 4 канала через запятую
        for (int i = 0; i < ARRAY_SIZE(lin_pd_channel_choices) - 1; i++) 
        {
            SYSTEM_BUS_GET(lin_pd_channel_choices[i].tag, &val);
            SCPI_ResultBool(_context, val == 1);
        }
    }
    return SCPI_RES_OK;
}

/**
 *  @brief      SCPI-команда управления каналами мультиплексора
 *  @details    Синхронно устанавливает состояние выбранного канала мультиплексора.
 *
 *  @param      _context - Указатель на контекст парсера SCPI
 *
 *  @return     scpi_result_t - SCPI_RES_OK при успехе, SCPI_RES_ERR при ошибке
 */
scpi_result_t cmd_output_mux(scpi_t *_context)
{
    int32_t channel;
    scpi_bool_t state;

    if (!SCPI_ParamChoice(_context, mux_channel_choices, &channel, TRUE))
    {
        return SCPI_RES_ERR;
    }

    if (!SCPI_ParamBool(_context, &state, TRUE))
    {
        return SCPI_RES_ERR;
    }

    k_sem_reset(&sem_mux_done);

    SERVER_CH_SET_SYC(channel, state, &sem_mux_done, &mux_response);

    if (false 
        || k_sem_take(&sem_mux_done, K_MSEC(100)) != 0
        || mux_response != 0
       ) 
    {
        SCPI_ErrorPush(_context, SCPI_ERROR_HARDWARE_ERROR);
        return SCPI_RES_ERR; 
    }
    
    return SCPI_RES_OK;
}

/**
 *  @brief      SCPI-запрос состояния каналов мультиплексора
 *  @details    Возвращает состояние указанного канала или всех каналов мультиплексора.
 *
 *  @param      _context - Указатель на контекст парсера SCPI
 *
 *  @return     scpi_result_t - Всегда SCPI_RES_OK
 */
scpi_result_t cmd_output_mux_q(scpi_t *_context)
{
    int32_t channel;
    bool val = false;

    // Если указан конкретный канал (например: OUTP:MUX? JTAG_MUX_EN)
    if (SCPI_ParamChoice(_context, mux_channel_choices, &channel, FALSE))
    {
        SYSTEM_BUS_GET(channel, &val);
        SCPI_ResultBool(_context, val);
    } 
    else 
    {
        // Если без параметров -> отдаем все каналы через запятую
        for (int i = 0; i < ARRAY_SIZE(mux_channel_choices) - 1; i++) 
        {
            SYSTEM_BUS_GET(mux_channel_choices[i].tag, &val);
            SCPI_ResultBool(_context, val);
        }
    }
    return SCPI_RES_OK;
}

/**
 *  @brief      SCPI-команда управления режимом работы дискретного выхода DOUT
 *  @details    Устанавливает режим (IN, LOW, HIGH, SHORT) для выбранного канала DOUT (1..18).
 *
 *  @param      _context - Указатель на контекст парсера SCPI
 *
 *  @return     scpi_result_t - SCPI_RES_OK при успехе, SCPI_RES_ERR при ошибке
 */
scpi_result_t cmd_output_dout(scpi_t *_context)
{
    int32_t channel;
    int32_t state;

    // 1-й параметр: номер канала ЧИСЛОМ (1..18)
    if (!SCPI_ParamInt32(_context, &channel, TRUE)) 
    {
        return SCPI_RES_ERR;
    }

    // Проверка диапазона номеров каналов
    if (false
        || channel < 1 
        || channel > DOUT_CH_COUNT
       ) 
    {
        SCPI_ErrorPush(_context, SCPI_ERROR_DATA_OUT_OF_RANGE); // Ошибка -222
        return SCPI_RES_ERR;
    }

    // 2-й параметр: режим (IN, LOW, HIGH, SHORT)
    if (!SCPI_ParamChoice(_context, dout_state_choices, &state, TRUE)) 
    {
        return SCPI_RES_ERR;
    }

    // Ограничение: каналы 17 и 18 не поддерживают SHORT
    if (true
        && channel >= 17 
        && state == STATE_SHORT
       )
    {
        SCPI_ErrorPush(_context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE); // Ошибка -224
        return SCPI_RES_ERR;
    }

    k_sem_reset(&sem_dout_done);

    SERVER_CH_SET_SYC(
        DOUT1 + channel - 1, 
        (uint32_t)state, 
        &sem_dout_done, 
        &dout_response
    );

    if (false 
        || k_sem_take(&sem_dout_done, K_MSEC(100)) != 0
        || dout_response != 0
       ) 
    {
        SCPI_ErrorPush(_context, SCPI_ERROR_HARDWARE_ERROR);
        return SCPI_RES_ERR; 
    }
    
    return SCPI_RES_OK;
}

/**
 *  @brief      SCPI-запрос режима работы дискретных выходов DOUT
 *  @details    Возвращает имя текущего состояния для одного или всех 18 каналов DOUT.
 *
 *  @param      _context - Указатель на контекст парсера SCPI
 *
 *  @return     scpi_result_t - SCPI_RES_OK при успехе, SCPI_RES_ERR при ошибке
 */
scpi_result_t cmd_output_dout_q(scpi_t *_context)
{
    int32_t channel;

    // Проверяем, передан ли номер канала (опциональный параметр)
    if (SCPI_ParamInt32(_context, &channel, FALSE)) 
    {
        if (false
            || channel < 1 
            || channel > DOUT_CH_COUNT
           ) 
        {
            SCPI_ErrorPush(_context, SCPI_ERROR_DATA_OUT_OF_RANGE);
            return SCPI_RES_ERR;
        }

        // Запрос одного канала: OUTP:DOUT? 1 -> "HIGH"
        uint32_t val = 0;
        SYSTEM_BUS_GET((DOUT1 + channel - 1), &val);

        SCPI_ResultMnemonic(_context, dout_state_names[val]);
    } 
    else 
    {
        // Если параметра не было, отдаем все 18 состояний через запятую
        for (int i = 0; i < DOUT_CH_COUNT; i++) 
        {
            uint32_t val = 0;
            SYSTEM_BUS_GET((DOUT1 + i), &val);
            SCPI_ResultMnemonic(_context, dout_state_names[val]);
        }
    }

    return SCPI_RES_OK;
}

/**
 *  @brief      SCPI-команда выбора канала мультиплексора JTAG (1..5)
 *  @details    Синхронно переключает мультиплексор JTAG на выбранный канал.
 *
 *  @param      _context - Указатель на контекст парсера SCPI
 *
 *  @return     scpi_result_t - SCPI_RES_OK при успехе, SCPI_RES_ERR при ошибке
 */
scpi_result_t cmd_mux_jtag(scpi_t *_context)
{
    int32_t ch;

    if (!SCPI_ParamInt32(_context, &ch, TRUE)) 
    {
        return SCPI_RES_ERR;
    }

    if (false
        || ch < 1 
        || ch > JTAG_MAX_CHANNELS
       ) 
    {
        SCPI_ErrorPush(_context, SCPI_ERROR_DATA_OUT_OF_RANGE); // Ошибка -222
        return SCPI_RES_ERR;
    }

    k_sem_reset(&sem_mux_done);

    SERVER_CH_SET_SYC(JTAG_MUX0, (uint32_t)ch, &sem_mux_done, &mux_response);

    if (false 
        || k_sem_take(&sem_mux_done, K_MSEC(100)) != 0
        || mux_response != 0
       ) 
    {
        SCPI_ErrorPush(_context, SCPI_ERROR_HARDWARE_ERROR);
        return SCPI_RES_ERR; 
    }

    return SCPI_RES_OK;
}

/**
 *  @brief      SCPI-запрос текущего канала мультиплексора JTAG
 *  @details    Считывает и возвращает номер текущего канала JTAG из системной шины.
 *
 *  @param      _context - Указатель на контекст парсера SCPI
 *
 *  @return     scpi_result_t - Всегда SCPI_RES_OK
 */
scpi_result_t cmd_mux_jtag_q(scpi_t *_context)
{
    uint32_t val = 0;

    SYSTEM_BUS_GET(JTAG_MUX0, &val);
    SCPI_ResultInt32(_context, val);

    return SCPI_RES_OK;
}

/**
 *  @brief      SCPI-команда выбора канала мультиплексора UART (1..4)
 *  @details    Синхронно переключает мультиплексор UART на выбранный канал.
 *
 *  @param      _context - Указатель на контекст парсера SCPI
 *
 *  @return     scpi_result_t - SCPI_RES_OK при успехе, SCPI_RES_ERR при ошибке
 */
scpi_result_t cmd_mux_uart(scpi_t *_context)
{
    int32_t ch;

    if (!SCPI_ParamInt32(_context, &ch, TRUE)) 
    {
        return SCPI_RES_ERR;
    }

    if (false
        || ch < 1 
        || ch > UART_MAX_CHANNELS
       ) 
    {
        SCPI_ErrorPush(_context, SCPI_ERROR_DATA_OUT_OF_RANGE); // Ошибка -222
        return SCPI_RES_ERR;
    }

    k_sem_reset(&sem_mux_done);

    SERVER_CH_SET_SYC(USB_FB_MUX, (uint32_t)ch, &sem_mux_done, &mux_response);

    if (false 
        || k_sem_take(&sem_mux_done, K_MSEC(100)) != 0
        || mux_response != 0
       ) 
    {
        SCPI_ErrorPush(_context, SCPI_ERROR_HARDWARE_ERROR);
        return SCPI_RES_ERR; 
    }
    
    return SCPI_RES_OK;
}

/**
 *  @brief      SCPI-запрос текущего канала мультиплексора UART
 *  @details    Считывает и возвращает номер текущего канала UART из системной шины.
 *
 *  @param      _context - Указатель на контекст парсера SCPI
 *
 *  @return     scpi_result_t - Всегда SCPI_RES_OK
 */
scpi_result_t cmd_mux_uart_q(scpi_t *_context)
{
    uint32_t val = 0;

    SYSTEM_BUS_GET(USB_FB_MUX, &val);
    SCPI_ResultInt32(_context, val);

    return SCPI_RES_OK;
}

/**
 *  @brief      SCPI-команда полного сброса всех силовых выходов и мультиплексоров
 *  @details    Переводит все каналы питания, LIN, MUX, DOUT, JTAG и UART в дефолтные состояния.
 *
 *  @param      _context - Указатель на контекст парсера SCPI
 *
 *  @return     scpi_result_t - Всегда SCPI_RES_OK
 */
scpi_result_t cmd_out_reset(scpi_t *_context)
{
    ARG_UNUSED(_context);

    for (int i = 0; i < ARRAY_SIZE(power_channel_choices) - 1; i++) 
    {        
        SERVER_CH_SET_ASYC(power_channel_choices[i].tag, (bool)false);
    }

    for (int i = 0; i < ARRAY_SIZE(lin_pd_channel_choices) - 1; i++) 
    {
        SERVER_CH_SET_ASYC(lin_pd_channel_choices[i].tag, (uint32_t)0);    
    }

    for (int i = 0; i < ARRAY_SIZE(mux_channel_choices) - 1; i++) 
    {
        SERVER_CH_SET_ASYC(mux_channel_choices[i].tag, (bool)false);    
    }

    for (int i = 0; i < DOUT_CH_COUNT; i++) 
    {    
        SERVER_CH_SET_ASYC(DOUT1 + i, (uint32_t)STATE_IN);     
    }

    SERVER_CH_SET_ASYC(JTAG_MUX0, (uint32_t)1);     
    SERVER_CH_SET_ASYC(USB_FB_MUX, (uint32_t)1);     

    return SCPI_RES_OK;
}

/***************************************************************************************************
 *                                           END OF FILE
 **************************************************************************************************/