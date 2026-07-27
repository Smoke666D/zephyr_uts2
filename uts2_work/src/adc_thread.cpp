#include <zephyr/kernel.h>
#include "adc_thread.h"
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include "global_params.h"
#include "os.h"
#include "mediator.hpp"
/* Подключаем публичный заголовочный файл нашего драйвера */
#include <seq_mux_adc.h>


static const uint32_t r1_resistors[TOTAL_CHANNEL_COUNT] = {
    10000,  10000,  10000,  10000,  // Шаг 0 (AO1, AO7, AO13, AVsense1)
    10000,  10000,  10000,  10000,  // Шаг 1 (AO2, AO8, AO14, AVsense2)
    10000,  10000,  10000,  10000,  // Шаг 2 (AO3 ...)
    10000,  10000,  10000,  10000,  // Шаг 3
    10000,  10000,  10000,  10000,  // Шаг 4
    10000,  10000,  10000,  10000,  // Шаг 5
    47000,  47000,  47000,  47000,  // Шаг 6 (Тестовые каналы DA11..DA44 T1)
    100000, 100000, 100000, 100000  // Шаг 7 (Тестовые каналы DA11..DA44 T2)
};

static const uint32_t r2_resistors[TOTAL_CHANNEL_COUNT] = {
    10000, 10000, 10000, 10000,
    10000, 10000, 10000, 10000,
    10000, 10000, 10000, 10000,
    10000, 10000, 10000, 10000,
    10000, 10000, 10000, 10000,
    10000, 10000, 10000, 10000,
    10000, 10000, 10000, 10000,
    10000, 10000, 10000, 10000
};

static float coefficients[TOTAL_CHANNEL_COUNT];

/* 
 * ОПРЕДЕЛЯЕМ КАНАЛ.
 * Поскольку adc_monitor_sub объявлен extern в adc_zbus.h, 
 * макрос ZBUS_OBSERVERS скомпилируется успешно.
 */
ZBUS_CHAN_DEFINE(adc_data_chan,
                 struct adc_data_msg,
                 NULL, /* Валидатор */
                 NULL, /* Пользовательские данные */
                 ZBUS_OBSERVERS(), /* Список получателей */
                 ZBUS_MSG_INIT(0) /* Инициализация нулями */
);






MEDIATOR_ELEMENT_DEFINE(PARAM_VAL, ain_ch1, AIN_AO1, {0});
MEDIATOR_ELEMENT_DEFINE(PARAM_VAL, ain_ch7, AIN_AO7, {0});
MEDIATOR_ELEMENT_DEFINE(PARAM_VAL, ain_ch13, AIN_AO13, {0});
MEDIATOR_ELEMENT_DEFINE(PARAM_VAL, ain_chs1, AIN_AVsense1, {0});
MEDIATOR_ELEMENT_DEFINE(PARAM_VAL, ain_ch2, AIN_AO2, {0});
MEDIATOR_ELEMENT_DEFINE(PARAM_VAL, ain_ch8, AIN_AO8, {0});
MEDIATOR_ELEMENT_DEFINE(PARAM_VAL, ain_ch14, AIN_AO14, {0});
MEDIATOR_ELEMENT_DEFINE(PARAM_VAL, ain_chs2, AIN_AVsense2, {0});
MEDIATOR_ELEMENT_DEFINE(PARAM_VAL, ain_ch3, AIN_AO3, {0});
MEDIATOR_ELEMENT_DEFINE(PARAM_VAL, ain_ch9, AIN_AO9, {0});
MEDIATOR_ELEMENT_DEFINE(PARAM_VAL, ain_ch15, AIN_AO15, {0});
MEDIATOR_ELEMENT_DEFINE(PARAM_VAL, ain_chs3, AIN_AVsense3, {0});
MEDIATOR_ELEMENT_DEFINE(PARAM_VAL, ain_ch4, AIN_AO4, {0});
MEDIATOR_ELEMENT_DEFINE(PARAM_VAL, ain_ch10, AIN_AO10, {0});
MEDIATOR_ELEMENT_DEFINE(PARAM_VAL, ain_ch16, AIN_AO16, {0});
MEDIATOR_ELEMENT_DEFINE(PARAM_VAL, ain_chs4, AIN_AVsense4, {0});
MEDIATOR_ELEMENT_DEFINE(PARAM_VAL, ain_ch5, AIN_AO5, {0});
MEDIATOR_ELEMENT_DEFINE(PARAM_VAL, ain_ch11, AIN_AO11, {0});
MEDIATOR_ELEMENT_DEFINE(PARAM_VAL, ain_ch17, AIN_AO17, {0});
MEDIATOR_ELEMENT_DEFINE(PARAM_VAL, ain_chss5, AIN_AVsense5, {0});
MEDIATOR_ELEMENT_DEFINE(PARAM_VAL, ain_ch6, AIN_AO6, {0});
MEDIATOR_ELEMENT_DEFINE(PARAM_VAL, ain_ch12, AIN_AO12, {0});
MEDIATOR_ELEMENT_DEFINE(PARAM_VAL, ain_ch18, AIN_AO18, {0});
MEDIATOR_ELEMENT_DEFINE(PARAM_VAL, ain_chss6, AIN_AVsense6, {0});


