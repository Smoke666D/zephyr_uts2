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

static int _cmd_led(const struct shell *_sh, size_t _argc, char **_argv)
{
    // Если введено просто "LED" или "LED ?"
    if (_argc == 1 || (_argc == 2 && strcmp(_argv[1], "?") == 0)) 
    {
        bool l1 = false, l2 = false, l3 = false;
        
        SYSTEM_BUS_GET(LED1, &l1);
        SYSTEM_BUS_GET(LED2, &l2);
        SYSTEM_BUS_GET(LED3, &l3);

        shell_print(_sh, "Current LED states:");
        shell_print(_sh, "  LED1 (Красный + Звук):    %s", l1 ? "ON" : "OFF");
        shell_print(_sh, "  LED2 (Зеленый):           %s", l2 ? "ON" : "OFF");
        shell_print(_sh, "  LED3 (Синий):             %s", l3 ? "ON" : "OFF");
        return 0;
    }

    // Управление: LED <1|2|3> <ON|OFF>
    if (_argc < 3) 
    {
        shell_error(_sh, "Invalid arguments.");
        shell_print(_sh, "Usage:");
        shell_print(_sh, "  LED                - Show status of all LEDs");
        shell_print(_sh, "  LED ?              - Show status of all LEDs");
        shell_print(_sh, "  LED <1-3> <ON|OFF> - Turn specific LED ON or OFF");
        return -EINVAL;
    }

    int led_num = atoi(_argv[1]);
    if (led_num < 1 || led_num > 3) 
    {
        shell_error(_sh, "Error: Invalid LED number. Use 1, 2, or 3.");
        return -EINVAL;
    }

    bool state = false;
    if (strcmp(_argv[2], "ON") == 0 || strcmp(_argv[2], "1") == 0) {
        state = true;
    } else if (strcmp(_argv[2], "OFF") == 0 || strcmp(_argv[2], "0") == 0) {
        state = false;
    } else {
        shell_error(_sh, "Error: Invalid state value. Use 'ON' or 'OFF'.");
        return -EINVAL;
    }

    switch (led_num) {
        case 1:
            SYSTEM_BUS_SET(LED1, (bool)state); // или SYSTEM_BUS_SET
            shell_print(_sh, "LED1 (Красный + Звук) set to %s", state ? "ON" : "OFF");
            break;
        case 2:
            SYSTEM_BUS_SET(LED2, (bool)state);
            shell_print(_sh, "LED2 (Зеленый) set to %s", state ? "ON" : "OFF");
            break;
        case 3:
            SYSTEM_BUS_SET(LED3, (bool)state);
            shell_print(_sh, "LED3 (Синий) set to %s", state ? "ON" : "OFF");
            break;
    }

    return 0;
}

SHELL_CMD_ARG_REGISTER(
    LED, 
    NULL, 
    "Control board LEDs and sound emitter.\n"
    "Usage:\n"
    "  LED                - Show status of all LEDs\n"
    "  LED ?              - Show status of all LEDs\n"
    "  LED <1-3> <ON|OFF> - Turn specific LED ON or OFF", 
    _cmd_led, 
    1, 
    2
);

/***************************************************************************************************
 *                                           DEFINITIONS & TABLES
 **************************************************************************************************/
#define NUM_PWR_CHANNELS POWER_CONTROL_LINE_CNT

/* Список имен каналов для удобства индексации (с 1 по 8) */
static const char *const pwr_channel_names[NUM_PWR_CHANNELS + 1] = {
    "NONE",
    "EN_DUT2_PSU",
    "EN_DUT3_PSU",
    "EN_P12V",
    "EN_P24V",
    "EN_VA",
    "EN_USB_OUT",
    "EN_USB_BOOT",
    "EN_USB_TOP"
};

/* Маппинг порядкового номера (1-8) на системные ID шины */
static inline SYSTEM_BUS_ID _get_pwr_bus_id(int num)
{
    switch (num) {
        case 1:  return EN_DUT2_PSU;
        case 2:  return EN_DUT3_PSU;
        case 3:  return EN_P12V;
        case 4:  return EN_P24V;
        case 5:  return EN_VA;
        case 6:  return EN_USB_OUT;
        case 7:  return EN_USB_BOOT;
        case 8:  return EN_USB_TOP;
        default: return (SYSTEM_BUS_ID)-1;
    }
}

/***************************************************************************************************
 *                                        COMMAND HANDLER
 **************************************************************************************************/

