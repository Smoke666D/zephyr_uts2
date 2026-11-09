/**
 *  @file       shell_dac.c
 *  @headerfile shell_dac.h
 *
 *  @date       2026.11.02
 *  @author     Dymov Igor
 *
 *  @brief      Команды Zephyr Shell для управления ЦАП и регулируемыми резисторами
 *  @details    Предоставляет интерфейс командной строки для установки и чтения
 *              выходного напряжения ЦАП (DAC) и сопротивления резисторов (ADJ_RES).
 */

/***************************************************************************************************
 *                                          INCLUDED FILES
 **************************************************************************************************/
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/zbus/zbus.h>

#include "system_bus_model.h"

/***************************************************************************************************
 *                                   PRIVATE FUNCTION PROTOTYPES
 **************************************************************************************************/
static int _parse_channel(const struct shell *_sh, const char *_str, 
                          long *_channel_out, long _max_val, const char *_prefix);
static int _parse_float_range(const struct shell *_sh, const char *_str, 
                              float *_val_out, float _min, float _max, const char *_unit, const char *_prefix);
static int _sync_bus_set(SYSTEM_BUS_ID _id, float _value);

static int _cmd_dac_set_action(const struct shell *_sh, size_t _argc, char **_argv);
static int _cmd_dac_get_action(const struct shell *_sh, size_t _argc, char **_argv);

static int _cmd_adj_res_set_action(const struct shell *_sh, size_t _argc, char **_argv);
static int _cmd_adj_res_get_action(const struct shell *_sh, size_t _argc, char **_argv);

/***************************************************************************************************
 *                                           PRIVATE DATA
 **************************************************************************************************/
static K_SEM_DEFINE(dac_done, 0, 1);
static uint8_t dac_response = 0;

static const uint32_t adj_res_name[] = 
{
    ADJ_RES1,
    ADJ_RES2  
};

/***************************************************************************************************
 *                                        PRIVATE FUNCTIONS
 **************************************************************************************************/

static int _parse_channel(const struct shell *_sh, const char *_str, 
                          long *_channel_out, long _max_val, const char *_prefix)
{
    char *endptr;
    long val = strtol(_str, &endptr, 10);
    
    if (*endptr != '\0' || val < 1 || val > _max_val) 
    {
        shell_error(_sh, "%s ERROR: Invalid channel: %s (Must be 1 to %ld)", _prefix, _str, _max_val);
        return -EINVAL;
    }

    *_channel_out = val;
    return 0;
}

static int _parse_float_range(const struct shell *_sh, const char *_str, 
                              float *_val_out, float _min, float _max, const char *_unit, const char *_prefix)
{
    char *endptr;
    float val = strtof(_str, &endptr);

    if (*endptr != '\0' || val < 0 || val > 4095) 
    {
        shell_error(_sh, "%s ERROR: Invalid value: %s (Must be between %.1f and %.1f %s)", 
                    _prefix, _str, (double)_min, (double)_max, _unit);
        return -EINVAL;
    }

    *_val_out = val;
    return 0;
}

static int _sync_bus_set(SYSTEM_BUS_ID _id, float _value)
{
    k_sem_reset(&dac_done);
    
    int ret = SERVER_CH_SET_SYC(_id, _value, &dac_done, &dac_response);
    if (ret != 0) 
    {
        return -EACCES;
    }

    if (k_sem_take(&dac_done, K_MSEC(100)) != 0) 
    {
        return -ETIMEDOUT;
    }

    return dac_response;
}

static int _cmd_dac_set_action(const struct shell *_sh, size_t _argc, char **_argv)
{
    if (_argc != 2) 
    {
        shell_error(_sh, "DAC ERROR: Usage: DAC set <0.0-30.0>");
        return -EINVAL;
    }

    float voltage;
    if (_parse_float_range(_sh, _argv[1], &voltage, 0.0f, 40.0f, "V", "DAC") != 0) 
    {
        return -EINVAL;
    }

    SERVER_CH_SET_ASYC(DAC_VALUE, voltage);  
    shell_print(_sh, "DAC OK: set request sent: %.2f", (double)voltage);
    
    return 0;
}

