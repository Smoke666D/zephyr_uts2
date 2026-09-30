#include "adc_thread.h"
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include "system_bus_model.h"
#include <zephyr/kernel.h>

/* Подключаем публичный заголовочный файл нашего драйвера */
#include <seq_mux_adc.h>


static const uint32_t r1_resistors[TOTAL_CHANNEL_COUNT] = {
    10000,  10000,  10000,  10000, 10000, // Шаг 0 (AO1, AO7, AO13, AVsense1)
    10000,  10000,  10000,  10000,  10000,// Шаг 1 (AO2, AO8, AO14, AVsense2)
    10000,  10000,  10000,  10000,  10000,// Шаг 2 (AO3 ...)
    10000,  10000,  10000,  10000, 10000,  // Шаг 3
    10000,  10000,  10000,  10000, 0,  // Шаг 4
    10000,  10000,  10000,  10000,  0,// Шаг 5
    0,  0,  0,  0, 0, // Шаг 6 (Тестовые каналы DA11..DA44 T1)
    0, 0, 0, 0,0  // Шаг 7 (Тестовые каналы DA11..DA44 T2)
};

static const uint32_t r2_resistors[TOTAL_CHANNEL_COUNT] = {
    69000, 69000, 69000, 10000, 69000,
    69000, 69000, 69000, 10000, 69000,
    69000, 69000, 69000, 10000,69000,
    69000, 69000, 69000, 10000,69000,
    69000, 69000, 69000, 10000,1,
    69000, 69000, 69000, 10000,1,
    1, 1, 1, 1,1,
    1, 1, 1, 1,1
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


/* Функция-помощник для считывания сырого опорного VREFINT для конкретного шага */
static uint32_t get_vref_raw(const struct device *dev, uint8_t step)
{
    struct seq_mux_adc_api *api = (struct seq_mux_adc_api *)dev->api;
    uint32_t raw_vref = 0;
    // Опорное напряжение VREFINT находится на 2-м канале (индекс step * 4 + 1)
    api->get_channel_value(dev, step * 4 + 1, &raw_vref);
    return raw_vref;
}




static void _adc_init()
{
    // Рассчитываем коэффициенты делителей один раз
    for (int i = 0; i < TOTAL_CHANNEL_COUNT; i++) {
        coefficients[i] =  (float)r2_resistors[i]  /(float)(r1_resistors[i] + r2_resistors[i]);
    }
}


static void my_custom_thread_entry(void *p1, void *p2, void *p3)
{
   static const struct device *const seq_dev = DEVICE_DT_GET(DT_NODELABEL(my_sequencer));
  // Проверяем готовность драйвера перед началом работы
    if (!device_is_ready(seq_dev)) {
         LOG_ERR("Sequencer driver is not ready!");
     return;
    }
    
    
    _adc_init();
    struct seq_mux_adc_api *api = (struct seq_mux_adc_api *)seq_dev->api;
    struct adc_data_msg msg; 
    /* Извлекаем интерфейс высокоуровневого API нашего драйвера */
    //struct seq_mux_adc_api *api = (struct seq_mux_adc_api *)seq_dev->api;
   // struct adc_data_msg msg; 
    k_msleep(2000);
    
   
    
     while (1) 
     {
         // Ждем готовности на семафоре драйвера (весь цикл 32 пересылок окончен)
        int ret = api->wait_for_data(seq_dev, K_FOREVER);
        if (ret < 0) {
            LOG_ERR("Failed to wait for data! Error: %d", ret);
            continue;
        }

        // Выполняем тестовое заполнение буфера
        for (int step = 0; step < ADC_SAMPLES_PER_CH; step++) {
           
            
              for (int ch = 0; ch < ADC_NUM_CHANNELS; ch++) {
                 uint8_t ch_idx = step * ADC_NUM_CHANNELS + ch;
                  uint32_t raw_val = 0;
                  api->get_channel_value(seq_dev, ch_idx, &raw_val);
                  float v_pin_mv = (float)raw_val * (3.3 / 65535.0f);
                 msg.channels_mv[ch_idx] =(v_pin_mv * coefficients[ch_idx]);
             }
             
        }

        // Публикуем тестовый 32-канальный пакет в Zbus
        zbus_chan_pub(&adc_data_chan, &msg, K_NO_WAIT);
    }
}




K_THREAD_DEFINE(my_thread_id, AIN_TASK_STACK_SIZE, my_custom_thread_entry, 
            NULL, NULL, NULL, 
                7, 0, 0);







PARAM_ROUTE_DEFINE(AIN_AO1,&adc_data_chan,0,ARRAY_DATA);
PARAM_ROUTE_DEFINE(AIN_AO7,&adc_data_chan,1,ARRAY_DATA);
PARAM_ROUTE_DEFINE(AIN_AO13,&adc_data_chan,2,ARRAY_DATA);
PARAM_ROUTE_DEFINE(AIN_AVsense1,&adc_data_chan,3,ARRAY_DATA);
PARAM_ROUTE_DEFINE(LIN_AO1,&adc_data_chan,4,ARRAY_DATA);
PARAM_ROUTE_DEFINE(AIN_AO2,&adc_data_chan,5,ARRAY_DATA);
PARAM_ROUTE_DEFINE(AIN_AO8,&adc_data_chan,6,ARRAY_DATA);
PARAM_ROUTE_DEFINE(AIN_AO14,&adc_data_chan,7,ARRAY_DATA);
PARAM_ROUTE_DEFINE(AIN_AVsense2,&adc_data_chan,8,ARRAY_DATA);
PARAM_ROUTE_DEFINE(LIN_AO2,&adc_data_chan,9,ARRAY_DATA);
PARAM_ROUTE_DEFINE(AIN_AO3,&adc_data_chan,10,ARRAY_DATA);
PARAM_ROUTE_DEFINE(AIN_AO9,&adc_data_chan,11,ARRAY_DATA);
PARAM_ROUTE_DEFINE(AIN_AO15,&adc_data_chan,12,ARRAY_DATA);
PARAM_ROUTE_DEFINE(AIN_AVsense3,&adc_data_chan,13,ARRAY_DATA);
PARAM_ROUTE_DEFINE(LIN_AO3,&adc_data_chan,14,ARRAY_DATA);
PARAM_ROUTE_DEFINE(AIN_AO4,&adc_data_chan,15,ARRAY_DATA);
PARAM_ROUTE_DEFINE(AIN_AO10,&adc_data_chan,16,ARRAY_DATA);
PARAM_ROUTE_DEFINE(AIN_AO16,&adc_data_chan,17,ARRAY_DATA);
PARAM_ROUTE_DEFINE(AIN_AVsense4,&adc_data_chan,18,ARRAY_DATA);
PARAM_ROUTE_DEFINE(LIN_AO4,&adc_data_chan,19,ARRAY_DATA);
PARAM_ROUTE_DEFINE(AIN_AO5,&adc_data_chan,20,ARRAY_DATA);
PARAM_ROUTE_DEFINE(AIN_AO11,&adc_data_chan,21,ARRAY_DATA);
PARAM_ROUTE_DEFINE(AIN_AO17,&adc_data_chan,22,ARRAY_DATA);
PARAM_ROUTE_DEFINE(AIN_AVsense5,&adc_data_chan,23,ARRAY_DATA);
PARAM_ROUTE_DEFINE(DA41_GND1,&adc_data_chan,24,ARRAY_DATA);
PARAM_ROUTE_DEFINE(AIN_AO6,&adc_data_chan,25,ARRAY_DATA);
PARAM_ROUTE_DEFINE(AIN_AO12,&adc_data_chan,26,ARRAY_DATA);
PARAM_ROUTE_DEFINE(AIN_AO18,&adc_data_chan,27,ARRAY_DATA);
PARAM_ROUTE_DEFINE(AIN_AVsense6,&adc_data_chan,28,ARRAY_DATA);
PARAM_ROUTE_DEFINE(DA41_GND2,&adc_data_chan,29,ARRAY_DATA);
PARAM_ROUTE_DEFINE(AIN_DA11_test1,&adc_data_chan,30,ARRAY_DATA);
PARAM_ROUTE_DEFINE(AIN_DA20_test1,&adc_data_chan,31,ARRAY_DATA);
PARAM_ROUTE_DEFINE(AIN_DA33_test1,&adc_data_chan,32,ARRAY_DATA);
PARAM_ROUTE_DEFINE(AIN_DA44_test1,&adc_data_chan,33,ARRAY_DATA);
PARAM_ROUTE_DEFINE(AIN_DA41_test1,&adc_data_chan,34,ARRAY_DATA);
PARAM_ROUTE_DEFINE(AIN_DA11_test2,&adc_data_chan,35,ARRAY_DATA);
PARAM_ROUTE_DEFINE(AIN_DA20_test2,&adc_data_chan,36,ARRAY_DATA);
PARAM_ROUTE_DEFINE(AIN_DA33_test2,&adc_data_chan,37,ARRAY_DATA);
PARAM_ROUTE_DEFINE(AIN_DA44_test2,&adc_data_chan,38,ARRAY_DATA);
PARAM_ROUTE_DEFINE(AIN_DA41_test2,&adc_data_chan,39,ARRAY_DATA);
 
 
 