static int _cmd_pwr_ctl(const struct shell *_sh, size_t _argc, char **_argv)
{
    // ВЕТКА 1: Вывод статуса (если вызвано как "PWR_CTL" или "PWR_CTL ?")
    if (_argc == 1 || (_argc == 2 && strcmp(_argv[1], "?") == 0)) 
    {
        shell_print(_sh, "Current Power Control states:");
        for (int i = 1; i <= NUM_PWR_CHANNELS; i++) 
        {
            bool state = false;
            SYSTEM_BUS_ID id = _get_pwr_bus_id(i);
            
            if (bus_get_bool(id, &state) == 0) {
                shell_print(_sh, "  %d - %-12s: %s", i, pwr_channel_names[i], state ? "ON" : "OFF");
            } else {
                shell_print(_sh, "  %d - %-12s: [N/A]", i, pwr_channel_names[i]);
            }
        }
        return 0;
    }

    // Проверяем, передан ли глобальный параметр ON или OFF (без номера канала)
    // Например: PWR_CTL ON  или  PWR_CTL OFF
    if (_argc == 2) 
    {
        bool state = false;
        if (strcmp(_argv[1], "ON") == 0 || strcmp(_argv[1], "1") == 0) {
            state = true;
        } else if (strcmp(_argv[1], "OFF") == 0 || strcmp(_argv[1], "0") == 0) {
            state = false;
        } else {
            shell_error(_sh, "Error: Invalid global state. Use 'ON', 'OFF', '1', or '0'.");
            return -EINVAL;
        }

        // Применяем состояние ко ВСЕМ каналам по очереди
        shell_print(_sh, "Applying state %s to ALL power channels:", state ? "ON" : "OFF");
        for (int i = 1; i <= NUM_PWR_CHANNELS; i++) {
            SYSTEM_BUS_ID id = _get_pwr_bus_id(i);
            int ret = bus_set_bool(id, state);
            if (ret == 0) {
                shell_print(_sh, "  %s -> %s", pwr_channel_names[i], state ? "ON" : "OFF");
            } else {
                shell_error(_sh, "  %s -> Failed (err: %d)", pwr_channel_names[i], ret);
            }
        }
        return 0;
    }

    // ВЕТКА 3: Управление конкретным каналом (ожидаем: PWR_CTL <1-8> <ON|OFF>)
    if (_argc < 3) 
    {
        shell_error(_sh, "Invalid arguments.");
        shell_print(_sh, "Usage:");
        shell_print(_sh, "  PWR_CTL                  - Show status of all channels");
        shell_print(_sh, "  PWR_CTL ?                - Show status of all channels");
        shell_print(_sh, "  PWR_CTL <ON|OFF>         - Turn ALL channels ON or OFF");
        shell_print(_sh, "  PWR_CTL <1-8> <ON|OFF>   - Turn specific channel ON or OFF");
        shell_print(_sh, "\nChannels map:");
        for (int i = 1; i <= NUM_PWR_CHANNELS; i++) {
            shell_print(_sh, "  %d: %s", i, pwr_channel_names[i]);
        }
        return -EINVAL;
    }

    int ch_num = atoi(_argv[1]);
    if (ch_num < 1 || ch_num > NUM_PWR_CHANNELS) 
    {
        shell_error(_sh, "Error: Invalid channel number. Use from 1 to %d.", NUM_PWR_CHANNELS);
        return -EINVAL;
    }

    bool state = false;
    if (strcmp(_argv[2], "ON") == 0 || strcmp(_argv[2], "1") == 0) {
        state = true;
    } else if (strcmp(_argv[2], "OFF") == 0 || strcmp(_argv[2], "0") == 0) {
        state = false;
    } else {
        shell_error(_sh, "Error: Invalid state value. Use 'ON' or 'OFF'.");
        return -EINVAL;
    }

    SYSTEM_BUS_ID id = _get_pwr_bus_id(ch_num);
    int ret = bus_set_bool(id, state);

    if (ret == 0) {
        shell_print(_sh, "%s (%d) set to %s", pwr_channel_names[ch_num], ch_num, state ? "ON" : "OFF");
    } else {
        shell_error(_sh, "Failed to set state for %s (err: %d)", pwr_channel_names[ch_num], ret);
    }

    return 0;
}

// Изменяем максимальное количество аргументов до 2, чтобы шелл пропускал команды вида "PWR_CTL ON"
SHELL_CMD_ARG_REGISTER(
    PWR_CTL, 
    NULL, 
    "Control power channels and supplies.\n"
    "Usage:\n"
    "  PWR_CTL                  - Show status of all channels\n"
    "  PWR_CTL ?                - Show status of all channels\n"
    "  PWR_CTL <ON|OFF>         - Turn ALL channels ON or OFF\n"
    "  PWR_CTL <1-8> <ON|OFF>   - Turn specific channel ON or OFF", 
    _cmd_pwr_ctl, 
    1, 
    2
);

#define NUM_LIN_PD_CHANNELS 4

/* Массив имен каналов */
static const char *const lin_pd_channel_names[NUM_LIN_PD_CHANNELS + 1] = {
    "",         // Заглушка для индекса 0
    "LIN_PD1",
    "LIN_PD2",
    "LIN_PD3",
    "LIN_PD4"
};

