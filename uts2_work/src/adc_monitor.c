#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

/* Подключаем наш общий Zbus заголовок */
#include "system_data_bus.h"

LOG_MODULE_REGISTER(adc_mon, LOG_LEVEL_INF);

/* 
 * ОПРЕДЕЛЯЕМ ПОДПИСЧИКА.
 * Это создаст очередь сообщений для нашего монитора.
 */
ZBUS_SUBSCRIBER_DEFINE(adc_monitor_sub, 4);

K_THREAD_STACK_DEFINE(monitor_stack, 2048);
static struct k_thread monitor_thread_data;

/* Функция нашего потока-подписчика */
static void adc_monitor_thread_fn(void *p1, void *p2, void *p3)
{
    const struct zbus_channel *chan;
    struct adc_data_msg msg;

    LOG_INF("Starting Zbus ADC Monitor Thread...");

    while (1) {
        /* Ждем сообщения из очереди подписчика */
               k_msleep(3000);

        /* 
         * Потокобезопасно читаем из канала АЦП самую свежую копию данных.
         * Защита от гонок данных гарантирована внутренним мьютексом канала Zbus.
         */
        int ret = zbus_chan_read(&adc_data_chan, &msg, K_MSEC(10));
        if (ret == 0) {
            /* Выводим данные в терминал */
            LOG_INF("=== [Zbus State Monitor] Latest Data ===");
            for (int step = 0; step < 8; step++) {
                LOG_INF("Step %d | VDDA: %u mV | Temp: %d C | Chan0: %u mV | Chan1: %u mV",
                        step, msg.vdda_mv, msg.core_temp,
                        msg.channels_mv[step * 2 + 0],
                        msg.channels_mv[step * 2 + 1]);
            }
        } else {
            LOG_WRN("Failed to read Zbus state channel! Code: %d", ret);
        }
    }
}

/* Функция запуска потока монитора */
int adc_monitor_thread_start(void)
{
    k_thread_create(&monitor_thread_data, monitor_stack, 2048,
                    adc_monitor_thread_fn, NULL, NULL, NULL, 11, 0, K_NO_WAIT);
    return 0;
}