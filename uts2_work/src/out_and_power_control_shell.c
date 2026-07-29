#include <stdint.h>
#include <stdlib.h>  /* Для strtol */
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include "global_params.h"
#include "param_server.h"

/* ------------------------------------------------------------------ */
/* 1. КОМАНДА: lin_pb_set <1-4> <ON/OFF>                              */
/* ------------------------------------------------------------------ */
static int cmd_lin_pb_set(const struct shell *sh, size_t argc, char **argv)
{
    if (argc != 3) {
        shell_error(sh, "Usage: lin_pb_set <1-4> <ON/OFF>");
        return -EINVAL;
    }

    /* Парсим номер канала LIN (1-4) */
    char *endptr;
    long channel_num = strtol(argv[1], &endptr, 10);
    if (*endptr != '\0' || channel_num < 1 || channel_num > 4) {
        shell_error(sh, "Invalid LIN channel: %s (Must be 1 to 4)", argv[1]);
        return -EINVAL;
    }

    /* Парсим состояние (ON/OFF) */
    bool active;
    if (strcmp(argv[2], "ON") == 0 || strcmp(argv[2], "on") == 0) {
        active = true;
    } else if (strcmp(argv[2], "OFF") == 0 || strcmp(argv[2], "off") == 0) {
        active = false;
    } else {
        shell_error(sh, "Invalid state: %s (Must be ON or OFF)", argv[2]);
        return -EINVAL;
    }

    /* Превращаем в ID параметра. LIN1_PD — это базовый ID */
    PARAM_ID param_id = LIN1_PD + (channel_num - 1);

    PARAM_VAL val;
    val.value.boolean = active;

    /* Записываем через Сервер Параметров */
    int ret = param_set(param_id, &val);
    if (ret == 0) {
        shell_print(sh, "LIN%ld_PD successfully set to %s", 
                    channel_num, active ? "ON" : "OFF");
    } else if (ret == -ENODEV) {
        shell_error(sh, "Error: out_and_power module is not active or compiled.");
    } else {
        shell_error(sh, "Failed to set LIN: error code %d", ret);
    }

    return ret;
}

/* ------------------------------------------------------------------ */
/* 2. КОМАНДА: out_set <1-18> <LOW/HIGH/INPUT>                         */
/* ------------------------------------------------------------------ */
static int cmd_out_set(const struct shell *sh, size_t argc, char **argv)
{
    if (argc != 3) {
        shell_error(sh, "Usage: out_set <1-18> <LOW/HIGH/INPUT>");
        return -EINVAL;
    }

    /* Парсим номер силового канала (1-18) */
    char *endptr;
    long channel_num = strtol(argv[1], &endptr, 10);
    if (*endptr != '\0' || channel_num < 1 || channel_num > 18) {
        shell_error(sh, "Invalid power channel: %s (Must be 1 to 18)", argv[1]);
        return -EINVAL;
    }

    /* Парсим желаемое состояние (LOW/HIGH/INPUT) */
    LOW_CUR_DRIVER_STATE state;
    if (strcmp(argv[2], "LOW") == 0 || strcmp(argv[2], "low") == 0) {
        state = OUT_LOW;
    } else if (strcmp(argv[2], "HIGH") == 0 || strcmp(argv[2], "high") == 0) {
        state = OUT_HIGH;
    } else if (strcmp(argv[2], "INPUT") == 0 || strcmp(argv[2], "input") == 0) {
        state = INPUT;
    } else {
        shell_error(sh, "Invalid state: %s (Must be LOW, HIGH, or INPUT)", argv[2]);
        return -EINVAL;
    }

    /* Превращаем в ID параметра */
    PARAM_ID param_id = POWER_DRV_CTR_CHANNEL1 + (channel_num - 1);

    PARAM_VAL val;
    val.value.integer = (int32_t)state;

    /* Записываем через Сервер Параметров */
    int ret = param_set(param_id, &val);
    if (ret == 0) {
        const char *state_str = (state == OUT_LOW) ? "LOW" : 
                                ((state == OUT_HIGH) ? "HIGH" : "INPUT");
        shell_print(sh, "Channel %ld successfully set to %s", channel_num, state_str);
    } else if (ret == -ENODEV) {
        shell_error(sh, "Error: out_and_power module is not active or compiled.");
    } else {
        shell_error(sh, "Failed to set Channel: error code %d", ret);
    }

    return ret;
}

/* ------------------------------------------------------------------ */
/* РЕГИСТРАЦИЯ КОМАНД В СИСТЕМЕ SHELL ZEPHYR                          */
/* ------------------------------------------------------------------ */

SHELL_CMD_REGISTER(lin_pb_set, NULL, 
                   "Set LIN pull-down state: lin_pb_set <1-4> <ON/OFF>", 
                   cmd_lin_pb_set);

SHELL_CMD_REGISTER(out_set, NULL, 
                   "Set output driver state: out_set <1-18> <LOW/HIGH/INPUT>", 
                   cmd_out_set);