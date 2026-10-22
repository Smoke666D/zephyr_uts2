/**
 *  @file       shell_temp.c
 *  @headerfile shell_temp.h
 *
 *  @date       2026.10.22
 *  @author     Dymov Igor
 *
 *  @brief      Оптимизированные команды Zephyr Shell для датчиков температуры и освещенности
 *  @details    Использует обобщенный обработчик с таблицами описателей параметров 
 *              для исключения дублирования кода и минимизации объема Flash-памяти.
 */

/***************************************************************************************************
 *                                          INCLUDED FILES
 **************************************************************************************************/
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#include "system_bus_model.h"
#include "shell_common.h"

/***************************************************************************************************
 *                                           DEFINITIONS
 **************************************************************************************************/

/***************************************************************************************************
 *                                          PRIVATE TYPES
 **************************************************************************************************/

/**
 *  @brief Описатель группы парных датчиков для команды Shell
 */
typedef struct sensor_cmd_desc
{
    SYSTEM_BUS_ID    id1;    ///< ID первого датчика на системной шине
    SYSTEM_BUS_ID    id2;    ///< ID второго датчика на системной шине
    const char *label1; ///< Текстовая метка первого датчика
    const char *label2; ///< Текстовая метка второго датчика
    const char *unit;   ///< Единица измерения (например, "°C" или "lx")
} sensor_cmd_desc_t;

/***************************************************************************************************
 *                                   PRIVATE FUNCTION PROTOTYPES
 **************************************************************************************************/
static int _cmd_sensor_generic(const struct shell *_sh, 
                               size_t _argc, 
                               char **_argv, 
                               const sensor_cmd_desc_t *_desc);
static int _cmd_temp(const struct shell *_sh, size_t _argc, char **_argv);
static int _cmd_lux(const struct shell *_sh, size_t _argc, char **_argv);

/***************************************************************************************************
 *                                           PRIVATE DATA
 **************************************************************************************************/

// Описатель для температурных датчиков (TEMP)
static const sensor_cmd_desc_t temp_desc = 
{
    .id1    = I2C1_TEMP1,
    .id2    = I2C1_TEMP2,
    .label1 = "TEMP1 (Материнский узел)",
    .label2 = "TEMP2 (Интерфейсный узел)",
    .unit   = "°C"
};

// Описатель для датчиков освещенности (LUX)
static const sensor_cmd_desc_t lux_desc = 
{
    .id1    = I2C1_LUX1,
    .id2    = I2C1_LUX2,
    .label1 = "LUX1 (Датчик DA22)",
    .label2 = "LUX2 (Датчик DA23)",
    .unit   = "lx"
};

/***************************************************************************************************
 *                                        PRIVATE FUNCTIONS
 **************************************************************************************************/

/**
 *  @brief      Обобщенный обработчик команд чтения датчиков
 *  @details    Выполняет разбор аргументов Shell, запрашивает значения с шины 
 *              и выводит отформатированный результат на основе структуры описателя.
 *
 *  @param      _sh   - Указатель на контекст Shell
 *  @param      _argc - Количество аргументов команды
 *  @param      _argv - Массив аргументов команды
 *  @param      _desc - Указатель на структуру описателя группы датчиков
 *
 *  @return     int - Ноль при успехе, отрицательный код ошибки при сбое
 */
static int _cmd_sensor_generic(const struct shell *_sh, 
                               size_t _argc, 
                               char **_argv, 
                               const sensor_cmd_desc_t *_desc)
{
    int sensor_idx = 0;

    if (_argc > 1) 
    {
        sensor_idx = atoi(_argv[1]);
        if (false
            || sensor_idx < 0 
            || sensor_idx > 2
           ) 
        {
            shell_error(_sh, "Error: Invalid sensor index. Use 0 (all), 1, or 2.");
            return -EINVAL;
        }
    }

    float val1 = 0.0f;
    float val2 = 0.0f;

    SYSTEM_BUS_GET(_desc->id1, &val1);
    SYSTEM_BUS_GET(_desc->id2, &val2);

    if (sensor_idx == 0) 
    {        
        shell_print(
            _sh, 
            "%s: %.2f %s   |   %s: %.2f %s", 
            _desc->label1, val1, _desc->unit, 
            _desc->label2, val2, _desc->unit
        );
    } 
    else if (sensor_idx == 1) 
    {     
        shell_print(_sh, "%s: %.2f %s", _desc->label1, val1, _desc->unit);
    } 
    else 
    {     
        shell_print(_sh, "%s: %.2f %s", _desc->label2, val2, _desc->unit);
    }

    return 0;
}

/**
 *  @brief      Обработчик команды TEMP в Zephyr Shell
 *  @details    Передает управление обобщенному обработчику с описателем temp_desc.
 *
 *  @param      _sh   - Указатель на контекст Shell
 *  @param      _argc - Количество аргументов команды
 *  @param      _argv - Массив аргументов команды
 *
 *  @return     int - Результат выполнения обобщенного обработчика
 */
static int _cmd_temp(const struct shell *_sh, size_t _argc, char **_argv)
{
    return _cmd_sensor_generic(_sh, _argc, _argv, &temp_desc);
}

/**
 *  @brief      Обработчик команды LUX в Zephyr Shell
 *  @details    Передает управление обобщенному обработчику с описателем lux_desc.
 *
 *  @param      _sh   - Указатель на контекст Shell
 *  @param      _argc - Количество аргументов команды
 *  @param      _argv - Массив аргументов команды
 *
 *  @return     int - Результат выполнения обобщенного обработчика
 */
static int _cmd_lux(const struct shell *_sh, size_t _argc, char **_argv)
{
    return _cmd_sensor_generic(_sh, _argc, _argv, &lux_desc);
}

/***************************************************************************************************
 *                                        PUBLIC FUNCTIONS
 **************************************************************************************************/

// Регистрация команды TEMP в Zephyr Shell
SHELL_CMD_ARG_REGISTER(
    TEMP, 
    NULL, 
    "Read temperature sensors.\n"
    "Usage: TEMP [sensor_0-2]\n"
    "  0 - Read all sensors in one line (default)\n"
    "  1 - TEMP1 (Материнский узел)\n"
    "  2 - TEMP2 (Интерфейсный узел)", 
    _cmd_temp, 
    1, 
    1
);

// Регистрация команды LUX в Zephyr Shell
SHELL_CMD_ARG_REGISTER(
    LUX, 
    NULL, 
    "Read light intensity sensors (Lux).\n"
    "Usage: LUX [sensor_0-2]\n"
    "  0 - Read all sensors in one line (default)\n"
    "  1 - LUX1 (Датчик DA22)\n"
    "  2 - LUX2 (Датчик DA23)", 
    _cmd_lux, 
    1, 
    1
);

/***************************************************************************************************
 *                                           END OF FILE
 **************************************************************************************************/