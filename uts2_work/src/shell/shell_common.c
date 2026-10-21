#include <stdint.h>
#include <stdlib.h>  /* Для strtol */
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include "shell_common.h"

bool shell_keypress_check(const struct shell *sh)
{
    uint8_t c = 0;
    size_t read_bytes = 0;

    // Защита от нулевых указателей
    if (sh && sh->iface && sh->iface->api && sh->iface->api->read) {
        // Читаем 1 байт в неблокирующем режиме
        sh->iface->api->read(sh->iface, &c, 1, &read_bytes);
    }

    // Если что-то прочитали — проверяем, не нажали ли Esc (0x1B) или Ctrl+C (0x03)
    if (read_bytes > 0) {
        if (c == 0x1B || c == 0x03) {
            return true; // Сигнал прерывания
        }
    }
    return false;
}