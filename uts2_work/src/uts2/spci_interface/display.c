/**
 *  @file       display.c
 *  @headerfile display.h
 *
 *  @date       2026.11.10
 *  @author     Dymov Igor
 *
 *  @brief      Обработчики SCPI-команд управления индикацией дисплея
 *  @details    Реализует команды установки состояния светодиодов (DISPlay:LED:STATe),
 *              запроса текущего состояния (DISPlay:LED:STATe?) и сброса всех индикаторов.
 */

/***************************************************************************************************
 *                                          INCLUDED FILES
 **************************************************************************************************/
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <zephyr/kernel.h>

#include "display.h"
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
// Словарь доступных идентификаторов для SCPI
static const scpi_choice_def_t led_targets[] = 
{
    {"RED",   LED1},
    {"GREen", LED2},
    {"BLUe",  LED3},
    SCPI_CHOICE_LIST_END
};

/***************************************************************************************************
 *                                        PUBLIC FUNCTIONS
 **************************************************************************************************/

/**
 *  @brief      SCPI-команда установки состояния индикатора
 *  @details    Устанавливает состояние выбранного светодиода (RED, GREen, BLUe)
 *              через асинхронную отправку в системную шину.
 *
 *  @param      _context - Указатель на контекст парсера SCPI
 *
 *  @return     scpi_result_t - SCPI_RES_OK при успехе, SCPI_RES_ERR при ошибке параметров
 */
scpi_result_t cmd_disp_led_state(scpi_t *_context)
{
    int32_t target;
    scpi_bool_t state;

    // Чтение 1-го параметра: имя целевого индикатора
    if (!SCPI_ParamChoice(_context, led_targets, &target, TRUE)) 
    {
        return SCPI_RES_ERR;
    }

    // Чтение 2-го параметра: булево состояние (0/1/OFF/ON)
    if (!SCPI_ParamBool(_context, &state, TRUE)) 
    {
        return SCPI_RES_ERR;
    }

    SERVER_CH_SET_ASYC(target, (bool)state);

    return SCPI_RES_OK;
}

/**
 *  @brief      SCPI-запрос текущего состояния индикатора
 *  @details    Считывает текущее состояние указанного светодиода из системной шины
 *              и возвращает результат клиенту SCPI.
 *
 *  @param      _context - Указатель на контекст парсера SCPI
 *
 *  @return     scpi_result_t - SCPI_RES_OK при успехе, SCPI_RES_ERR при ошибке параметров
 */
scpi_result_t cmd_disp_led_state_q(scpi_t *_context)
{
    int32_t target;
    bool val = false;

    // Чтение параметра запроса: имя целевого индикатора
    if (!SCPI_ParamChoice(_context, led_targets, &target, TRUE)) 
    {
        return SCPI_RES_ERR;
    }

    SYSTEM_BUS_GET(target, &val);

    // Возврат текущего состояния (1 или 0)
    SCPI_ResultBool(_context, val);
    return SCPI_RES_OK;
}

/**
 *  @brief      SCPI-команда сброса состояния всех индикаторов
 *  @details    Асинхронно отключает все светодиоды дисплея (LED1, LED2, LED3).
 *
 *  @param      _context - Указатель на контекст парсера SCPI
 *
 *  @return     scpi_result_t - Всегда SCPI_RES_OK
 */
scpi_result_t cmd_led_reset(scpi_t *_context)
{    
    ARG_UNUSED(_context);

    SERVER_CH_SET_ASYC(LED1, (bool)false);
    SERVER_CH_SET_ASYC(LED2, (bool)false);
    SERVER_CH_SET_ASYC(LED3, (bool)false);

    return SCPI_RES_OK;
}

/***************************************************************************************************
 *                                           END OF FILE
 **************************************************************************************************/