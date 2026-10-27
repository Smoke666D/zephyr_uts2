/**
 *  @file       adc_pipeline.c
 *  @headerfile adc_pipeline.h
 *
 *  @date       2026.10.21
 *  @author     Dymov Igor
 *
 *  @brief      Обработка и фильтрация сигналов измерительных каналов АЦП
 *  @details    Считывает данные сканирующего и селективного АЦП, применяет 
 *              медианную фильтрацию, пересчитывает значения с учетом делителей
 *              и публикует измерения в каналы ZBUS.
 */

/***************************************************************************************************
 *                                          INCLUDED FILES
 **************************************************************************************************/
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include <seq_mux_adc.h>
#include <scan_cont_adc.h>

#include "system_bus_model.h"
#include "filters.h"
#include "app_worker.h"

/***************************************************************************************************
 *                                           DEFINITIONS
 **************************************************************************************************/
LOG_MODULE_REGISTER(seq_mux_adc_drv, LOG_LEVEL_INF);

#define ENV_ADC_BUF_SIZE  8
#define COMBINATIONS_COUNT 8

#define ADC_NUM_CHANNELS   5
#define ADC_SAMPLES_PER_CH 8
#define ADC_BUF_SIZE       (ADC_NUM_CHANNELS * ADC_SAMPLES_PER_CH)
#define TOTAL_CHANNEL_COUNT ADC_BUF_SIZE 

/* Структура сообщения АЦП */
typedef struct sens_adc_msg {
    float channels_mv[TOTAL_CHANNEL_COUNT]; // Наш тестовый буфер (8 шагов * 2 канала)
};

typedef struct env_adc_msg {
    float channels_mv[ENV_ADC_BUF_SIZE]; // Наш тестовый буфер (8 шагов * 2 канала)
};



// Канал ZBUS для передачи измерений каналов Sense
ZBUS_CHAN_DEFINE(
    adc_data_chan,
    struct sens_adc_msg,
    NULL,
    NULL,
    ZBUS_OBSERVERS_EMPTY,
    ZBUS_MSG_INIT(0)
);

// Канал ZBUS для передачи измерений каналов Env
ZBUS_CHAN_DEFINE(
    adc2_data_chan,
    struct env_adc_msg,
    NULL,
    NULL,
    ZBUS_OBSERVERS_EMPTY,
    ZBUS_MSG_INIT(0)
);

/***************************************************************************************************
 *                                   PRIVATE FUNCTION PROTOTYPES
 **************************************************************************************************/
static void _sense_adc_init(void);
static void _env_adc_init(void);
static void _env_listener_handler(struct k_work *_work);
static void _sense_listener_handler(struct k_work *_work);
static int  _adc_pipeline_init(void);

/***************************************************************************************************
 *                                           PRIVATE DATA
 **************************************************************************************************/
static const uint32_t r1_resistors_sens[TOTAL_CHANNEL_COUNT] = 
{
    69000, 69000, 69000, 10000, 69000, // Шаг 0 (AO1, AO7, AO13, AVsense1)
    69000, 69000, 69000, 10000, 69000, // Шаг 1 (AO2, AO8, AO14, AVsense2)
    69000, 69000, 69000, 10000, 69000, // Шаг 2 (AO3 ...)
    69000, 69000, 69000, 10000, 69000, // Шаг 3
    69000, 69000, 69000, 10000,     0, // Шаг 4
    69000, 69000, 69000, 10000,     0, // Шаг 5
        0,     0,     0,     0,     0, // Шаг 6 (Тестовые каналы DA11..DA44 T1)
        0,     0,     0,     0,     0  // Шаг 7 (Тестовые каналы DA11..DA44 T2)
};

static const uint32_t r2_resistors_sens[TOTAL_CHANNEL_COUNT] = 
{
    10000, 10000, 10000, 10000, 10000,
    10000, 10000, 10000, 10000, 10000,
    10000, 10000, 10000, 10000, 10000,
    10000, 10000, 10000, 10000, 10000,
    10000, 10000, 10000, 10000,     1,
    10000, 10000, 10000, 10000,     1,
        1,     1,     1,     1,     1,
        1,     1,     1,     1,     1
};

static const uint32_t r1_resistors_env[ENV_ADC_BUF_SIZE] = 
{
    10000, 10000, 100000, 124000, 124000,
    10000, 10000,  10000  
};

static const uint32_t r2_resistors_env[ENV_ADC_BUF_SIZE] = 
{
    20000, 10000, 12000, 10000, 6800,
    10000, 20000, 20000
};

