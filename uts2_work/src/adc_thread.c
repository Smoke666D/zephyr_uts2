#include "adc_thread.h"
#include <stdint.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include "system_bus_model.h"
#include <zephyr/kernel.h>
/* Подключаем публичный заголовочный файл нашего драйвера */
#include <seq_mux_adc.h>
#include <scan_cont_adc.h>
#include "filters.h"
#include "app_worker.h"

#define AIN_TASK_STACK_SIZE 2024
#define AIN_TASK_PRIORITY 1


static const uint32_t r1_resistors[TOTAL_CHANNEL_COUNT] = {
    69000,  69000,  69000,  10000, 69000, // Шаг 0 (AO1, AO7, AO13, AVsense1)
    69000,  69000, 69000,  10000,  69000,// Шаг 1 (AO2, AO8, AO14, AVsense2)
    69000,  69000,  69000,  10000,  69000,// Шаг 2 (AO3 ...)
    69000,  69000,  69000,  10000, 69000,  // Шаг 3
    69000,  69000,  69000,  10000, 0,  // Шаг 4
    69000,  69000,  69000,  10000,  0,// Шаг 5
    0,  0,  0,  0, 0, // Шаг 6 (Тестовые каналы DA11..DA44 T1)
    0, 0, 0, 0,0  // Шаг 7 (Тестовые каналы DA11..DA44 T2)
};

static const uint32_t r2_resistors[TOTAL_CHANNEL_COUNT] = {
    10000, 10000, 10000, 10000, 10000,
    10000, 10000, 10000, 10000, 10000,
    10000, 10000, 10000, 10000,10000,
    10000, 10000, 10000, 10000,10000,
    10000, 10000, 10000, 10000,1,
    10000, 10000, 10000, 10000,1,
    1, 1, 1, 1,1,
    1, 1, 1, 1,1
};


static const uint32_t r1_resistors1[ADC2_BUF_SIZE] = {
    10000,  10000,  100000,  124000, 124000, // Шаг 0 (AO1, AO7, AO13, AVsense1)
    10000,  10000,  10000  
};

static const uint32_t r2_resistors1[ADC2_BUF_SIZE] = {
    20000, 10000, 12000, 10000, 6800,
    10000, 20000, 20000
};

static float _sense_coof[TOTAL_CHANNEL_COUNT] __attribute__((section("DTCM")));;
static float _envariemen_coof[ADC2_BUF_SIZE] __attribute__((section("DTCM")));;
channel_median_filter_t _envariemen_filters[ADC2_BUF_SIZE] __attribute__((section("DTCM")));
channel_median_filter_t _sense_filters[TOTAL_CHANNEL_COUNT] __attribute__((section("DTCM")));
static struct k_sem *_scan_adc_sem;
static struct k_sem *_sec_mux_adc_sem;

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


ZBUS_CHAN_DEFINE(adc2_data_chan,
                 struct adc_scan_msg,
                 NULL, /* Валидатор */
                 NULL, /* Пользовательские данные */
                 ZBUS_OBSERVERS(), /* Список получателей */
                 ZBUS_MSG_INIT(0) /* Инициализация нулями */
);


LOG_MODULE_REGISTER(seq_mux_adc_drv, LOG_LEVEL_INF);



static void _sense_adc_init()
{
    // Рассчитываем коэффициенты делителей один раз
    for (int i = 0; i < TOTAL_CHANNEL_COUNT; i++) {
        _sense_coof[i] =   (float)(r1_resistors[i] + r2_resistors[i])/(float)r2_resistors[i] ;
        _sense_coof[i] *=  (3.3f / 65535.0f);
        _sense_filters[i].head = 0;
        memset(_sense_filters[i].history,0,sizeof(uint16_t)*MEDIAN_WINDOW_SIZE);
    }
}

static const struct device *const scan_dev = DEVICE_DT_GET(DT_NODELABEL(scan_adc));

