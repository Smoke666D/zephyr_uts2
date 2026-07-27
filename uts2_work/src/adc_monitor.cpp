#include "global_params.h"
#include "os.h"
#include "mediator.hpp"
#include <zephyr/logging/log.h>
#include "param_server.h"
#include "system_data_bus.h"

LOG_MODULE_REGISTER(adc_mon, LOG_LEVEL_INF);

/**
 * @brief Класс монитора АЦП.
 * Наследуемся от os::task, используя CRTP (передаем сам класс как первый параметр шаблона).
 * Стек 2048 байт зашит в шаблон.
 */
class AdcMonitorTask : public os::task<AdcMonitorTask, 2048> {
private:
    // Список параметров выносим в статические данные класса
    static inline const uint8_t param_names[] = {
        AIN_AO1, AIN_AO7, AIN_AO13, AIN_AVsense1,
        AIN_AO2, AIN_AO8, AIN_AO14, AIN_AVsense2,    
        AIN_AO3, AIN_AO9, AIN_AO15, AIN_AVsense3,        
        AIN_AO4, AIN_AO10, AIN_AO16, AIN_AVsense4,
        AIN_AO5, AIN_AO11, AIN_AO17, AIN_AVsense5,
        AIN_AO6, AIN_AO12, AIN_AO18, AIN_AVsense6,
        AIN_DA11_test1, AIN_DA20_test1, AIN_DA33_test1, AIN_DA44_test1,
        AIN_DA11_test2, AIN_DA20_test2, AIN_DA33_test2, AIN_DA44_test2, 
    };

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
       // auto adc = os::mediator::access<PARAM_VAL>(AIN_AO1);
        while (true)
         {
            // Используем метод sleep из базового класса (обертка над k_msleep)
            sleep(3000);

            PARAM_VAL val[4];
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