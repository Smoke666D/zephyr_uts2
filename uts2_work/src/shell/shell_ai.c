/**
 *  @file       shell_ai.c
 *
 *  @date       2026.10.21
 *  @author     Dymov Igor
 *
 *  @brief      Команды консоли Zephyr Shell для чтения аналоговых входов и выходов
 *  @details    Предоставляет интерфейс Shell для одиночного и циклического чтения 
 *              каналов окружения (ENV), Sense, аналоговых выходов (AO) и LIN_AO.
 */

/***************************************************************************************************
 *                                          INCLUDED FILES
 **************************************************************************************************/
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#include "system_bus_model.h"
#include "shell_common.h"

/***************************************************************************************************
 *                                           DEFINITIONS
 **************************************************************************************************/
#define NUM_CHANNELS        8
#define NUM_SENSE_CHANNELS  6
#define NUM_AO_CHANNELS     18
#define NUM_LIN_AO_CHANNELS 4

/***************************************************************************************************
 *                                   PRIVATE FUNCTION PROTOTYPES
 **************************************************************************************************/
static inline void _read_all_voltages(float *_voltages);
static void        _print_voltages(const struct shell *_sh, int _channel_idx, 
                                   bool _use_names, const float *_voltages);
static int         _cmd_ai_env(const struct shell *_sh, size_t _argc, char **_argv);

static inline void _read_all_sense_voltages(float *_voltages);
static void        _print_sense_voltages(const struct shell *_sh, int _channel_idx, 
                                         const float *_voltages);
static int         _cmd_ai_sense(const struct shell *_sh, size_t _argc, char **_argv);

static inline void _read_all_ao_voltages(float *_voltages);
static void        _print_ao_voltages(const struct shell *_sh, int _channel_idx, 
                                      const float *_voltages);
static int         _cmd_ao(const struct shell *_sh, size_t _argc, char **_argv);

static inline void _read_all_lin_ao_voltages(float *_voltages);
static void        _print_lin_ao_voltages(const struct shell *_sh, int _channel_idx, 
                                          const float *_voltages);
static int         _cmd_lin_ao(const struct shell *_sh, size_t _argc, char **_argv);

/***************************************************************************************************
 *                                           PRIVATE DATA
 **************************************************************************************************/
// Массив имен каналов окружения в порядке индексов
static const char *const channel_names[NUM_CHANNELS + 1] = 
{
    "ALL",
    "ENV_P3V3",
    "ENV_P5V0",
    "ENV_VIN",
    "ENV_VDOUT1",
    "ENV_P40V",
    "ENV_USB",
    "ENV_VDOUT2",
    "ENV_VDOUT3"
};

/***************************************************************************************************
 *                                        PRIVATE FUNCTIONS
 **************************************************************************************************/

/**
 *  @brief      Чтение напряжений всех каналов окружения
 *  @details    Запрашивает из системной шины значения всех 8 каналов окружения.
 *
 *  @param      _voltages - Выходной массив для записи значений напряжений
 */
static inline void _read_all_voltages(float *_voltages)
{
    SYSTEM_BUS_GET(ENV_P3V3,   &_voltages[0]);
    SYSTEM_BUS_GET(ENV_P5V0,   &_voltages[1]);
    SYSTEM_BUS_GET(ENV_VIN,    &_voltages[2]);
    SYSTEM_BUS_GET(ENV_VDOUT1, &_voltages[3]);
    SYSTEM_BUS_GET(ENV_P40V,   &_voltages[4]);
    SYSTEM_BUS_GET(ENV_USB,    &_voltages[5]);
    SYSTEM_BUS_GET(ENV_VDOUT2, &_voltages[6]);
    SYSTEM_BUS_GET(ENV_VDOUT3, &_voltages[7]);
}

/**
 *  @brief      Вывод напряжений каналов окружения в консоль Shell
 *  @details    Выводит измерения одного или всех каналов с поддержкой
 *              отображения имен каналов или их индексов.
 *
 *  @param      _sh          - Указатель на контекст Shell
 *  @param      _channel_idx - Индекс запрашиваемого канала (0 - все)
 *  @param      _use_names   - Флаг использования текстовых имен каналов
 *  @param      _voltages    - Массив с прочитанными значениями напряжений
 */
