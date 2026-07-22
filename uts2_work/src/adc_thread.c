#include <zephyr/kernel.h>
#include "adc_thread.h"
#include <zephyr/logging/log.h>
#include "system_data_bus.h"


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

LOG_MODULE_REGISTER(seq_mux_adc_drv, LOG_LEVEL_INF);

/* Получаем указатель на наш прибор из дерева устройств */
static const struct device *const seq_dev = DEVICE_DT_GET(DT_NODELABEL(my_sequencer));


static uint8_t __attribute__((__section__("DTCM"))) adc_thread_stack[AIN_TASK_STACK_SIZE];
static struct __attribute__((__section__("DTCM"))) k_thread thread_data;


/* Функция-помощник для считывания сырого опорного VREFINT для конкретного шага */
static uint32_t get_vref_raw(const struct device *dev, uint8_t step)
{
    struct seq_mux_adc_api *api = (struct seq_mux_adc_api *)dev->api;
    uint32_t raw_vref = 0;
    // Опорное напряжение VREFINT находится на 2-м канале (индекс step * 4 + 1)
    api->get_channel_value(dev, step * 4 + 1, &raw_vref);
    return raw_vref;
}




static void adc_pub_thread_fn(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);


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

            if (step == 0) {
                msg.vdda_mv = vdda_mv;
                msg.raw_temp = raw_temp;
            }

            /* 
             * ТЕСТОВЫЙ РЕЖИМ ЗАПОЛНЕНИЯ:
             * 1-й и 3-й канал шага (индексы 0 и 2) — пишем VDDA в милливольтах
             * 2-й и 4-й канал шага (индексы 1 и 3) — пишем Температуру в градусах
             */
            msg.channels_mv[step * 4 + 0] = vdda_mv;
            msg.channels_mv[step * 4 + 1] = (uint32_t)temp_c;
            msg.channels_mv[step * 4 + 2] = vdda_mv;
            msg.channels_mv[step * 4 + 3] = (uint32_t)temp_c;

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
        zbus_chan_pub(&adc_data_chan, &msg, K_NO_WAIT);
    }
}

/* Получить калиброванное напряжение канала из Zbus */
int get_channel_voltage_from_zbus(uint8_t channel_idx, uint32_t *voltage_mv)
{
    struct adc_data_msg msg;

    if (false
        || channel_idx >= TOTAL_CHANNEL_COUNT
        || voltage_mv == NULL
        || zbus_chan_read(&adc_data_chan, &msg, K_MSEC(10)) < 0
    ) {
        return -EINVAL;
    }

    *voltage_mv = msg.channels_mv[channel_idx];
    return 0;
}

/* Получить калиброванную температуру из Zbus */
int get_channel_temp_from_zbus(uint8_t channel_idx, int32_t *temp_c)
{
    struct adc_data_msg msg;

    ARG_UNUSED(channel_idx);

    if (false
        || temp_c == NULL
        || zbus_chan_read(&adc_data_chan, &msg, K_MSEC(10)) < 0
        || msg.vdda_mv == 0
        || msg.raw_temp == 0
    ) {
        return -EINVAL;
    }

    *temp_c = __LL_ADC_CALC_TEMPERATURE(msg.vdda_mv, msg.raw_temp, LL_ADC_RESOLUTION_12B);
    return 0;
}

/* Запуск потока-издателя АЦП */
int ain_thread_start(void)
{
    // Рассчитываем коэффициенты делителей один раз при старте
    for (int i = 0; i < TOTAL_CHANNEL_COUNT; i++) {
        coefficients[i] = (float)(r1_resistors[i] + r2_resistors[i]) / (float)r2_resistors[i];
    }

    k_thread_create(&thread_data, 
                    (k_thread_stack_t *)adc_thread_stack, 
                    AIN_TASK_STACK_SIZE,
                    adc_pub_thread_fn, 
                    NULL, NULL, NULL, 
                    AIN_TASK_PRIORITY, 
                    0, 
                    K_MSEC(2000));
    return 0;
}