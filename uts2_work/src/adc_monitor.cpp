#include "global_params.h"
#include "os.h"
#include "mediator.hpp"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(adc_mon, LOG_LEVEL_INF);

/**
 * @brief Класс монитора АЦП.
 * Наследуемся от os::task, используя CRTP (передаем сам класс как первый параметр шаблона).
 * Стек 2048 байт зашит в шаблон.
 */
class AdcMonitorTask : public os::task<AdcMonitorTask, 2048> {
private:


public:
    // Конструктор инициализирует базовый класс (имя задачи, приоритет)
    AdcMonitorTask() : os::task<AdcMonitorTask, 2048>(
        "adc_mon", 
        os::priority::low, // Приоритет 11 в Си-коде соответствует low/normal
        os::opt::start     // Запускать сразу после создания объекта
    ) {}

    /**
     * @brief Основная логика задачи.
     * Аналог функции adc_monitor_thread_fn.
     */
    void task_func() 
    {
        LOG_INF("Starting State-Reader ADC Monitor (C++ Task)...");
        while (true)
         {
            // Используем метод sleep из базового класса (обертка над k_msleep)
            sleep(3000);
            
            LOG_INF("=== [Zbus State Monitor] 32 Channels Test Pattern ===");
            
            for (int step = 0; step < 8; step++) 
            {

                    LOG_INF("Step %d | VREF: %u mV | Temp: %d C | VREF: %u mV | Temp: %d C",
                            step, 
                            os::mediator((PARAM_ID)(AIN_AO1 + step * 4)).get().value.integer,
                            os::mediator((PARAM_ID)(AIN_AO1 + step * 4 + 1)).get().value.integer,
                            os::mediator((PARAM_ID)(AIN_AO1 + step * 4 + 2)).get().value.integer,
                            os::mediator((PARAM_ID)(AIN_AO1 + step * 4 + 3)).get().value.integer);

            }
        
        }
    }
};

// Глобальный (или статический) экземпляр задачи. 
// В C++ конструктор вызовется автоматически, и поток запустится сам.
static AdcMonitorTask adc_monitor_inst __attribute__((section(".dtcm_bss")));;