static void _print_voltages(const struct shell *_sh, int _channel_idx, 
                            bool _use_names, const float *_voltages)
{
    if (_channel_idx == 0) 
    {
        shell_print(_sh, "--- Scan Frame ---");
        for (int i = 0; i < NUM_CHANNELS; i++) 
        {
            if (_use_names) 
            {
                shell_print(_sh, "%s: %.4f V", channel_names[i + 1], _voltages[i]);
            } 
            else 
            {
                shell_print(_sh, "%d: %.4f V", i + 1, _voltages[i]);
            }
        }
    } 
    else 
    {
        if (_use_names) 
        {
            shell_print(_sh, "%s: %.4f V", 
                        channel_names[_channel_idx], 
                        _voltages[_channel_idx - 1]);
        } 
        else 
        {
            shell_print(_sh, "%d: %.4f V", _channel_idx, _voltages[_channel_idx - 1]);
        }
    }
}

/**
 *  @brief      Обработчик команды AI_ENV в Zephyr Shell
 *  @details    Выполняет разовое чтение или запускает цикл сканирования
 *              аналоговых входов окружения с заданным периодом.
 *
 *  @param      _sh   - Указатель на контекст Shell
 *  @param      _argc - Количество аргументов команды
 *  @param      _argv - Массив аргументов команды
 *
 *  @return     int - Ноль при успехе, отрицательный код ошибки при сбое
 */
static int _cmd_ai_env(const struct shell *_sh, size_t _argc, char **_argv)
{
    if (_argc < 2) 
    {
        shell_print(_sh, "Usage: AI_ENV [channel_0-8] [name] [period_ms]");
        return -EINVAL;
    }

    int channel_idx = atoi(_argv[1]);
    if (false
        || channel_idx < 0 
        || channel_idx > NUM_CHANNELS
       ) 
    {
        shell_error(_sh, "Error: Channel index must be from 0 to %d.", NUM_CHANNELS);
        return -EINVAL;
    }

    bool use_names = false; // По умолчанию выводим номера
    int scan_time_ms = 0;

    if (_argc > 2) 
    {
        // Если второй аргумент равен "name", включаем вывод имен
        if (strcmp(_argv[2], "name") == 0) 
        {
            use_names = true;
            if (_argc > 3) 
            {
                scan_time_ms = atoi(_argv[3]);
            }
        } 
        else 
        {
            // Иначе считаем, что второй аргумент — это сразу время периода
            use_names = false;
            scan_time_ms = atoi(_argv[2]);
        }
    }

    // Проверка и округление времени сканирования до кратного 100 мс
    if (scan_time_ms > 0) 
    {
        if (scan_time_ms < 100) 
        {
            shell_error(_sh, "Error: Scan time must be at least 100 ms.");
            return -EINVAL;
        }
        scan_time_ms = ((scan_time_ms + 50) / 100) * 100;
    }

    float mock_voltages[NUM_CHANNELS];

    // Режим разового замера
    if (scan_time_ms <= 0) 
    {
        _read_all_voltages(mock_voltages);
        _print_voltages(_sh, channel_idx, use_names, mock_voltages);
        return 0;
    }

    // Режим непрерывного сканирования с заданным периодом
    shell_print(_sh, "Starting scan mode (Period: %d ms). Press Ctrl+C to stop...", scan_time_ms);

    while (true) 
    {
        _read_all_voltages(mock_voltages);
        _print_voltages(_sh, channel_idx, use_names, mock_voltages);

        int elapsed_ms = 0;
        bool exit_requested = false;

        while (elapsed_ms < scan_time_ms) 
        {
            k_msleep(20); // Спим квантами по 20 мс
            elapsed_ms += 20;

            if (shell_keypress_check(_sh)) 
            {
                exit_requested = true;
                break;
            }
        }

        if (exit_requested) 
        {
            shell_print(_sh, "\nScan stopped by user.");
            break; 
        }
    }

    return 0;
}

