/**
 *  @file       shell_ai.c
 *  @date       2026.10.21
 *  @author     Dymov Igor
 *  @brief      Оптимизированные команды консоли Zephyr Shell для чтения аналоговых входов/выходов
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#include "system_bus_model.h"
#include "shell_common.h"

/***************************************************************************************************
 *                                           DEFINITIONS & TABLES
 **************************************************************************************************/
#define NUM_ENV_CH          8
#define NUM_SENSE_CH        6
#define NUM_AO_CH           18
#define NUM_LIN_AO_CH       4

// Имена для ENV каналов (индекс 0 зарезервирован под ALL)
static const char *const env_names[NUM_ENV_CH + 1] = {
    "ALL", "ENV_P3V3", "ENV_P5V0", "ENV_VIN", "ENV_VDOUT1",
    "ENV_P40V", "ENV_USB", "ENV_VDOUT2", "ENV_VDOUT3"
};

/***************************************************************************************************
 *                                   PRIVATE FUNCTION PROTOTYPES
 **************************************************************************************************/
static void _print_channels(const struct shell *_sh, const char *_prefix, 
                            const char *const _names[], int _ch_idx, 
                            int _count, const float *_voltages);
static int  _cmd_generic_ai(const struct shell *_sh, size_t _argc, char **_argv, 
                            int _max_ch, const char *_prefix, const char *const _names[], 
                            void (*_read_fn)(float *));

/***************************************************************************************************
 *                                        READ FUNCTIONS
 **************************************************************************************************/
static inline void _read_env(float *v) 
{
    SYSTEM_BUS_GET(ENV_P3V3,   &v[0]); 
    SYSTEM_BUS_GET(ENV_P5V0,   &v[1]);
    SYSTEM_BUS_GET(ENV_VIN,    &v[2]); 
    SYSTEM_BUS_GET(ENV_VDOUT1, &v[3]);
    SYSTEM_BUS_GET(ENV_P40V,   &v[4]); 
    SYSTEM_BUS_GET(ENV_USB,    &v[5]);
    SYSTEM_BUS_GET(ENV_VDOUT2, &v[6]); 
    SYSTEM_BUS_GET(ENV_VDOUT3, &v[7]);
}

static inline void _read_sense(float *v) 
{
    SYSTEM_BUS_GET(AIN_AVsense1, &v[0]);
    SYSTEM_BUS_GET(AIN_AVsense2, &v[1]);
    SYSTEM_BUS_GET(AIN_AVsense3, &v[2]); 
    SYSTEM_BUS_GET(AIN_AVsense4, &v[3]);
    SYSTEM_BUS_GET(AIN_AVsense5, &v[4]); 
    SYSTEM_BUS_GET(AIN_AVsense6, &v[5]);
}

static inline void _read_ao(float *v) 
{
    SYSTEM_BUS_GET(AIN_AO1,  &v[0]);  
    SYSTEM_BUS_GET(AIN_AO2,  &v[1]);
    SYSTEM_BUS_GET(AIN_AO3,  &v[2]);  
    SYSTEM_BUS_GET(AIN_AO4,  &v[3]);
    SYSTEM_BUS_GET(AIN_AO5,  &v[4]);
    SYSTEM_BUS_GET(AIN_AO6,  &v[5]);
    SYSTEM_BUS_GET(AIN_AO7,  &v[6]);  
    SYSTEM_BUS_GET(AIN_AO8,  &v[7]);
    SYSTEM_BUS_GET(AIN_AO9,  &v[8]);  
    SYSTEM_BUS_GET(AIN_AO10, &v[9]);
    SYSTEM_BUS_GET(AIN_AO11, &v[10]); 
    SYSTEM_BUS_GET(AIN_AO12, &v[11]);
    SYSTEM_BUS_GET(AIN_AO13, &v[12]); 
    SYSTEM_BUS_GET(AIN_AO14, &v[13]);
    SYSTEM_BUS_GET(AIN_AO15, &v[14]); 
    SYSTEM_BUS_GET(AIN_AO16, &v[15]);
    SYSTEM_BUS_GET(AIN_AO17, &v[16]); 
    SYSTEM_BUS_GET(AIN_AO18, &v[17]);
}

static inline void _read_lin_ao(float *v) 
{
    SYSTEM_BUS_GET(LIN_AO1, &v[0]); 
    SYSTEM_BUS_GET(LIN_AO2, &v[1]);
    SYSTEM_BUS_GET(LIN_AO3, &v[2]); 
    SYSTEM_BUS_GET(LIN_AO4, &v[3]);
}

