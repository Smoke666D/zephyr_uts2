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

static void adc_monitor_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    LOG_INF("Starting State-Reader ADC Monitor Thread...");

    while (1) {
        /* Опрашиваем Zbus раз в 3 секунды */
        k_msleep(3000);

        int ret = 0;
        uint32_t val_ch0, val_ch1, val_ch2, val_ch3;

        LOG_INF("=== [Zbus State Monitor] 32 Channels Test Pattern ===");
        
        for (int step = 0; step < 8; step++) {
            /* 
             * Запрашиваем 4 канала текущего шага из Zbus.
             * На нечетных позициях (0 и 2) лежит VREF в мВ.
             * На четных позициях (1 и 3) лежит Температура в градусах.
             */
            ret  = get_channel_voltage_from_zbus(step * 4 + 0, &val_ch0);
            ret |= get_channel_voltage_from_zbus(step * 4 + 1, &val_ch1);
            ret |= get_channel_voltage_from_zbus(step * 4 + 2, &val_ch2);
            ret |= get_channel_voltage_from_zbus(step * 4 + 3, &val_ch3);

            if (ret == 0) {
                // Преобразуем беззнаковые значения температуры обратно в int32_t для красивого вывода
                int32_t temp1 = (int32_t)val_ch1;
                int32_t temp2 = (int32_t)val_ch3;

                LOG_INF("Step %d | VREF: %u mV | Temp: %d C | VREF: %u mV | Temp: %d C",
                        step, val_ch0, temp1, val_ch2, temp2);
            } else {
                LOG_WRN("Step %d | Data is not ready yet.", step);
            }
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