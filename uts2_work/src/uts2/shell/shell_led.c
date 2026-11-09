/**
 *  @file       shell_led.c
 *  @brief      Команды Zephyr Shell для управления индикаторами и звукоизлучателем (LED)
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <strings.h> 

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#include "system_bus_model.h"

/***************************************************************************************************
 *                                        COMMAND HANDLERS
 **************************************************************************************************/

/* Субкоманда: LED get [1-3] */
static int _cmd_led_get(const struct shell *sh, size_t argc, char **argv)
{
    bool l1 = false, l2 = false, l3 = false;
    
    SYSTEM_BUS_GET(LED1, &l1);
    SYSTEM_BUS_GET(LED2, &l2);
    SYSTEM_BUS_GET(LED3, &l3);

    if (argc == 1) 
    {
        shell_print(sh, "LED OK: Current states:");
        shell_print(sh, "  1 (LED1 - Красный + Звук): %s", l1 ? "ON" : "OFF");
        shell_print(sh, "  2 (LED2 - Зеленый):        %s", l2 ? "ON" : "OFF");
        shell_print(sh, "  3 (LED3 - Синий):          %s", l3 ? "ON" : "OFF");
        return 0;
    }

    if (argc == 2) 
    {
        int led_num = atoi(argv[1]);
        if (led_num < 1 || led_num > 3) {
            shell_error(sh, "LED ERROR: Invalid LED number. Use 1, 2, or 3.");
            return -EINVAL;
        }

        bool state = (led_num == 1) ? l1 : (led_num == 2) ? l2 : l3;
        shell_print(sh, "LED OK: LED%d is %s", led_num, state ? "ON" : "OFF");
        return 0;
    }

    return -EINVAL;
}

/* Субкоманда: LED set <1-3> <ON|OFF> */
static int _cmd_led_set(const struct shell *sh, size_t argc, char **argv)
{
    if (argc != 3) 
    {
        shell_error(sh, "LED ERROR: Invalid arguments.");
        shell_print(sh, "Usage: LED set <1-3> <ON|OFF>");
        shell_print(sh, "  1 - LED1 (Красный + Звук)");
        shell_print(sh, "  2 - LED2 (Зеленый)");
        shell_print(sh, "  3 - LED3 (Синий)");
        return -EINVAL;
    }

    int led_num = atoi(argv[1]);
    if (led_num < 1 || led_num > 3) 
    {
        shell_error(sh, "LED ERROR: Invalid LED number. Use 1, 2, or 3.");
        return -EINVAL;
    }

    bool state = false;
    if (strcmp(argv[2], "ON") == 0 || strcmp(argv[2], "1") == 0) {
        state = true;
    } else if (strcmp(argv[2], "OFF") == 0 || strcmp(argv[2], "0") == 0) {
        state = false;
    } else {
        shell_error(sh, "LED ERROR: Invalid state value. Use 'ON' or 'OFF'.");
        return -EINVAL;
    }

    switch (led_num) {
        case 1:
            SERVER_CH_SET_ASYC(LED1, (bool)state);
            shell_print(sh, "LED OK: LED1 (Красный + Звук) set to %s", state ? "ON" : "OFF");
            break;
        case 2:
            SERVER_CH_SET_ASYC(LED2, (bool)state);
            shell_print(sh, "LED OK: LED2 (Зеленый) set to %s", state ? "ON" : "OFF");
            break;
        case 3:
            SERVER_CH_SET_ASYC(LED3, (bool)state);
            shell_print(sh, "LED OK: LED3 (Синий) set to %s", state ? "ON" : "OFF");
            break;
    }

    return 0;
}

/***************************************************************************************************
 *                                        REGISTRATION
 **************************************************************************************************/

SHELL_STATIC_SUBCMD_SET_CREATE(sub_led,
    SHELL_CMD_ARG(get, NULL, "Get LED status.\nUsage: LED get [1-3]", _cmd_led_get, 1, 1),
    SHELL_CMD_ARG(set, NULL, "Set LED state.\nUsage: LED set <1-3> <ON|OFF>", _cmd_led_set, 3, 0),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(LED, &sub_led, "Control board LEDs and sound emitter", NULL);