static const struct device *const scan_dev = DEVICE_DT_GET(DT_NODELABEL(scan_adc));
static const struct device *const seq_dev  = DEVICE_DT_GET(DT_NODELABEL(my_sequencer));

static struct k_work_poll  env_adc_monitor_work;
static struct k_poll_event env_adc_poll_events[1];

static struct k_work_poll  sense_adc_monitor_work;
static struct k_poll_event sense_adc_poll_events[1];

static float sense_coof[TOTAL_CHANNEL_COUNT] __attribute__((section("DTCM")));
static float env_coof[ENV_ADC_BUF_SIZE]      __attribute__((section("DTCM")));

static channel_median_filter_t env_filters[ENV_ADC_BUF_SIZE]          __attribute__((section("DTCM")));
static channel_median_filter_t sense_filters[TOTAL_CHANNEL_COUNT] __attribute__((section("DTCM")));

static struct k_sem *scan_adc_sem;
static struct k_sem *seq_mux_adc_sem;

/***************************************************************************************************
 *                                        PRIVATE FUNCTIONS
 **************************************************************************************************/

/**
 *  @brief      Инициализация коэффициентов и фильтров каналов датчиков (Sense)
 *  @details    Рассчитывает масштабные коэффициенты делителей напряжения
 *              и очищает историю медианных фильтров для всех каналов Sense.
 */
static void _sense_adc_init(void)
{
    for (int i = 0; i < TOTAL_CHANNEL_COUNT; i++) 
    {
        float r1 = (float)r1_resistors_sens[i];
        float r2 = (float)r2_resistors_sens[i];

        sense_coof[i] = ((r1 + r2) / r2) * (3.3f / 65535.0f);
        sense_filters[i].head = 0;
        memset(sense_filters[i].history, 0, sizeof(uint16_t) * MEDIAN_WINDOW_SIZE);
    }
}

/**
 *  @brief      Инициализация коэффициентов и фильтров каналов окружения (Env)
 *  @details    Рассчитывает масштабные коэффициенты делителей напряжения
 *              и очищает историю медианных фильтров для каналов окружения.
 */
static void _env_adc_init(void)
{
    for (int i = 0; i < ENV_ADC_BUF_SIZE; i++) 
    {
        float r1 = (float)r1_resistors_env[i];
        float r2 = (float)r2_resistors_env[i];

        env_coof[i] = ((r1 + r2) / r2) * (3.3f / 65535.0f);
        env_filters[i].head = 0;
        memset(env_filters[i].history, 0, sizeof(uint16_t) * MEDIAN_WINDOW_SIZE);
    }
}

/**
 *  @brief      Обработчик готовности кадра сканирующего АЦП (Env)
 *  @details    Вычитывает сырой буфер сканирующего АЦП, применяет медианный
 *              фильтр 5-го порядка, вычисляет напряжения и публикует пакет в ZBUS.
 *
 *  @param      _work - Указатель на структуру задачи поллинга
 */
static void _env_listener_handler(struct k_work *_work)
{    
    ARG_UNUSED(_work);
    struct env_adc_msg msg; 
    uint16_t _raw_buf[ENV_ADC_BUF_SIZE];

    k_sem_reset(scan_adc_sem);
    scan_cont_adc_get_buf_copy(scan_dev, _raw_buf);

    for (int channel = 0; channel < ENV_ADC_BUF_SIZE; channel++) 
    {                      
        float v_pin_mv = (float)apply_median_filter_5(&env_filters[channel], _raw_buf[channel]);
        msg.channels_mv[channel] = v_pin_mv * env_coof[channel]; 
    }

    // Публикация пакета измерений в ZBUS
    zbus_chan_pub(&adc2_data_chan, &msg, K_NO_WAIT);
    
    // Сброс статуса события в "не готово" для следующего кадра АЦП
    env_adc_poll_events[0].state = K_POLL_STATE_NOT_READY;

    // Повторная отправка воркера на отслеживание семафора DMA
    app_worker_poll_submit(&env_adc_monitor_work, env_adc_poll_events, REAL_TIME_WORKER);
}

/**
 *  @brief      Обработчик готовности кадра мультиплексируемого АЦП (Sense)
 *  @details    Вычитывает буфер селективного АЦП, применяет медианный фильтр,
 *              рассчитывает физические напряжения и публикует пакет в ZBUS.
 *
 *  @param      _work - Указатель на структуру задачи поллинга
 */
