#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <strings.h> 
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#include "system_bus_model.h"

/***************************************************************************************************
 *                                        COMMAND HANDLER
 **************************************************************************************************/

static K_SEM_DEFINE(out_ctl_done, 0, 1);
static uint8_t out_ctl_response = 0;


#define NUM_DOUT_CHANNELS 18


/* Текстовые представления состояний */
static const char *const state_str[] = {
    "IN",
    "LOW",
    "HIGH",
    "SHORT"
};

/* Массив имен каналов DOUT1 - DOUT18 */
static const char *const dout_channel_names[NUM_DOUT_CHANNELS + 1] = {
    "",
    "DOUT1",  "DOUT2",  "DOUT3",  "DOUT4",  "DOUT5",  "DOUT6",
    "DOUT7",  "DOUT8",  "DOUT9",  "DOUT10", "DOUT11", "DOUT12",
    "DOUT13", "DOUT14", "DOUT15", "DOUT16", "DOUT17", "DOUT18"
};

/* Функция маппинга номера канала на SYSTEM_BUS_ID */
static inline SYSTEM_BUS_ID _get_dout_bus_id(int ch_num)
{
    switch (ch_num) {
        case 1:  return DOUT1;  case 2:  return DOUT2;
        case 3:  return DOUT3;  case 4:  return DOUT4;
        case 5:  return DOUT5;  case 6:  return DOUT6;
        case 7:  return DOUT7;  case 8:  return DOUT8;
        case 9:  return DOUT9;  case 10: return DOUT10;
        case 11: return DOUT11; case 12: return DOUT12;
        case 13: return DOUT13; case 14: return DOUT14;
        case 15: return DOUT15; case 16: return DOUT16;
        case 17: return DOUT17; case 18: return DOUT18;
        default: return (SYSTEM_BUS_ID)-1;
    }
}

/* Функция парсинга текстового или числового состояния */
static bool parse_state_arg(const char *arg, uint32_t *out_state)
{
    if (strcasecmp(arg, "IN") == 0 || strcmp(arg, "0") == 0) {
        *out_state = STATE_IN;
        return true;
    }
    if (strcasecmp(arg, "LOW") == 0 || strcmp(arg, "1") == 0) {
        *out_state = STATE_LOW;
        return true;
    }
    if (strcasecmp(arg, "HIGH") == 0 || strcmp(arg, "2") == 0) {
        *out_state = STATE_HIGH;
        return true;
    }
    if (strcasecmp(arg, "SHORT") == 0 || strcmp(arg, "3") == 0) {
        *out_state = STATE_SHORT;
        return true;
    }
    return false;
}

/***************************************************************************************************
 *                                        COMMAND HANDLER
 **************************************************************************************************/