static const struct device *const seq_dev = DEVICE_DT_GET(DT_NODELABEL(my_sequencer));
static struct k_work_poll _envariemen_adc_monitor_work;
static struct k_poll_event _envariemen_adc_poll_events[1];
static struct k_work_poll _sense_adc_monitor_work;
static struct k_poll_event _sense_adc_poll_events[1];

static void _envariemen_adc_init()
{
    // Рассчитываем коэффициенты делителей один раз
    for (int i = 0; i < ADC2_BUF_SIZE; i++) 
    {
        _envariemen_coof[i] =  (float)(r1_resistors1[i] + r2_resistors1[i])/(float)r2_resistors1[i];
        _envariemen_coof[i] *=  (3.3f / 65535.0f);
        _envariemen_filters[i].head = 0;
        memset(_envariemen_filters[i].history,0,sizeof(uint16_t)*MEDIAN_WINDOW_SIZE);
    }
}


static void _envariement_listener_handler(struct k_work *work)
{    

    struct adc_scan_msg msg; 
    uint16_t raw_buf[ADC2_BUF_SIZE];

    k_sem_reset(_scan_adc_sem);
    scan_cont_adc_get_buf_copy(scan_dev,raw_buf);
    for (int channel = 0; channel < ADC2_BUF_SIZE; channel++) 
    {                      
        float v_pin_mv =(float)apply_median_filter_5(&_envariemen_filters[channel], raw_buf[channel]);
          msg.channels_mv[channel] = v_pin_mv * _envariemen_coof[channel]; 
    }
    // Публикуем тестовый 32-канальный пакет в Zbus
    zbus_chan_pub(&adc2_data_chan, &msg, K_NO_WAIT);
    
    /* Сбрасываем статус события в "не готово" для следующего кадра АЦП [1] */
    _envariemen_adc_poll_events[0].state = K_POLL_STATE_NOT_READY;

    /* Повторно отправляем воркер следить за семафором DMA АЦП [1] */
    app_worker_poll_submit(&_envariemen_adc_monitor_work, _envariemen_adc_poll_events,REAL_TIME_WORKER);
    
}


static void _sense_listener_handler(struct k_work *work)
{    

    struct adc_data_msg msg;
    uint16_t raw_buf[ADC_BUF_SIZE];

    k_sem_reset(_sec_mux_adc_sem);
   
    seq_mux_adc_get_buf_copy(seq_dev,raw_buf);
    // Выполняем тестовое заполнение буфера
    for (int step = 0; step < ADC_SAMPLES_PER_CH; step++) 
    {                       
        for (int ch = 0; ch < ADC_NUM_CHANNELS; ch++) 
        {
            uint8_t ch_idx = step * ADC_NUM_CHANNELS + ch;
            float v_pin_mv =  (float)apply_median_filter_5(&_sense_filters[ch_idx], raw_buf[ch_idx]); 
            msg.channels_mv[ch_idx] =(v_pin_mv * _sense_coof[ch_idx]);
        }             
    }
        // Публикуем тестовый 32-канальный пакет в Zbus
    zbus_chan_pub(&adc_data_chan, &msg, K_NO_WAIT);

    /* Сбрасываем статус события в "не готово" для следующего кадра АЦП [1] */
    _sense_adc_poll_events[0].state = K_POLL_STATE_NOT_READY;

    /* Повторно отправляем воркер следить за семафором DMA АЦП [1] */
    app_worker_poll_submit(&_sense_adc_monitor_work, _sense_adc_poll_events,REAL_TIME_WORKER);
    
}


