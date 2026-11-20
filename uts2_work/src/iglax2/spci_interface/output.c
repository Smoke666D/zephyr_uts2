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


/***************************************************************************************************
 *                                          PRIVATE TYPES
 **************************************************************************************************/

/***************************************************************************************************
 *                                   PRIVATE FUNCTION PROTOTYPES
 **************************************************************************************************/

/***************************************************************************************************
 *                                           PRIVATE DATA
 **************************************************************************************************/
static const scpi_choice_def_t out_channel_choices[] = 
{
    {"OD1", OD1},
    {"OD2", OD2},
    {"OD3", OD3},
    {"OD4", OD4},
    {"OD5", OD5},
    {"OD6", OD6},
    {"OD7", OD7},
    {"OD8", OD8},
    {"OD9", OD9},
    {"OD10", OD10},    
    {"PU_ON3", IN3_CONFIG},
    {"PU_ON4", IN4_CONFIG},
    {"OUT_P", OUT_P},    
    SCPI_CHOICE_LIST_END
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
    if (!SCPI_ParamChoice(_context, out_channel_choices, &channel, TRUE)) 
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

    if (SCPI_ParamChoice(_context, out_channel_choices, &channel, FALSE)) 
    {    
        SYSTEM_BUS_GET(channel, &val);
        SCPI_ResultBool(_context, val);   
    }
    else 
    {
        for (int i = 0; i < ARRAY_SIZE(out_channel_choices) - 1; i++) 
        {        
            SYSTEM_BUS_GET(out_channel_choices[i].tag, &val);
            SCPI_ResultBool(_context, val);
        }
    }    
    return SCPI_RES_OK;
}