// Регистрация команды AI_ENV в Zephyr Shell
SHELL_CMD_ARG_REGISTER(
    AI_ENV, 
    NULL, 
    "Read environment analog inputs.\n"
    "Usage: AI_ENV [channel_0-8] [name] [period_ms]\n"
    "  0 - ALL\n"
    "  1 - ENV_P3V3,  2 - ENV_P5V0,  3 - ENV_VIN,   4 - ENV_VDOUT1\n"
    "  5 - ENV_P40V,  6 - ENV_USB,   7 - ENV_VDOUT2, 8 - ENV_VDOUT3", 
    _cmd_ai_env, 
    1, 
    3
);

/**
 *  @brief      Чтение напряжений всех каналов Sense
 *  @details    Запрашивает из системной шины значения всех 6 каналов Sense.
 *
 *  @param      _voltages - Выходной массив для записи значений напряжений
 */
static inline void _read_all_sense_voltages(float *_voltages) 
{
    SYSTEM_BUS_GET(AIN_AVsense1, &_voltages[0]);
    SYSTEM_BUS_GET(AIN_AVsense2, &_voltages[1]);
    SYSTEM_BUS_GET(AIN_AVsense3, &_voltages[2]);
    SYSTEM_BUS_GET(AIN_AVsense4, &_voltages[3]);
    SYSTEM_BUS_GET(AIN_AVsense5, &_voltages[4]);
    SYSTEM_BUS_GET(AIN_AVsense6, &_voltages[5]);
}

/**
 *  @brief      Вывод напряжений каналов Sense в консоль Shell
 *  @details    Выводит измерения одного или всех каналов Sense по их номерам.
 *
 *  @param      _sh          - Указатель на контекст Shell
 *  @param      _channel_idx - Индекс запрашиваемого канала (0 - все)
 *  @param      _voltages    - Массив с прочитанными значениями напряжений
 */
static void _print_sense_voltages(const struct shell *_sh, int _channel_idx, 
                                  const float *_voltages) 
{
    if (_channel_idx == 0) 
    {
        shell_print(_sh, "--- Sense Scan Frame ---");
        for (int i = 0; i < NUM_SENSE_CHANNELS; i++) 
        {
            shell_print(_sh, "%d: %.4f V", i + 1, _voltages[i]);
        }
    } 
    else 
    {
        shell_print(_sh, "%d: %.4f V", _channel_idx, _voltages[_channel_idx - 1]);
    }
}

/**
 *  @brief      Обработчик команды AI_Sense в Zephyr Shell
 *  @details    Выполняет разовое чтение или запускает цикл сканирования
 *              аналоговых входов Sense с заданным периодом.
 *
 *  @param      _sh   - Указатель на контекст Shell
 *  @param      _argc - Количество аргументов команды
 *  @param      _argv - Массив аргументов команды
 *
 *  @return     int - Ноль при успехе, отрицательный код ошибки при сбое
 */