static void _sense_listener_handler(struct k_work *_work)
{    
    ARG_UNUSED(_work);
    struct sens_adc_msg msg;
    uint16_t _raw_buf[ADC_BUF_SIZE];

    k_sem_reset(seq_mux_adc_sem);
   
    seq_mux_adc_get_buf_copy(seq_dev, _raw_buf);

    for (int channel = 0; channel < TOTAL_CHANNEL_COUNT; channel++) 
    {                               
        float v_pin_mv = (float)apply_median_filter_5(&sense_filters[channel], _raw_buf[channel]); 
        msg.channels_mv[channel] = v_pin_mv * sense_coof[channel];        
    }

    // Публикация 32-канального пакета измерений в ZBUS
    zbus_chan_pub(&adc_data_chan, &msg, K_NO_WAIT);

    // Сброс статуса события в "не готово" для следующего кадра АЦП
    sense_adc_poll_events[0].state = K_POLL_STATE_NOT_READY;

    // Повторная отправка воркера на отслеживание семафора DMA
    app_worker_poll_submit(&sense_adc_monitor_work, sense_adc_poll_events, REAL_TIME_WORKER);
}

/**
 *  @brief      Инициализация конвейера обработки АЦП
 *  @details    Проверяет готовность драйверов АЦП, запрашивает семафоры DMA,
 *              настраивает события поллинга и запускает воркеры в очередь.
 *
 *  @return     int - Ноль при успехе, отрицательный код ошибки при сбое
 */
static int _adc_pipeline_init(void)
{
    // Проверка готовности периферийных устройств АЦП
    if (false
        || !device_is_ready(scan_dev)
        || !device_is_ready(seq_dev)
       ) 
    {
        LOG_ERR("ADC device is not ready yet!");
        return -ENODEV;
    }

    _sense_adc_init();
    _env_adc_init();

    scan_adc_sem    = scan_cont_adc_get_sem(scan_dev);
    seq_mux_adc_sem = seq_mux_adc_get_sem(seq_dev);

    if (false
        || scan_adc_sem == NULL
        || seq_mux_adc_sem == NULL
       ) 
    {
        LOG_ERR("Failed to retrieve semaphore from ADC drivers!");
        return -ENOTSUP;
    }

    k_poll_event_init(&env_adc_poll_events[0],
                      K_POLL_TYPE_SEM_AVAILABLE,
                      K_POLL_MODE_NOTIFY_ONLY,
                      scan_adc_sem);

    k_poll_event_init(&sense_adc_poll_events[0],
                      K_POLL_TYPE_SEM_AVAILABLE,
                      K_POLL_MODE_NOTIFY_ONLY,
                      seq_mux_adc_sem);

    k_work_poll_init(&env_adc_monitor_work, _env_listener_handler);                
    app_worker_poll_submit(&env_adc_monitor_work, env_adc_poll_events, REAL_TIME_WORKER);

    k_work_poll_init(&sense_adc_monitor_work, _sense_listener_handler);                
    app_worker_poll_submit(&sense_adc_monitor_work, sense_adc_poll_events, REAL_TIME_WORKER);
    
    return 0;
}

SYS_INIT(_adc_pipeline_init, APPLICATION, 40);

/***************************************************************************************************
 *                                 Привязка ZBUS к логическим каналам сервера
 **************************************************************************************************/