static int _cmd_dout(const struct shell *_sh, size_t _argc, char **_argv)
{
    // ВЕТКА 1: Вывод статуса (если вызвано как "DOUT" или "DOUT ?")
    if (_argc == 1 || (_argc == 2 && strcmp(_argv[1], "?") == 0)) 
    {
        shell_print(_sh, "Current DOUT states:");
        for (int i = 1; i <= NUM_DOUT_CHANNELS; i++) 
        {
            uint32_t state = 0;
            SYSTEM_BUS_ID id = _get_dout_bus_id(i);
            
            if (bus_get_u32(id, &state) == 0) {
                const char *s_name = (state <= 3) ? state_str[state] : "UNKNOWN";
                shell_print(_sh, "  %2d - %-8s: %-5s (%u)", i, dout_channel_names[i], s_name, state);
            } else {
                shell_print(_sh, "  %2d - %-8s: [N/A]", i, dout_channel_names[i]);
            }
        }
        return 0;
    }

    // ВЕТКА 2: Глобальное управление ВСЕМИ каналами (например: DOUT HIGH или DOUT 2)
    if (_argc == 2) 
    {
        uint32_t val = 0;
        if (!parse_state_arg(_argv[1], &val)) {
            shell_error(_sh, "Error: Invalid state. Use IN, LOW, HIGH, SHORT (or 0-3).");
            return -EINVAL;
        }

        // STATE_SHORT недопустим при глобальном применении, так как он запрещен для DOUT17-18
        if (val == STATE_SHORT) {
            shell_error(_sh, "Error: STATE_SHORT is restricted and cannot be applied globally (allowed only for channels 1-16).");
            return -EINVAL;
        }

        shell_print(_sh, "Applying state %s (%u) to ALL DOUT channels (1-18):", state_str[val], val);
        for (int i = 1; i <= NUM_DOUT_CHANNELS; i++) {
            SYSTEM_BUS_ID id = _get_dout_bus_id(i);
            int ret = SERVER_CH_SET_SYC(id, val,&out_ctl_done,&out_ctl_response);
            if (k_sem_take(&out_ctl_done, K_MSEC(100)) != 0) 
            {
                ret = -ETIMEDOUT;
            }
            else 
            {
                ret = out_ctl_response;
            }

            if (ret == 0) {
                shell_print(_sh, "  %s -> %s", dout_channel_names[i], state_str[val]);
            } else {
                shell_error(_sh, "  %s -> Failed (err: %d)", dout_channel_names[i], ret);
            }
        }
        return 0;
    }

    // ВЕТКА 3: Управление конкретным каналом (ожидаем: DOUT <1-18> <STATE>)
    if (_argc < 3) 
    {
        shell_error(_sh, "Invalid arguments.");
        shell_print(_sh, "Usage:");
        shell_print(_sh, "  DOUT                   - Show status of all channels");
        shell_print(_sh, "  DOUT ?                 - Show status of all channels");
        shell_print(_sh, "  DOUT <STATE>           - Set channels 1-18 to state");
        shell_print(_sh, "  DOUT <1-18> <STATE>    - Set specific channel (SHORT allowed for 1-16 only)");
        return -EINVAL;
    }

    int ch_num = atoi(_argv[1]);
    if (ch_num < 1 || ch_num > NUM_DOUT_CHANNELS) 
    {
        shell_error(_sh, "Error: Invalid channel number. Use from 1 to %d.", NUM_DOUT_CHANNELS);
        return -EINVAL;
    }

    uint32_t val = 0;
    if (!parse_state_arg(_argv[2], &val)) {
        shell_error(_sh, "Error: Invalid state value. Use IN (0), LOW (1), HIGH (2), or SHORT (3).");
        return -EINVAL;
    }

    // Проверка ограничений: STATE_SHORT (3) запрещен для DOUT17 и DOUT18
    if (val == STATE_SHORT && ch_num > 16) {
        shell_error(_sh, "Error: STATE_SHORT is not allowed for %s (channels 17-18 do not support it).", dout_channel_names[ch_num]);
        return -EINVAL;
    }

    SYSTEM_BUS_ID id = _get_dout_bus_id(ch_num);
     int ret = SERVER_CH_SET_SYC(id, val,&out_ctl_done,&out_ctl_response);
    if (k_sem_take(&out_ctl_done, K_MSEC(100)) != 0) 
        {
                ret = -ETIMEDOUT;
            }
            else 
            {
                ret = out_ctl_response;
            }

    if (ret == 0) {
        shell_print(_sh, "%s (%d) set to %s (%u)", dout_channel_names[ch_num], ch_num, state_str[val], val);
    } else {
        shell_error(_sh, "Failed to set state for %s (err: %d)", dout_channel_names[ch_num], ret);
    }

    return 0;
}

// Регистрация команды DOUT в Zephyr Shell
SHELL_CMD_ARG_REGISTER(
    DOUT, 
    NULL, 
    "Control digital outputs DOUT1-DOUT18 (States: IN, LOW, HIGH, SHORT).\n"
    "Usage:\n"
    "  DOUT                   - Show status of all channels\n"
    "  DOUT ?                 - Show status of all channels\n"
    "  DOUT <STATE>           - Set channels 1-18 globally\n"
    "  DOUT <1-18> <STATE>    - Set specific channel (SHORT for 1-16 only)", 
    _cmd_dout, 
    1, 
    2
);


