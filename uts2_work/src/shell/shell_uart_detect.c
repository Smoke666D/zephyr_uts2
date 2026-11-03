#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include "system_bus_model.h"
#include <zephyr/zbus/zbus.h>

/* Очередь на прием байт (совет: для тестов лучше увеличить размер, например до 64) */
K_MSGQ_DEFINE(uart_detect_rx_queue, sizeof(uint8_t), 64, 4);

/* Слушатель Zbus: перекладывает принятый байт в очередь */
static void _uart_detect_rx_listener_cb(const struct zbus_channel *_chan)
{
    ARG_UNUSED(_chan);
    uint32_t _temp;
    
    SYSTEM_BUS_GET(UART_DETECT_RX, &_temp);
    uint8_t _msg = (uint8_t)_temp;  
    
    /* Кладем байт в очередь без блокировки */
    k_msgq_put(&uart_detect_rx_queue, &_msg, K_NO_WAIT);
}

ZBUS_LISTENER_DEFINE(uart_detect_rx_listener, _uart_detect_rx_listener_cb);

/* ====================================================================
 * Shell команда: UART_DETECT
 * ==================================================================== */
static int cmd_uart_detect(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    bus_listener_attach(&uart_detect_rx_listener,UART_DETECT_RX);
    SYSTEM_BUS_SET(EN_USB_UART, (bool)true);
    uint8_t byte;
    uint32_t count = 0;

    shell_print(sh, "--- [UART_DETECT RX Buffer Dump] ---");

    /* 
     * Вычитываем байты из очереди:
     * Таймаут 50 мс позволяет дождаться хвоста посылки, 
     * если байты всё ещё поступают по шине прямо сейчас.
     */
    while (k_msgq_get(&uart_detect_rx_queue, &byte, K_MSEC(50)) == 0) {
        if (count == 0) {
            shell_fprintf(sh, SHELL_NORMAL, "HEX:   ");
        }
        shell_fprintf(sh, SHELL_NORMAL, "%02X ", byte);
        count++;
    }

    if (count == 0) {
        shell_warn(sh, "Очередь пуста (нет новых данных).");
    } else {
        shell_print(sh, "\r\nВсего принято байт: %u", count);
    }

    return 0;
}

/* Регистрация команды в Shell */
SHELL_CMD_REGISTER(UART_DETECT, NULL, "Чтение принятых данных UART_DETECT из очереди", cmd_uart_detect);