PARAM_ROUTE_DEFINE(AIN_AO1,        &adc_data_chan, 0,  FLOAT_DATA);
PARAM_ROUTE_DEFINE(AIN_AO7,        &adc_data_chan, 1,  FLOAT_DATA);
PARAM_ROUTE_DEFINE(AIN_AO13,       &adc_data_chan, 2,  FLOAT_DATA);
PARAM_ROUTE_DEFINE(AIN_AVsense1,   &adc_data_chan, 3,  FLOAT_DATA);
PARAM_ROUTE_DEFINE(LIN_AO1,        &adc_data_chan, 4,  FLOAT_DATA);
PARAM_ROUTE_DEFINE(AIN_AO2,        &adc_data_chan, 5,  FLOAT_DATA);
PARAM_ROUTE_DEFINE(AIN_AO8,        &adc_data_chan, 6,  FLOAT_DATA);
PARAM_ROUTE_DEFINE(AIN_AO14,       &adc_data_chan, 7,  FLOAT_DATA);
PARAM_ROUTE_DEFINE(AIN_AVsense2,   &adc_data_chan, 8,  FLOAT_DATA);
PARAM_ROUTE_DEFINE(LIN_AO2,        &adc_data_chan, 9,  FLOAT_DATA);
PARAM_ROUTE_DEFINE(AIN_AO3,        &adc_data_chan, 10, FLOAT_DATA);
PARAM_ROUTE_DEFINE(AIN_AO9,        &adc_data_chan, 11, FLOAT_DATA);
PARAM_ROUTE_DEFINE(AIN_AO15,       &adc_data_chan, 12, FLOAT_DATA);
PARAM_ROUTE_DEFINE(AIN_AVsense3,   &adc_data_chan, 13, FLOAT_DATA);
PARAM_ROUTE_DEFINE(LIN_AO3,        &adc_data_chan, 14, FLOAT_DATA);
PARAM_ROUTE_DEFINE(AIN_AO4,        &adc_data_chan, 15, FLOAT_DATA);
PARAM_ROUTE_DEFINE(AIN_AO10,       &adc_data_chan, 16, FLOAT_DATA);
PARAM_ROUTE_DEFINE(AIN_AO16,       &adc_data_chan, 17, FLOAT_DATA);
PARAM_ROUTE_DEFINE(AIN_AVsense4,   &adc_data_chan, 18, FLOAT_DATA);
PARAM_ROUTE_DEFINE(LIN_AO4,        &adc_data_chan, 19, FLOAT_DATA);
PARAM_ROUTE_DEFINE(AIN_AO5,        &adc_data_chan, 20, FLOAT_DATA);
PARAM_ROUTE_DEFINE(AIN_AO11,       &adc_data_chan, 21, FLOAT_DATA);
PARAM_ROUTE_DEFINE(AIN_AO17,       &adc_data_chan, 22, FLOAT_DATA);
PARAM_ROUTE_DEFINE(AIN_AVsense5,   &adc_data_chan, 23, FLOAT_DATA);
PARAM_ROUTE_DEFINE(DA41_GND1,      &adc_data_chan, 24, FLOAT_DATA);
PARAM_ROUTE_DEFINE(AIN_AO6,        &adc_data_chan, 25, FLOAT_DATA);
PARAM_ROUTE_DEFINE(AIN_AO12,       &adc_data_chan, 26, FLOAT_DATA);
PARAM_ROUTE_DEFINE(AIN_AO18,       &adc_data_chan, 27, FLOAT_DATA);
PARAM_ROUTE_DEFINE(AIN_AVsense6,   &adc_data_chan, 28, FLOAT_DATA);
PARAM_ROUTE_DEFINE(DA41_GND2,      &adc_data_chan, 29, FLOAT_DATA);
PARAM_ROUTE_DEFINE(AIN_DA11_test1, &adc_data_chan, 30, FLOAT_DATA);
PARAM_ROUTE_DEFINE(AIN_DA20_test1, &adc_data_chan, 31, FLOAT_DATA);
PARAM_ROUTE_DEFINE(AIN_DA33_test1, &adc_data_chan, 32, FLOAT_DATA);
PARAM_ROUTE_DEFINE(AIN_DA44_test1, &adc_data_chan, 33, FLOAT_DATA);
PARAM_ROUTE_DEFINE(AIN_DA41_test1, &adc_data_chan, 34, FLOAT_DATA);
PARAM_ROUTE_DEFINE(AIN_DA11_test2, &adc_data_chan, 35, FLOAT_DATA);
PARAM_ROUTE_DEFINE(AIN_DA20_test2, &adc_data_chan, 36, FLOAT_DATA);
PARAM_ROUTE_DEFINE(AIN_DA33_test2, &adc_data_chan, 37, FLOAT_DATA);
PARAM_ROUTE_DEFINE(AIN_DA44_test2, &adc_data_chan, 38, FLOAT_DATA);
PARAM_ROUTE_DEFINE(AIN_DA41_test2, &adc_data_chan, 39, FLOAT_DATA);

PARAM_ROUTE_DEFINE(ENV_P3V3,       &adc2_data_chan, 0, FLOAT_DATA);
PARAM_ROUTE_DEFINE(ENV_P5V0,       &adc2_data_chan, 1, FLOAT_DATA);
PARAM_ROUTE_DEFINE(ENV_VIN,        &adc2_data_chan, 2, FLOAT_DATA);
PARAM_ROUTE_DEFINE(ENV_VDOUT1,     &adc2_data_chan, 3, FLOAT_DATA);
PARAM_ROUTE_DEFINE(ENV_P40V,       &adc2_data_chan, 4, FLOAT_DATA);
PARAM_ROUTE_DEFINE(ENV_USB,        &adc2_data_chan, 5, FLOAT_DATA);
PARAM_ROUTE_DEFINE(ENV_VDOUT2,     &adc2_data_chan, 6, FLOAT_DATA);
PARAM_ROUTE_DEFINE(ENV_VDOUT3,     &adc2_data_chan, 7, FLOAT_DATA);

/***************************************************************************************************
 *                                           END OF FILE
 **************************************************************************************************/

