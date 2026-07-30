#include <stdint.h>
#include <stdlib.h>  /* Для strtol */
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/zbus/zbus.h>   /* Добавляем заголовок Zbus */
#include "system_data_bus.h"   /* Структуры данных и перечисления */

LOG_MODULE_REGISTER(hc595_shell, LOG_LEVEL_INF);

/* 1. Объявляем внешний прикладной канал управления, созданный в out_and_power_control_rtio.c */
ZBUS_CHAN_DECLARE(app_control_chan);

/* ------------------------------------------------------------------ */
/* КОМАНДА: lin_pb_set <1-4> <ON/OFF>                                 */
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

    /* 
     * Атомарный Zero-Copy доступ к каналу Zbus.
     * Блокируем встроенный семафор канала [2].
     */
    int err = zbus_chan_claim(&app_control_chan, K_FOREVER);
    if (err == 0) {
        /* Получаем прямой указатель на структуру данных в памяти Zbus */
        struct hc595_channels_msg *msg = (struct hc595_channels_msg *)zbus_chan_msg(&app_control_chan);
        
        /* Модифицируем конкретное поле */
        msg->lin_pb[channel_num - 1] = active;

        /* Принудительно запускаем рассылку уведомлений (листенер RTIO проснется сам) [2] */
        zbus_chan_notify(&app_control_chan, K_MSEC(10));

        /* Освобождаем встроенный семафор канала */
        zbus_chan_finish(&app_control_chan);

        shell_print(sh, "LIN%ld_PD set to %s successfully.", channel_num, active ? "ON" : "OFF");
    } else {
        shell_error(sh, "Zbus channel claim failed: %d", err);
    }

    return err;
}

/* ------------------------------------------------------------------ */
/* КОМАНДА: out_set <1-18> <LOW/HIGH/INPUT>                           */
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

    /* 
     * Атомарный Zero-Copy доступ к каналу Zbus [2].
     */
    int err = zbus_chan_claim(&app_control_chan, K_FOREVER);
    if (err == 0) {
        struct hc595_channels_msg *msg = (struct hc595_channels_msg *)zbus_chan_msg(&app_control_chan);
        
        /* Модифицируем конкретный силовой канал */
        msg->low_cur_driver_channels[channel_num - 1] = state;

        /* Принудительно запускаем рассылку уведомлений [2] */
        zbus_chan_notify(&app_control_chan, K_MSEC(10));

        /* Освобождаем канал */
        zbus_chan_finish(&app_control_chan);

        const char *state_str = (state == OUT_LOW) ? "LOW" : 
                                ((state == OUT_HIGH) ? "HIGH" : "INPUT");
        shell_print(sh, "Channel %ld set to %s successfully.", channel_num, state_str);
    } else {
        shell_error(sh, "Zbus channel claim failed: %d", err);
    }

    return err;
}

/* ------------------------------------------------------------------ */
/* РЕГИСТРАЦИЯ КОМАНД                                                 */
/* ------------------------------------------------------------------ */

SHELL_CMD_REGISTER(lin_pb_set, NULL, 
                   "Set LIN pull-down state: lin_pb_set <1-4> <ON/OFF>", 
                   cmd_lin_pb_set);

SHELL_CMD_REGISTER(out_set, NULL, 
                   "Set output driver state: out_set <1-18> <LOW/HIGH/INPUT>", 
                   cmd_out_set);