#include "adc_mux_processor.hpp"
#include "coorutines.hpp" // Теперь подключаем полную реализацию корутин для компиляции
#include <zephyr/logging/log.h>
#include "seq_adc_processor.h"


LOG_MODULE_REGISTER(adc_mux_processor, LOG_LEVEL_INF);
// Объявляем, что физически очередь сообщений находится в другом месте (в main.cpp)

// 1. Создаем обычную статическую функцию для вывода лога старта.
// Так как это не корутина, компилятор GCC соберет макрос LOG_INF идеально!
static void log_daemon_started() {
    LOG_INF("Sensor bridge daemon started successfully in separate module!");
}

// 2. Создаем обычную функцию для вывода лога данных
static void log_sensor_data(uint16_t adc_val) {
    LOG_INF("Successfully processed ADC value: %u", adc_val);
}


extern struct k_msgq drv_sensor_msgq;


// Определение единого канала обработанных данных
ZBUS_CHAN_DEFINE(adc_processed_chan,
                 adc_processed_msg_t,
                 NULL,                 /* Без валидатора */
                 NULL,                 /* Без метаданных */
                 ZBUS_OBSERVERS_EMPTY, /* Список статических наблюдателей пуст */
                 ZBUS_MSG_INIT(0)      /* Инициализация нулями */
);

// Реализация корутины
os::DetachedTask<SensorDispatcher> sensor_bridge_daemon(SensorDispatcher& disp) 
{

    seq_mux_adc_msg_t temp_msgq;
    log_daemon_started();

    while (true) {
        // Асинхронно ждем данные из очереди через лаконичный метод диспетчера
        co_await disp.wait_queue(&drv_sensor_msgq, temp_msgq);
        (void)zbus_chan_pub(&adc_processed_chan, &temp_msgq, K_NO_WAIT);
        // Просто выводим полученное значение для проверки работы
        // LOG_INF("Successfully processed ADC value: %u", raw.adc_val);
    }
}



