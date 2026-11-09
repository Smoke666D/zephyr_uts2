#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include "system_bus_model.h"
#include <zephyr/zbus/zbus.h>

#define POLL_INTERVAL_MS 30
#define RX_TEMP_BUF_SIZE 128

/* Очередь для приема байт */
K_MSGQ_DEFINE(uart_detect_rx_queue, sizeof(uint8_t), 128, 4);

static struct k_work_delayable uart_detect_work;
static const struct shell *active_shell = NULL;
static atomic_t is_monitoring = ATOMIC_INIT(0);

/* ====================================================================
 * 1. Слушатель Zbus
 * ==================================================================== */
static void _uart_detect_rx_listener_cb(const struct zbus_channel *_chan)
{
    /* 
     * Самый быстрый и надежный способ получить данные из канала Zbus —
     * взять их прямо из указателя _chan без дополнительных функций
     */
    const uint32_t *val = zbus_chan_const_msg(_chan);
    uint8_t _msg = (uint8_t)(*val);  
    
    /* Кладем байт в очередь */
    k_msgq_put(&uart_detect_rx_queue, &_msg, K_NO_WAIT);

    /* 
     * ВОТ КЛЮЧЕВОЕ ИСПРАВЛЕНИЕ:
     * Если мониторинг включен, каждый пришедший байт перезапускает таймер на 30 мс.
     * Как только пачка байт закончится (пауза 30 мс), сработает uart_detect_work_handler!
     */
    if (atomic_get(&is_monitoring)) {
        k_work_reschedule(&uart_detect_work, K_MSEC(POLL_INTERVAL_MS));
    }
}

ZBUS_LISTENER_DEFINE(uart_detect_rx_listener, _uart_detect_rx_listener_cb);

/* ====================================================================
 * 2. Обработчик k_work (вызывается после паузы в приеме)
 * ==================================================================== */
static void uart_detect_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    if (!atomic_get(&is_monitoring) || active_shell == NULL) {
        return;
    }

    uint8_t byte;
    char ascii_buf[RX_TEMP_BUF_SIZE + 1];
    size_t count = 0;

    /* Забираем всё, что пришло к этой миллисекунде */
    while (count < RX_TEMP_BUF_SIZE && 
           k_msgq_get(&uart_detect_rx_queue, &byte, K_NO_WAIT) == 0) {
        
        if (byte >= 32 && byte <= 126) {
            ascii_buf[count] = (char)byte;
        } else {
            ascii_buf[count] = '.'; /* Непечатные символы */
        }
        count++;
    }

    /* Печатаем собранную посылку */
    if (count > 0) {
        ascii_buf[count] = '\0';
        shell_print(active_shell, "+UART_DETECT %zu %s", count, ascii_buf);
    }

    /* 
     * Внимание: здесь НЕ нужно вызывать k_work_reschedule! 
     * Воркер заснет и проснется только тогда, когда слушатель примет следующий байт.
     */
}

/* Инициализация при старте */
static int init_uart_detect_worker(void)
{
    k_work_init_delayable(&uart_detect_work, uart_detect_work_handler);
    bus_listener_attach(&uart_detect_rx_listener, UART_DETECT_RX);
    return 0;
}
SYS_INIT(init_uart_detect_worker, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

/* ====================================================================
 * 3. Команды Shell
 * ==================================================================== */
static int cmd_uart_detect_start(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    if (atomic_get(&is_monitoring)) {
        shell_warn(sh, "Мониторинг UART_DETECT уже запущен.");
        return 0;
    }

    /* Запоминаем текущую сессию консоли */
    active_shell = sh;
    
    /* Очищаем старые данные из очереди */
    k_msgq_purge(&uart_detect_rx_queue);
    
    /* Разрешаем мониторинг */
    atomic_set(&is_monitoring, 1);

    shell_print(sh, "Мониторинг UART_DETECT запущен. Жду данные...");
    return 0;
}

static int cmd_uart_detect_stop(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    if (!atomic_get(&is_monitoring)) {
        shell_warn(sh, "Мониторинг UART_DETECT не был запущен.");
        return 0;
    }

    atomic_set(&is_monitoring, 0);
    k_work_cancel_delayable(&uart_detect_work);
    active_shell = NULL;

    shell_print(sh, "Мониторинг UART_DETECT остановлен.");
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_uart_detect,
    SHELL_CMD(start, NULL, "Запустить мониторинг", cmd_uart_detect_start),
    SHELL_CMD(stop, NULL, "Остановить мониторинг", cmd_uart_detect_stop),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(UART_DETECT, &sub_uart_detect, "Управление мониторингом UART_DETECT", NULL);