static int _cmd_dac_get_action(const struct shell *_sh, size_t _argc, char **_argv)
{
    if (_argc != 1) 
    {
        shell_error(_sh, "DAC ERROR: Usage: DAC get");
        return -EINVAL;
    }

    float voltage = 0.0f;
    int ret = SYSTEM_BUS_GET(DAC_VALUE, &voltage);
    
    if (ret == 0) 
    {
        shell_print(_sh, "DAC OK: current value: %.2f V", (double)voltage);
    } 
    else 
    {
        shell_error(_sh, "DAC ERROR: Failed to get value (err: %d)", ret);
    }

    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
    sub_dac_cmds,
    SHELL_CMD(set, NULL, "Set DAC output: DAC set <0.0-4095.0>", _cmd_dac_set_action),
    SHELL_CMD(get, NULL, "Get DAC output: DAC get", _cmd_dac_get_action),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(
    DAC, 
    &sub_dac_cmds, 
    "DAC control (set/get voltage)", 
    NULL
);

static int _cmd_adj_res_set_action(const struct shell *_sh, size_t _argc, char **_argv)
{
    if (_argc != 3) 
    {
        shell_error(_sh, "ADJ_RES ERROR: Usage: ADJ_RES set <1-%d> <0.0-100.0>", 
                    (int)ARRAY_SIZE(adj_res_name));
        return -EINVAL;
    }
    
    long channel_num;
    if (_parse_channel(_sh, _argv[1], &channel_num, ARRAY_SIZE(adj_res_name), "ADJ_RES") != 0) 
    {
        return -EINVAL;
    }

    float resistance;
    if (_parse_float_range(_sh, _argv[2], &resistance, 0.0f, 100.0f, "kOm", "ADJ_RES") != 0) 
    {
        return -EINVAL;
    }

    int err = _sync_bus_set(adj_res_name[channel_num - 1], resistance);
    
    if (err == 0) 
    {
        shell_print(_sh, "ADJ_RES OK: channel %ld set to %.2f kOm", channel_num, (double)resistance);
    } 
    else if (err == -ETIMEDOUT) 
    {
        shell_error(_sh, "ADJ_RES ERROR: Failed to set value: timeout");
    } 
    else 
    {
        shell_error(_sh, "ADJ_RES ERROR: Failed to set value (err: %d)", err);
    }

    return 0;
}

static int _cmd_adj_res_get_action(const struct shell *_sh, size_t _argc, char **_argv)
{
    if (_argc != 2) 
    {
        shell_error(_sh, "ADJ_RES ERROR: Usage: ADJ_RES get <1-%d>", (int)ARRAY_SIZE(adj_res_name));
        return -EINVAL;
    }
    
    long channel_num;
    if (_parse_channel(_sh, _argv[1], &channel_num, ARRAY_SIZE(adj_res_name), "ADJ_RES") != 0) 
    {
        return -EINVAL;
    }

    float resistance = 0.0f;
    int ret = SYSTEM_BUS_GET(adj_res_name[channel_num - 1], &resistance);
    
    if (ret == 0) 
    {
        shell_print(_sh, "ADJ_RES OK: channel %ld current value: %.2f kOm", channel_num, (double)resistance);
    } 
    else 
    {
        shell_error(_sh, "ADJ_RES ERROR: Failed to get value (err: %d)", ret);
    }

    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
    sub_adj_res_cmds,
    SHELL_CMD(set, NULL, "Set resistance: ADJ_RES set <1-2> <0.0-100.0>", _cmd_adj_res_set_action),
    SHELL_CMD(get, NULL, "Get resistance: ADJ_RES get <1-2>", _cmd_adj_res_get_action),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(
    ADJ_RES, 
    &sub_adj_res_cmds, 
    "Adjustable resistor control (set/get values)", 
    NULL
);