static int _cmd_ai_sense(const struct shell *_sh, size_t _argc, char **_argv)
{
    if (_argc < 2) 
    {
        shell_print(_sh, "Usage: AI_Sense [channel_0-6] [period_ms]");
        return -EINVAL;
    }

    int channel_idx = atoi(_argv[1]);
    if (false
        || channel_idx < 0 
        || channel_idx > NUM_SENSE_CHANNELS
       ) 
    {
        shell_error(_sh, "Error: Channel index must be from 0 to %d.", NUM_SENSE_CHANNELS);
        return -EINVAL;
    }

    int scan_time_ms = 0;
    if (_argc > 2) 
    {
        scan_time_ms = atoi(_argv[2]);
    }

    // Проверка и округление времени сканирования до кратного 100 мс
    if (scan_time_ms > 0) 
    {
        if (scan_time_ms < 100) 
        {
            shell_error(_sh, "Error: Scan time must be at least 100 ms.");
            return -EINVAL;
        }
        scan_time_ms = ((scan_time_ms + 50) / 100) * 100;
    }

    float mock_voltages[NUM_SENSE_CHANNELS];

    // Режим разового замера
    if (scan_time_ms <= 0) 
    {
        _read_all_sense_voltages(mock_voltages);
        _print_sense_voltages(_sh, channel_idx, mock_voltages);
        return 0;
    }

    // Режим непрерывного сканирования с заданным периодом
    shell_print(_sh, "Starting Sense scan mode (Period: %d ms). Press Ctrl+C to stop...", 
                scan_time_ms);

    while (true) 
    {
        _read_all_sense_voltages(mock_voltages);
        _print_sense_voltages(_sh, channel_idx, mock_voltages);

        int elapsed_ms = 0;
        bool exit_requested = false;

        while (elapsed_ms < scan_time_ms) 
        {
            k_msleep(20); // Спим квантами по 20 мс
            elapsed_ms += 20;

            if (shell_keypress_check(_sh)) 
            {
                exit_requested = true;
                break;
            }
        }

        if (exit_requested) 
        {
            shell_print(_sh, "\nScan stopped by user.");
            break; 
        }
    }

    return 0;
}

// Регистрация команды AI_Sense в Zephyr Shell
SHELL_CMD_ARG_REGISTER(
    AI_Sense, 
    NULL, 
    "Read sense analog inputs.\n"
    "Usage: AI_Sense [channel_0-6] [period_ms]\n"
    "  0 - ALL\n"
    "  1-6 - AIN_AVsense1 to AIN_AVsense6", 
    _cmd_ai_sense, 
    1, 
    2
);

/**
 *  @brief      Чтение напряжений всех AO каналов
 *  @details    Запрашивает из системной шины значения всех 18 аналоговых выходов.
 *
 *  @param      _voltages - Выходной массив для записи значений напряжений
 */
static inline void _read_all_ao_voltages(float *_voltages) 
{
    SYSTEM_BUS_GET(AIN_AO1,  &_voltages[0]);
    SYSTEM_BUS_GET(AIN_AO2,  &_voltages[1]);
    SYSTEM_BUS_GET(AIN_AO3,  &_voltages[2]);
    SYSTEM_BUS_GET(AIN_AO4,  &_voltages[3]);
    SYSTEM_BUS_GET(AIN_AO5,  &_voltages[4]);
    SYSTEM_BUS_GET(AIN_AO6,  &_voltages[5]);
    SYSTEM_BUS_GET(AIN_AO7,  &_voltages[6]);
    SYSTEM_BUS_GET(AIN_AO8,  &_voltages[7]);
    SYSTEM_BUS_GET(AIN_AO9,  &_voltages[8]);
    SYSTEM_BUS_GET(AIN_AO10, &_voltages[9]);
    SYSTEM_BUS_GET(AIN_AO11, &_voltages[10]);
    SYSTEM_BUS_GET(AIN_AO12, &_voltages[11]);
    SYSTEM_BUS_GET(AIN_AO13, &_voltages[12]);
    SYSTEM_BUS_GET(AIN_AO14, &_voltages[13]);
    SYSTEM_BUS_GET(AIN_AO15, &_voltages[14]);
    SYSTEM_BUS_GET(AIN_AO16, &_voltages[15]);
    SYSTEM_BUS_GET(AIN_AO17, &_voltages[16]);
    SYSTEM_BUS_GET(AIN_AO18, &_voltages[17]);
}

/**
 *  @brief      Вывод напряжений AO каналов в консоль Shell
 *  @details    Выводит измерения одного или всех аналоговых выходов по их номерам.
 *
 *  @param      _sh          - Указатель на контекст Shell
 *  @param      _channel_idx - Индекс запрашиваемого канала (0 - все)
 *  @param      _voltages    - Массив с прочитанными значениями напряжений
 */
static void _print_ao_voltages(const struct shell *_sh, int _channel_idx, 
                               const float *_voltages) 
{
    if (_channel_idx == 0) 
    {
        shell_print(_sh, "--- AO Scan Frame ---");
        for (int i = 0; i < NUM_AO_CHANNELS; i++) 
        {
            shell_print(_sh, "%d: %.4f V", i + 1, _voltages[i]);
        }
    } 
    else 
    {
        shell_print(_sh, "%d: %.4f V", _channel_idx, _voltages[_channel_idx - 1]);
    }
}

