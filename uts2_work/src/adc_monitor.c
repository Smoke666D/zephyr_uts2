#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "param_server.h"

/* Подключаем наш общий Zbus заголовок */
#include "system_data_bus.h"

LOG_MODULE_REGISTER(adc_mon, LOG_LEVEL_INF);

/* 
 * ОПРЕДЕЛЯЕМ ПОДПИСЧИКА.
 * Это создаст очередь сообщений для нашего монитора.
 */
static const uint8_t const param_names[] = {
    AIN_AO1,
    AIN_AO7,
    AIN_AO13,
    AIN_AVsense1,
    AIN_AO2,
    AIN_AO8,
    AIN_AO14,
    AIN_AVsense2,    
    AIN_AO3,
    AIN_AO9,
    AIN_AO15,
    AIN_AVsense3,        
    AIN_AO4,
    AIN_AO10,
    AIN_AO16,
    AIN_AVsense4,
    AIN_AO5,
    AIN_AO11,
    AIN_AO17,
    AIN_AVsense5,
    AIN_AO6,                    
    AIN_AO12,                
    AIN_AO18,
    AIN_AVsense6,
    AIN_DA11_test1,
    AIN_DA20_test1,
    AIN_DA33_test1,
    AIN_DA44_test1,
    AIN_DA11_test2,
    AIN_DA20_test2,
    AIN_DA33_test2,
    AIN_DA44_test2, 
};


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
        PARAM_VAL val_ch0, val_ch1, val_ch2, val_ch3;

        LOG_INF("=== [Zbus State Monitor] 32 Channels Test Pattern ===");
        
        for (int step = 0; step < 8; step++) {
            /* 
             * Запрашиваем 4 канала текущего шага из Zbus.
             * На нечетных позициях (0 и 2) лежит VREF в мВ.
             * На четных позициях (1 и 3) лежит Температура в градусах.
             */

             
            ret  = param_get(param_names[step * 4 + 0], &val_ch0);
            ret  = param_get(param_names[step * 4 + 1], &val_ch1);
            ret  = param_get(param_names[step * 4 + 2], &val_ch2);
            ret  = param_get(param_names[step * 4 + 3], &val_ch3);
            

            if (ret == 0) {
                /*
                 * Извлекаем значения из поля .value.integer нашей общей структуры PARAM_VAL.
                 * На нечетных позициях (ch0, ch2) лежит напряжение (uint32_t).
                 * На четных позициях (ch1, ch3) лежит температура (int32_t).
                 */
                uint32_t vref1 = (uint32_t)val_ch0.value.integer;
                int32_t temp1  = (int32_t)val_ch1.value.integer;
                uint32_t vref2 = (uint32_t)val_ch2.value.integer;
                int32_t temp2  = (int32_t)val_ch3.value.integer;

                LOG_INF("Step %d | VREF: %u mV | Temp: %d C | VREF: %u mV | Temp: %d C",
                        step, vref1, temp1, vref2, temp2);
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