/***************************************************************************************************
 *                                     GENERIC PRINT & CMD LOGIC
 **************************************************************************************************/

/**
 *  @brief      Универсальный вывод каналов (0 - в одну строку с именами, N - конкретный канал)
 */
static void _print_channels(const struct shell *_sh, const char *_prefix, 
                            const char *const _names[], int _ch_idx, 
                            int _count, const float *_voltages)
{
    if (_ch_idx == 0) 
    {
        char line_buf[512] = {0};
        int offset = 0;

        for (int i = 0; i < _count; i++) 
        {
            const char *label;
            if (_names != NULL) {
                label = _names[i + 1]; // Для ENV (берем из таблицы имен)
            } else {
                static char dyn_label[16];
                snprintf(dyn_label, sizeof(dyn_label), "%s%d", _prefix, i + 1);
                label = dyn_label;
            }

            int written = snprintf(line_buf + offset, sizeof(line_buf) - offset, 
                                   "%s: %.4f V   ", label, _voltages[i]);
            if (written > 0) { offset += written; }
        }
        shell_print(_sh, "%s", line_buf);
    } 
    else 
    {
        const char *label;
        if (_names != NULL) {
            label = _names[_ch_idx];
        } else {
            static char dyn_label[16];
            snprintf(dyn_label, sizeof(dyn_label), "%s%d", _prefix, _ch_idx);
            label = dyn_label;
        }
        shell_print(_sh, "%s: %.4f V", label, _voltages[_ch_idx - 1]);
    }
}

/**
 *  @brief      Универсальный обработчик команд
 */
static int _cmd_generic_ai(const struct shell *_sh, size_t _argc, char **_argv, 
                            int _max_ch, const char *_prefix, const char *const _names[], 
                            void (*_read_fn)(float *))
{
    if (_argc < 2) 
    {
        shell_print(_sh, "Usage: %s [channel_0-%d]", _argv[0], _max_ch);
        return -EINVAL;
    }

    int channel_idx = atoi(_argv[1]);
    if (channel_idx < 0 || channel_idx > _max_ch) 
    {
        shell_error(_sh, "Error: Channel index must be from 0 to %d.", _max_ch);
        return -EINVAL;
    }

    float voltages[_max_ch];
    _read_fn(voltages);
    _print_channels(_sh, _prefix, _names, channel_idx, _max_ch, voltages);

    return 0;
}

/***************************************************************************************************
 *                                      SHELL COMMAND WRAPPERS
 **************************************************************************************************/
static int _cmd_ai_env(const struct shell *_sh, size_t _argc, char **_argv) {
    return _cmd_generic_ai(_sh, _argc, _argv, NUM_ENV_CH, "ENV_", env_names, _read_env);
}

static int _cmd_ai_sense(const struct shell *_sh, size_t _argc, char **_argv) {
    return _cmd_generic_ai(_sh, _argc, _argv, NUM_SENSE_CH, "AVsense", NULL, _read_sense);
}

static int _cmd_ao(const struct shell *_sh, size_t _argc, char **_argv) {
    return _cmd_generic_ai(_sh, _argc, _argv, NUM_AO_CH, "AO", NULL, _read_ao);
}

static int _cmd_lin_ao(const struct shell *_sh, size_t _argc, char **_argv) {
    return _cmd_generic_ai(_sh, _argc, _argv, NUM_LIN_AO_CH, "LIN_AO", NULL, _read_lin_ao);
}

/***************************************************************************************************
 *                                           REGISTRATION
 **************************************************************************************************/
SHELL_CMD_ARG_REGISTER(AI_ENV,   NULL, "Read environment inputs. Usage: AI_ENV [0-%d]", _cmd_ai_env, 1, 1);
SHELL_CMD_ARG_REGISTER(AI_Sense, NULL, "Read sense inputs. Usage: AI_Sense [0-%d]", _cmd_ai_sense, 1, 1);
SHELL_CMD_ARG_REGISTER(AO,       NULL, "Read analog outputs. Usage: AO [0-%d]", _cmd_ao, 1, 1);
SHELL_CMD_ARG_REGISTER(LIN_AO,   NULL, "Read LIN analog outputs. Usage: LIN_AO [0-%d]", _cmd_lin_ao, 1, 1);