/**
 *  @brief      Обработчик команды AO в Zephyr Shell
 *  @details    Выполняет разовое чтение или запускает цикл сканирования
 *              аналоговых выходов AO с заданным периодом.
 *
 *  @param      _sh   - Указатель на контекст Shell
 *  @param      _argc - Количество аргументов команды
 *  @param      _argv - Массив аргументов команды
 *
 *  @return     int - Ноль при успехе, отрицательный код ошибки при сбое
 */
static int _cmd_ao(const struct shell *_sh, size_t _argc, char **_argv)
{
    if (_argc < 2) 
    {
        shell_print(_sh, "Usage: AO [channel_0-18] [period_ms]");
        return -EINVAL;
    }

    int channel_idx = atoi(_argv[1]);
    if (false
        || channel_idx < 0 
        || channel_idx > NUM_AO_CHANNELS
       ) 
    {
        shell_error(_sh, "Error: Channel index must be from 0 to %d.", NUM_AO_CHANNELS);
        return -EINVAL;
    }

    int scan_time_ms = 0;
    if (_argc > 2) 
    {
        scan_time_ms = atoi(_argv[2]);
    }

    // Проверка и округление времени сканирования до кратного 100 мс
    if (scan_time_ms > 0) 
    {
        if (scan_time_ms < 100) 
        {
            shell_error(_sh, "Error: Scan time must be at least 100 ms.");
            return -EINVAL;
        }
        scan_time_ms = ((scan_time_ms + 50) / 100) * 100;
    }

    float mock_voltages[NUM_AO_CHANNELS];

    // Режим разового замера
    if (scan_time_ms <= 0) 
    {
        _read_all_ao_voltages(mock_voltages);
        _print_ao_voltages(_sh, channel_idx, mock_voltages);
        return 0;
    }

    // Режим непрерывного сканирования с заданным периодом
    shell_print(_sh, "Starting AO scan mode (Period: %d ms). Press Ctrl+C to stop...", scan_time_ms);

    while (true) 
    {
        _read_all_ao_voltages(mock_voltages);
        _print_ao_voltages(_sh, channel_idx, mock_voltages);

        int elapsed_ms = 0;
        bool exit_requested = false;

        while (elapsed_ms < scan_time_ms) 
        {
            k_msleep(20); // Спим квантами по 20 мс
            elapsed_ms += 20;

            if (shell_keypress_check(_sh)) 
            {
                exit_requested = true;
                break;
            }
        }

        if (exit_requested) 
        {
            shell_print(_sh, "\nScan stopped by user.");
            break; 
        }
    }

    return 0;
}

// Регистрация команды AO в Zephyr Shell
SHELL_CMD_ARG_REGISTER(
    AO, 
    NULL, 
    "Read analog outputs.\n"
    "Usage: AO [channel_0-18] [period_ms]\n"
    "  0 - ALL\n"
    "  1-18 - AO1 to AO18", 
    _cmd_ao, 
    1, 
    2
);

/**
 *  @brief      Чтение напряжений всех LIN_AO каналов
 *  @details    Запрашивает из системной шины значения всех 4 аналоговых выходов LIN.
 *
 *  @param      _voltages - Выходной массив для записи значений напряжений
 */
static inline void _read_all_lin_ao_voltages(float *_voltages) 
{
    SYSTEM_BUS_GET(LIN_AO1, &_voltages[0]);
    SYSTEM_BUS_GET(LIN_AO2, &_voltages[1]);
    SYSTEM_BUS_GET(LIN_AO3, &_voltages[2]);
    SYSTEM_BUS_GET(LIN_AO4, &_voltages[3]);
}

