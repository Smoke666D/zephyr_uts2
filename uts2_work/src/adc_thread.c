#include <zephyr/kernel.h>
#include "adc_thread.h"
#include <zephyr/logging/log.h>
#include "system_data_bus.h"
#include "global_params.h"

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

static const AIN_MUX_CHANNEL_NUMBER param_to_adc_map[] = {
    [AIN_AO1]        = AO1,
    [AIN_AO7]        = AO7,
    [AIN_AO13]       = AO13,
    [AIN_AVsense1]   = AVsense1,
    [AIN_AO2]        = AO2,
    [AIN_AO8]        = AO8,
    [AIN_AO14]       = AO14,
    [AIN_AVsense2]   = AVsense2,
    [AIN_AO3]        = AO3,
    [AIN_AO9]        = AO9,
    [AIN_AO15]       = AO15,
    [AIN_AVsense3]   = AVsense3,
    [AIN_AO4]        = AO4,
    [AIN_AO10]       = AO10,
    [AIN_AO16]       = AO16,
    [AIN_AVsense4]   = AVsense4,
    [AIN_AO5]        = AO5,
    [AIN_AO11]       = AO11,
    [AIN_AO17]       = AO17,
    [AIN_AVsense5]   = AVsense5,
    [AIN_AO6]        = AO6,
    [AIN_AO12]       = AO12,
    [AIN_AO18]       = AO18,
    [AIN_AVsense6]   = AVsense6,
    [AIN_DA11_test1] = DA11_test1,
    [AIN_DA20_test1] = DA20_test1,
    [AIN_DA33_test1] = DA33_test1,
    [AIN_DA44_test1] = DA44_test1,
    [AIN_DA11_test2] = DA11_test2,
    [AIN_DA20_test2] = DA20_test2,
    [AIN_DA33_test2] = DA33_test2,
    [AIN_DA44_test2] = DA44_test2,
};

static int adc_param_get(PARAM_ID id, PARAM_VAL *val)
{
    /* Проверяем, что запрашиваемый ID входит в диапазон наших аналоговых каналов */
    if (id >= AIN_AO1 && id <= AIN_DA44_test2) {
        struct adc_data_msg adc_msg;
        
        /* Читаем последние кэшированные данные из канала Zbus */
        int err = zbus_chan_read(&adc_data_chan, &adc_msg, K_NO_WAIT);
        if (err == 0) {
            /* Находим физический канал АЦП по логическому ID */
            AIN_MUX_CHANNEL_NUMBER adc_chan = param_to_adc_map[id];
            
            /* Преобразуем милливольты в Вольты и сохраняем в вещественное поле */
            val->value.integer= adc_msg.channels_mv[adc_chan];
        }
        return err;
    }
    
    return -ENOTSUP;
}

PARAM_ROUTE_RO(AIN_AO1,        adc_param_get);
PARAM_ROUTE_RO(AIN_AO7,        adc_param_get);
PARAM_ROUTE_RO(AIN_AO13,       adc_param_get);
PARAM_ROUTE_RO(AIN_AVsense1,   adc_param_get);
PARAM_ROUTE_RO(AIN_AO2,        adc_param_get);
PARAM_ROUTE_RO(AIN_AO8,        adc_param_get);
PARAM_ROUTE_RO(AIN_AO14,       adc_param_get);
PARAM_ROUTE_RO(AIN_AVsense2,   adc_param_get);
PARAM_ROUTE_RO(AIN_AO3,        adc_param_get);
PARAM_ROUTE_RO(AIN_AO9,        adc_param_get);
PARAM_ROUTE_RO(AIN_AO15,       adc_param_get);
PARAM_ROUTE_RO(AIN_AVsense3,   adc_param_get);
PARAM_ROUTE_RO(AIN_AO4,        adc_param_get);
PARAM_ROUTE_RO(AIN_AO10,       adc_param_get);
PARAM_ROUTE_RO(AIN_AO16,       adc_param_get);
PARAM_ROUTE_RO(AIN_AVsense4,   adc_param_get);
PARAM_ROUTE_RO(AIN_AO5,        adc_param_get);
PARAM_ROUTE_RO(AIN_AO11,       adc_param_get);
PARAM_ROUTE_RO(AIN_AO17,       adc_param_get);
PARAM_ROUTE_RO(AIN_AVsense5,   adc_param_get);
PARAM_ROUTE_RO(AIN_AO6,        adc_param_get);
PARAM_ROUTE_RO(AIN_AO12,       adc_param_get);
PARAM_ROUTE_RO(AIN_AO18,       adc_param_get);
PARAM_ROUTE_RO(AIN_AVsense6,   adc_param_get);
PARAM_ROUTE_RO(AIN_DA11_test1, adc_param_get);
PARAM_ROUTE_RO(AIN_DA20_test1, adc_param_get);
PARAM_ROUTE_RO(AIN_DA33_test1, adc_param_get);
PARAM_ROUTE_RO(AIN_DA44_test1, adc_param_get);
PARAM_ROUTE_RO(AIN_DA11_test2, adc_param_get);
PARAM_ROUTE_RO(AIN_DA20_test2, adc_param_get);
PARAM_ROUTE_RO(AIN_DA33_test2, adc_param_get);
PARAM_ROUTE_RO(AIN_DA44_test2, adc_param_get);




LOG_MODULE_REGISTER(seq_mux_adc_drv, LOG_LEVEL_INF);

/* Получаем указатель на наш прибор из дерева устройств */
static const struct device *const seq_dev = DEVICE_DT_GET(DT_NODELABEL(my_sequencer));


static uint8_t __attribute__((__section__("DTCM"))) adc_thread_stack[AIN_TASK_STACK_SIZE];
static struct  k_thread thread_data; 


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
            msg.channels_mv[step * 4 + 0] = vdda_mv;
            msg.channels_mv[step * 4 + 1] = temp_val;
            msg.channels_mv[step * 4 + 2] = vdda_mv;
            msg.channels_mv[step * 4 + 3] = temp_val;

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