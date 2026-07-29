#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/zbus/zbus.h>

#include "seq_adc_processor.h"
#include "system_bridge.h"
#include "seq_mux_adc.h"


// Определение единого канала обработанных данных
ZBUS_CHAN_DEFINE(adc_processed_chan,
                 adc_processed_msg_t,
                 NULL,                 /* Без валидатора */
                 NULL,                 /* Без метаданных */
                 ZBUS_OBSERVERS_EMPTY, /* Список статических наблюдателей пуст */
                 ZBUS_MSG_INIT(0)      /* Инициализация нулями */
);


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

/* Функция-помощник для считывания сырого опорного VREFINT для конкретного шага */
static uint32_t get_vref_raw(const struct device *dev, uint8_t step)
{
    struct seq_mux_adc_api *api = (struct seq_mux_adc_api *)dev->api;
    uint32_t raw_vref = 0;
    // Опорное напряжение VREFINT находится на 2-м канале (индекс step * 4 + 1)
    api->get_channel_value(dev, step * 4 + 1, &raw_vref);
    return raw_vref;
}

/**
 *  @brief      Основная рабочая функция (выполняется в потоке воркера)
 *  @details    Считывает сырые данные из zbus-канала драйвера АЦП, 
 *              пересчитывает в напряжения и публикует результат в выходные каналы.
 *
 *  @param      arg - Указатель на zbus-канал драйвера АЦП
 */
static void adc_processing_handler(void *arg)
{
    const struct zbus_channel *driver_chan = (const struct zbus_channel *)arg;
    seq_mux_adc_msg_t raw_msg;
    

    static const struct device *const seq_dev = DEVICE_DT_GET(DT_NODELABEL(my_sequencer));
  // Проверяем готовность драйвера перед началом работы
    if (!device_is_ready(seq_dev)) {
         
        return;
    }
 /* Извлекаем интерфейс высокоуровневого API нашего драйвера */
    struct seq_mux_adc_api *api = (struct seq_mux_adc_api *)seq_dev->api;
    struct adc_data_msg msg; 

    // Извлекаем сырые данные из канала драйвера АЦП
    int ret = zbus_chan_read(driver_chan, &raw_msg, K_NO_WAIT);
    if (ret < 0) 
    {
        return;
    }

    adc_processed_msg_t processed_msg;

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

            /* 
             * ТЕСТОВЫЙ РЕЖИМ ЗАПОЛНЕНИЯ:
             * 1-й и 3-й канал шага (индексы 0 и 2) — пишем VDDA в милливольтах
             * 2-й и 4-й канал шага (индексы 1 и 3) — пишем Температуру в градусах
             */
            processed_msg.voltages[step * 4 + 0] =vdda_mv;
            processed_msg.voltages[step * 4 + 1] =temp_val;
            processed_msg.voltages[step * 4 + 2] =vdda_mv;
            processed_msg.voltages[step * 4 + 3] =temp_val;



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
    
        (void)zbus_chan_pub(&adc_processed_chan, &processed_msg, K_NO_WAIT);



    // Приведение сырых отсчетов к напряжениям (для 16-битного АЦП и Vref = 3.3V)
   /* for (int i = 0; i < TOTAL_CHANNELS_CNT; i++) 
    {
        float voltage = ((float)raw_msg.data[i] * 3.3f) / 65535.0f;
        
        // Публикация в соответствующий физический канал
        (void)zbus_chan_pub(adc_out_channels[i], &voltage, K_NO_WAIT);
    }*/
}

/**
 *  @brief      Слушатель обновлений канала драйвера АЦП
 *  @details    Вызывается в контексте публикации (в данном случае — в ISR DMA). 
 *              Быстро перенаправляет задачу на асинхронную обработку воркеру.
 *
 *  @param      chan - Канал, в который была произведена публикация
 */
static void adc_listener_callback(const struct zbus_channel *chan)
{
    app_task_t task = {
        .handler = adc_processing_handler,
        .arg     = (void *)chan,
    };
    
    // Передаем работу в воркер
    (void)task_worker_submit(&task);
}

// Статическая декларация слушателя ZBus
ZBUS_LISTENER_DEFINE(adc_listener, adc_listener_callback);

/**
 *  @brief      Инициализация модуля обработки АЦП
 *  @details    Вызывается ядром на этапе APPLICATION после запуска планировщика. 
 *              Динамически подключает слушатель к каналу АЦП-драйвера.
 */
static int adc_processor_init(void)
{
    const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(my_sequencer));
    if (!device_is_ready(dev)) 
    {
        return -ENODEV;
    }

    const struct seq_mux_adc_api *api = (const struct seq_mux_adc_api *)dev->api;
    if (api == NULL || api->get_channel == NULL) 
    {
        return -EINVAL;
    }

    // Получаем ссылку на канал драйвера
    const struct zbus_channel *driver_chan = api->get_channel(dev);
    if (driver_chan == NULL) 
    {
        return -EINVAL;
    }

    // Динамически регистрируем нашего слушателя на канале драйвера
    int ret = zbus_chan_add_obs(driver_chan, &adc_listener, K_MSEC(100));
    if (ret < 0) 
    {
        return ret;
    }

    // Рассчитываем коэффициенты делителей один раз
    for (int i = 0; i < TOTAL_CHANNEL_COUNT; i++) {
        coefficients[i] = (float)(r1_resistors[i] + r2_resistors[i]) / (float)r2_resistors[i];
    }

    return 0;
}

// Регистрация в ядре (APPLICATION выполняется перед вызовом main, когда все службы ядра готовы)
SYS_INIT(adc_processor_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);