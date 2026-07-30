#pragma once
#include <cstddef>
#include <stdint.h>

// МЫ НЕ ПОДКЛЮЧАЕМ "coorutines.hpp" здесь!

namespace os {
    // Предварительное объявление шаблона Dispatcher в пространстве имен os
    template <int MaxEvents, std::size_t PoolSize, std::size_t StackSize>
    class Dispatcher;

    // Предварительное объявление шаблона DetachedTask в пространстве имен os
    template <typename DispatcherType>
    struct DetachedTask;
}

// Конфигурируем и объявляем тип нашего диспетчера SensorDispatcher.
// 1088 байт стека = 1024 базовый стек + 4 события * 16 байт под k_poll_event.
using SensorDispatcher = os::Dispatcher<4, 2048, 1088>;

// Структура сырых данных
struct SensorRawData {
    uint16_t adc_val;
};

// Объявляем сигнатуру нашей функции корутины-моста
os::DetachedTask<SensorDispatcher> sensor_bridge_daemon(SensorDispatcher& disp);