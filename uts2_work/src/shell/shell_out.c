#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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
        shell_print(_sh, "  LED1 (Красный + Звук):     %s", l1 ? "ON" : "OFF");
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