/* Функция маппинга номера канала на SYSTEM_BUS_ID */
static inline SYSTEM_BUS_ID _get_lin_pd_bus_id(int ch_num)
{
    switch (ch_num) {
        case 1:  return LIN_PD1;
        case 2:  return LIN_PD2;
        case 3:  return LIN_PD3;
        case 4:  return LIN_PD4;
        default: return (SYSTEM_BUS_ID)-1;
    }
}
static int _cmd_lin_pd(const struct shell *_sh, size_t _argc, char **_argv)
{
    // ВЕТКА 1: Вывод статуса (если вызвано как "LIN_PD" или "LIN_PD ?")
    if (_argc == 1 || (_argc == 2 && strcmp(_argv[1], "?") == 0)) 
    {
        shell_print(_sh, "Current LIN_PD states:");
        for (int i = 1; i <= NUM_LIN_PD_CHANNELS; i++) 
        {
            uint32_t state = 0;
            SYSTEM_BUS_ID id = _get_lin_pd_bus_id(i);
            
            if (bus_get_u32(id, &state) == 0) {
                shell_print(_sh, "  %d - %-10s: %s (%u)", i, lin_pd_channel_names[i], (state > 0) ? "ON" : "OFF", state);
            } else {
                shell_print(_sh, "  %d - %-10s: [N/A]", i, lin_pd_channel_names[i]);
            }
        }
        return 0;
    }

    // ВЕТКА 2: Глобальное управление ВСЕМИ каналами (например: LIN_PD ON или LIN_PD OFF)
    if (_argc == 2) 
    {
        uint32_t val = 0;
        if (strcmp(_argv[1], "ON") == 0 || strcmp(_argv[1], "1") == 0) {
            val = 1;
        } else if (strcmp(_argv[1], "OFF") == 0 || strcmp(_argv[1], "0") == 0) {
            val = 0;
        } else {
            shell_error(_sh, "Error: Invalid global state. Use 'ON' or 'OFF'.");
            return -EINVAL;
        }

        shell_print(_sh, "Applying state %s (%u) to ALL LIN_PD channels:", val ? "ON" : "OFF", val);
        for (int i = 1; i <= NUM_LIN_PD_CHANNELS; i++) {
            SYSTEM_BUS_ID id = _get_lin_pd_bus_id(i);
            int ret = bus_set_u32(id, val);
            if (ret == 0) {
                shell_print(_sh, "  %s -> %s", lin_pd_channel_names[i], val ? "ON" : "OFF");
            } else {
                shell_error(_sh, "  %s -> Failed (err: %d)", lin_pd_channel_names[i], ret);
            }
        }
        return 0;
    }

    // ВЕТКА 3: Управление конкретным каналом (ожидаем: LIN_PD <1-4> <ON|OFF>)
    if (_argc < 3) 
    {
        shell_error(_sh, "Invalid arguments.");
        shell_print(_sh, "Usage:");
        shell_print(_sh, "  LIN_PD                 - Show status of all channels");
        shell_print(_sh, "  LIN_PD ?               - Show status of all channels");
        shell_print(_sh, "  LIN_PD <ON|OFF>        - Set ALL channels to ON or OFF");
        shell_print(_sh, "  LIN_PD <1-4> <ON|OFF>  - Set specific channel to ON or OFF");
        return -EINVAL;
    }

    int ch_num = atoi(_argv[1]);
    if (ch_num < 1 || ch_num > NUM_LIN_PD_CHANNELS) 
    {
        shell_error(_sh, "Error: Invalid channel number. Use from 1 to %d.", NUM_LIN_PD_CHANNELS);
        return -EINVAL;
    }

    // Конвертируем ON/OFF в uint32_t (1 или 0)
    uint32_t val = 0;
    if (strcmp(_argv[2], "ON") == 0 || strcmp(_argv[2], "1") == 0) {
        val = 1;
    } else if (strcmp(_argv[2], "OFF") == 0 || strcmp(_argv[2], "0") == 0) {
        val = 0;
    } else {
        shell_error(_sh, "Error: Invalid state value. Use 'ON' or 'OFF'.");
        return -EINVAL;
    }

    SYSTEM_BUS_ID id = _get_lin_pd_bus_id(ch_num);
    int ret = bus_set_u32(id, val);

    if (ret == 0) {
        shell_print(_sh, "%s (%d) set to %s (%u)", lin_pd_channel_names[ch_num], ch_num, val ? "ON" : "OFF", val);
    } else {
        shell_error(_sh, "Failed to set state for %s (err: %d)", lin_pd_channel_names[ch_num], ret);
    }

    return 0;
}

// Регистрация команды LIN_PD в Zephyr Shell
SHELL_CMD_ARG_REGISTER(
    LIN_PD, 
    NULL, 
    "Control LIN power distribution channels (ON/OFF).\n"
    "Usage:\n"
    "  LIN_PD                 - Show status of all channels\n"
    "  LIN_PD ?               - Show status of all channels\n"
    "  LIN_PD <ON|OFF>        - Set ALL channels to ON or OFF\n"
    "  LIN_PD <1-4> <ON|OFF>  - Set specific channel to ON or OFF", 
    _cmd_lin_pd, 
    1, 
    2
);


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
            int ret = bus_set_u32(id, val);
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
    int ret = bus_set_u32(id, val);

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