/**
 *  @brief      Вывод напряжений LIN_AO каналов в консоль Shell
 *  @details    Выводит измерения одного или всех аналоговых выходов LIN по их номерам.
 *
 *  @param      _sh          - Указатель на контекст Shell
 *  @param      _channel_idx - Индекс запрашиваемого канала (0 - все)
 *  @param      _voltages    - Массив с прочитанными значениями напряжений
 */
static void _print_lin_ao_voltages(const struct shell *_sh, int _channel_idx, 
                                      const float *_voltages) 
{
    if (_channel_idx == 0) 
    {
        shell_print(_sh, "--- LIN_AO Scan Frame ---");
        for (int i = 0; i < NUM_LIN_AO_CHANNELS; i++) 
        {
            shell_print(_sh, "%d: %.4f V", i + 1, _voltages[i]);
        }
    } 
    else 
    {
        shell_print(_sh, "%d: %.4f V", _channel_idx, _voltages[_channel_idx - 1]);
    }
}

/**
 *  @brief      Обработчик команды LIN_AO в Zephyr Shell
 *  @details    Выполняет разовое чтение или запускает цикл сканирования
 *              аналоговых выходов LIN с заданным периодом.
 *
 *  @param      _sh   - Указатель на контекст Shell
 *  @param      _argc - Количество аргументов команды
 *  @param      _argv - Массив аргументов команды
 *
 *  @return     int - Ноль при успехе, отрицательный код ошибки при сбое
 */
static int _cmd_lin_ao(const struct shell *_sh, size_t _argc, char **_argv)
{
    if (_argc < 2) 
    {
        shell_print(_sh, "Usage: LIN_AO [channel_0-4] [period_ms]");
        return -EINVAL;
    }

    int channel_idx = atoi(_argv[1]);
    if (false
        || channel_idx < 0 
        || channel_idx > NUM_LIN_AO_CHANNELS
       ) 
    {
        shell_error(_sh, "Error: Channel index must be from 0 to %d.", NUM_LIN_AO_CHANNELS);
        return -EINVAL;
    }

    int scan_time_ms = 0;
    if (_argc > 2) 
    {
        scan_time_ms = atoi(_argv[2]);
    }

    // Проверка и округление времени сканирования до кратного 100 мс
    if (scan_time_ms > 0) 
    {
        if (scan_time_ms < 100) 
        {
            shell_error(_sh, "Error: Scan time must be at least 100 ms.");
            return -EINVAL;
        }
        scan_time_ms = ((scan_time_ms + 50) / 100) * 100;
    }

    float mock_voltages[NUM_LIN_AO_CHANNELS];

    // Режим разового замера
    if (scan_time_ms <= 0) 
    {
        _read_all_lin_ao_voltages(mock_voltages);
        _print_lin_ao_voltages(_sh, channel_idx, mock_voltages);
        return 0;
    }

    // Режим непрерывного сканирования с заданным периодом
    shell_print(_sh, "Starting LIN_AO scan mode (Period: %d ms). Press Ctrl+C to stop...", 
                scan_time_ms);

    while (true) 
    {
        _read_all_lin_ao_voltages(mock_voltages);
        _print_lin_ao_voltages(_sh, channel_idx, mock_voltages);

        int elapsed_ms = 0;
        bool exit_requested = false;

        while (elapsed_ms < scan_time_ms) 
        {
            k_msleep(20); // Спим квантами по 20 мс
            elapsed_ms += 20;

            if (shell_keypress_check(_sh)) 
            {
                exit_requested = true;
                break;
            }
        }

        if (exit_requested) 
        {
            shell_print(_sh, "\nScan stopped by user.");
            break; 
        }
    }

    return 0;
}

// Регистрация команды LIN_AO в Zephyr Shell
SHELL_CMD_ARG_REGISTER(
    LIN_AO, 
    NULL, 
    "Read LIN analog outputs.\n"
    "Usage: LIN_AO [channel_0-4] [period_ms]\n"
    "  0 - ALL\n"
    "  1-4 - LIN_AO1 to LIN_AO4", 
    _cmd_lin_ao, 
    1, 
    2
);

/***************************************************************************************************
 *                                           END OF FILE
 **************************************************************************************************/