LOG_MODULE_REGISTER(seq_mux_adc_drv, LOG_LEVEL_INF);

/* Получаем указатель на наш прибор из дерева устройств */





/* Функция-помощник для считывания сырого опорного VREFINT для конкретного шага */
static uint32_t get_vref_raw(const struct device *dev, uint8_t step)
{
    struct seq_mux_adc_api *api = (struct seq_mux_adc_api *)dev->api;
    uint32_t raw_vref = 0;
    // Опорное напряжение VREFINT находится на 2-м канале (индекс step * 4 + 1)
    api->get_channel_value(dev, step * 4 + 1, &raw_vref);
    return raw_vref;
}

class AdcTask : public os::task<AdcTask, AIN_TASK_STACK_SIZE> {
public:
    // Конструктор: инициализирует базовый класс и рассчитывает коэффициенты
    AdcTask();

    // Основной цикл задачи
    void task_func();


 
};


AdcTask::AdcTask() : os::task<AdcTask, AIN_TASK_STACK_SIZE>(
    "adc_pub", 
    static_cast<os::priority>(AIN_TASK_PRIORITY),
    os::opt::start // Не запускаем сразу, ждем вызова из ain_thread_start или системного init
) {
    // Рассчитываем коэффициенты делителей один раз
    for (int i = 0; i < TOTAL_CHANNEL_COUNT; i++) {
        coefficients[i] = (float)(r1_resistors[i] + r2_resistors[i]) / (float)r2_resistors[i];
    }
}


void AdcTask::task_func()
{
static const struct device *const seq_dev = DEVICE_DT_GET(DT_NODELABEL(my_sequencer));
  // Проверяем готовность драйвера перед началом работы
    if (!device_is_ready(seq_dev)) {
         LOG_ERR("Sequencer driver is not ready!");
        return;
    }

    /* Извлекаем интерфейс высокоуровневого API нашего драйвера */
    struct seq_mux_adc_api *api = (struct seq_mux_adc_api *)seq_dev->api;
    struct adc_data_msg msg; 
    
    LOG_INF("Starting ADC3 Real-Time Publisher Thread...");
    k_sleep(K_MSEC(2000));
    
     while (1) {
        // Ждем готовности на семафоре драйвера (весь цикл 32 пересылок окончен)
        int ret = api->wait_for_data(seq_dev, K_FOREVER);
        if (ret < 0) {
            LOG_ERR("Failed to wait for data! Error: %d", ret);
            continue;
        }

        // Выполняем тестовое заполнение буфера
        for (int step = 0; step < 8; step++) {
            uint32_t raw_vref = get_vref_raw(seq_dev, step);
            uint32_t raw_temp = 0;
            api->get_channel_value(seq_dev, step * 4 + 0, &raw_temp); // Датчик температуры — на 1 канале шага (индекс step * 4 + 0)

            // Защита от деления на ноль при неготовности данных
            if (raw_vref == 0 || raw_temp == 0) {
                continue;
            }

            // Вычисляем VDDA (VREF) и Температуру для текущего шага мультиплексора
            uint32_t vdda_mv = __LL_ADC_CALC_VREFANALOG_VOLTAGE(raw_vref, LL_ADC_RESOLUTION_12B);
            int32_t temp_c = __LL_ADC_CALC_TEMPERATURE(vdda_mv, raw_temp, LL_ADC_RESOLUTION_12B);

            uint32_t temp_val = (uint32_t)(temp_c < 0 ? -temp_c : temp_c);


            if (step == 0) {
                msg.vdda_mv = vdda_mv;
                msg.raw_temp = raw_temp;
            }

            PARAM_VAL mediator_msg_mv;
            mediator_msg_mv.value.integer =  vdda_mv;
            PARAM_VAL mediator_msg_tmp;
            mediator_msg_tmp.value.integer = temp_c;
            
           
                    os::mediator((PARAM_ID)(AIN_AO1 + step * 4 + 1)).set(mediator_msg_tmp);
                    os::mediator((PARAM_ID)(AIN_AO1 + step * 4 + 3)).set(mediator_msg_tmp);                  
                    os::mediator((PARAM_ID)(AIN_AO1 + step * 4 + 0) ).set(mediator_msg_mv);
                    os::mediator((PARAM_ID)(AIN_AO1 + step * 4 + 2 )).set(mediator_msg_mv);
              
            
            /* 
             * РЕАЛЬНЫЙ КОД ДЛЯ ПРИВЕДЕНИЯ К НАПРЯЖЕНИЮ (закомментирован для тестов по запросу):
             *
             * for (int ch = 0; ch < 4; ch++) {
             *     uint8_t ch_idx = step * 4 + ch;
             *     uint32_t raw_val = 0;
             *     api->get_channel_value(seq_dev, ch_idx, &raw_val);
             *     float v_pin_mv = (float)(raw_val * vdda_mv) / 4095.0f;
             *     msg.channels_mv[ch_idx] = (uint32_t)(v_pin_mv * coefficients[ch_idx]);
             * }
             */
        }

        // Публикуем тестовый 32-канальный пакет в Zbus
        //zbus_chan_pub(&adc_data_chan, &msg, K_NO_WAIT);
    }
}

// РАЗМЕЩЕНИЕ В DTCM: Создаем статический объект задачи в секции быстрой памяти
static AdcTask adc_task_inst;