static int adc_scan_init(void)
{
    /* 1. ИСПРАВЛЕНИЕ: Обязательно проверяем готовность АЦП перед работой [1] */
    if (false
        || !device_is_ready(scan_dev)
        || !device_is_ready(seq_dev)
    ) 
    {
        LOG_ERR("adc device is not ready yet!");
        return -ENODEV;
    }

    _sense_adc_init();
    _envariemen_adc_init();

    _scan_adc_sem = scan_cont_adc_get_sem(scan_dev);
    _sec_mux_adc_sem = seq_mux_adc_get_sem(seq_dev);
    if (false
        || _scan_adc_sem == NULL
        || _sec_mux_adc_sem == NULL
    ) 
    {
        LOG_ERR("Failed to retrieve semaphore from adc drivers!");
        return -ENOTSUP;
    }

    k_poll_event_init(&_envariemen_adc_poll_events[0],
                      K_POLL_TYPE_SEM_AVAILABLE,
                      K_POLL_MODE_NOTIFY_ONLY,
                      _scan_adc_sem);
    k_poll_event_init(&_sense_adc_poll_events[0],
                      K_POLL_TYPE_SEM_AVAILABLE,
                      K_POLL_MODE_NOTIFY_ONLY,
                      _sec_mux_adc_sem);

    k_work_poll_init(&_envariemen_adc_monitor_work, _envariement_listener_handler);                
    app_worker_poll_submit(&_envariemen_adc_monitor_work,_envariemen_adc_poll_events,REAL_TIME_WORKER);
    k_work_poll_init(&_sense_adc_monitor_work, _sense_listener_handler);                
    app_worker_poll_submit(&_sense_adc_monitor_work,_sense_adc_poll_events,REAL_TIME_WORKER);
    
    return 0;
}

SYS_INIT(adc_scan_init, APPLICATION, 40);


static void _sense_thread_entry(void *p1, void *p2, void *p3)
{
      uint16_t raw_buf[ADC_BUF_SIZE];
    
    
    _sense_adc_init();
    struct seq_mux_adc_api *api = (struct seq_mux_adc_api *)seq_dev->api;
    /* Извлекаем интерфейс высокоуровневого API нашего драйвера */
    //struct seq_mux_adc_api *api = (struct seq_mux_adc_api *)seq_dev->api;
    struct adc_data_msg msg; 
    k_msleep(2000);
    
   
     while (1) 
     {
         // Ждем готовности на семафоре драйвера (весь цикл 32 пересылок окончен)
        int ret = api->wait_for_data(seq_dev, K_FOREVER);
        if (ret < 0) 
        {
            LOG_ERR("Failed to wait for data! Error: %d", ret);
            continue;
        }


        seq_mux_adc_get_buf_copy(seq_dev,raw_buf);
        // Выполняем тестовое заполнение буфера
        for (int step = 0; step < ADC_SAMPLES_PER_CH; step++) 
        {                       
            for (int ch = 0; ch < ADC_NUM_CHANNELS; ch++) 
            {
                uint8_t ch_idx = step * ADC_NUM_CHANNELS + ch;
                float v_pin_mv =  (float)apply_median_filter_5(&_sense_filters[ch_idx], raw_buf[ch_idx]); 
                msg.channels_mv[ch_idx] =(v_pin_mv * _sense_coof[ch_idx]);
            }             
        }
        // Публикуем тестовый 32-канальный пакет в Zbus
        zbus_chan_pub(&adc_data_chan, &msg, K_NO_WAIT);
    }
}




//K_THREAD_DEFINE(sense_thread_id, AIN_TASK_STACK_SIZE, _sense_thread_entry, 
  //          NULL, NULL, NULL, 
 //               1, 0, 0);


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
 

PARAM_ROUTE_DEFINE(ENV_P3V3,&adc2_data_chan,0,ARRAY_DATA);
PARAM_ROUTE_DEFINE(ENV_P5V0,&adc2_data_chan,1,ARRAY_DATA);
PARAM_ROUTE_DEFINE(ENV_VIN,&adc2_data_chan,2,ARRAY_DATA);
PARAM_ROUTE_DEFINE(ENV_VDOUT1,&adc2_data_chan,3,ARRAY_DATA);
PARAM_ROUTE_DEFINE(ENV_P40V,&adc2_data_chan,4,ARRAY_DATA);
PARAM_ROUTE_DEFINE(ENV_USB,&adc2_data_chan,5,ARRAY_DATA);
PARAM_ROUTE_DEFINE(ENV_VDOUT2,&adc2_data_chan,6,ARRAY_DATA);
PARAM_ROUTE_DEFINE(ENV_VDOUT3,&adc2_data_chan,7,ARRAY_DATA);

