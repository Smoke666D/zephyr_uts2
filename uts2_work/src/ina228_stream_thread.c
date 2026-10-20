/**
 *  @file       ina_rtio_poller.c
 *  @headerfile ina228_stream_thread.h
 *
 *  @date       2026.07.23
 *  @author     Dymov Igor
 *
 *  @brief      Опрос датчиков тока и напряжения INA228 через RTIO и ZBUS
 *  @details    Быстрый асинхронный опрос U и I через RTIO + прямое точечное
 *              чтение накопленной энергии 10 раз в секунду.
 */

/***************************************************************************************************
 *                                          INCLUDED FILES
 **************************************************************************************************/
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor_data_types.h>
#include <zephyr/rtio/rtio.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>

#include <zephyr/dt-bindings/sensor/ina237.h>
#include <zephyr/drivers/sensor/ina2xx.h>

#include "system_bus_model.h"
#include "ina228_stream_thread.h"

/***************************************************************************************************
 *                                           DEFINITIONS
 **************************************************************************************************/
LOG_MODULE_REGISTER(ina_rtio_poller, LOG_LEVEL_INF);

#define FAST_POLL_INTERVAL_MS     1   /* Быстрый опрос U и I каждые 10 мс (100 Гц) */
#define SLOW_ENERGY_POLL_TICKS    10   /* Опрос энергии каждые 10 тиков (100 мс / 10 Гц) */

#define INA228_THREAD_STACK_SIZE  3072
#define INA228_THREAD_PRIORITY    4

// Канал ZBUS для быстрых данных (U, I)
ZBUS_CHAN_DEFINE(ina_batch_chan,
                 ina_batch_msg_t,
                 NULL,
                 NULL,
                 ZBUS_OBSERVERS_EMPTY,
                 ZBUS_MSG_INIT(0));

// Канал ZBUS для накопленной энергии
ZBUS_CHAN_DEFINE(ina_energy_chan,
                 ina_energy_msg_t,
                 NULL,
                 NULL,
                 ZBUS_OBSERVERS_EMPTY,
                 ZBUS_MSG_INIT(0));

/***************************************************************************************************
 *                                   PRIVATE FUNCTION PROTOTYPES
 **************************************************************************************************/
static double _q31_to_double(int32_t _value, int8_t _shift);
static void   _process_fast_measurements(ina_batch_msg_t *_batch);
static void   _process_slow_energy_fetch(void);
static void   _rti_poller_thread(void *_p1, void *_p2, void *_p3);

/***************************************************************************************************
 *                                           PRIVATE DATA
 **************************************************************************************************/
// Программный аккумулятор энергии в Джоулях для 8 датчиков
static double total_energy[CUR_SENS_NUM_SENSORS] = {0};

// Каналы для быстрого опроса U и I через RTIO
#define INA_FAST_CHANNELS \
    { SENSOR_CHAN_VOLTAGE, 0 }, \
    { SENSOR_CHAN_CURRENT, 0 }

SENSOR_DT_READ_IODEV(ina_iodev_0, DT_NODELABEL(cur_sens_brd_low),   INA_FAST_CHANNELS);
SENSOR_DT_READ_IODEV(ina_iodev_1, DT_NODELABEL(cur_sens_brd_high),  INA_FAST_CHANNELS);
SENSOR_DT_READ_IODEV(ina_iodev_2, DT_NODELABEL(cur_sens_vdut2),     INA_FAST_CHANNELS);
SENSOR_DT_READ_IODEV(ina_iodev_3, DT_NODELABEL(cur_sens_vdut3),     INA_FAST_CHANNELS);
SENSOR_DT_READ_IODEV(ina_iodev_4, DT_NODELABEL(cur_sens_vin),       INA_FAST_CHANNELS);
SENSOR_DT_READ_IODEV(ina_iodev_5, DT_NODELABEL(cur_sens_3_3_dcdc),  INA_FAST_CHANNELS);
SENSOR_DT_READ_IODEV(ina_iodev_6, DT_NODELABEL(cur_sens_vdout_pwr), INA_FAST_CHANNELS);
SENSOR_DT_READ_IODEV(ina_iodev_7, DT_NODELABEL(cur_sens_5vd),       INA_FAST_CHANNELS);

static const struct rtio_iodev *const fast_iodevs[CUR_SENS_NUM_SENSORS] = 
{
    &ina_iodev_0, &ina_iodev_1, &ina_iodev_2, &ina_iodev_3,
    &ina_iodev_4, &ina_iodev_5, &ina_iodev_6, &ina_iodev_7
};

// Общие структуры устройств
static const struct device *const devices[CUR_SENS_NUM_SENSORS] = 
{
    DEVICE_DT_GET(DT_NODELABEL(cur_sens_brd_low)),
    DEVICE_DT_GET(DT_NODELABEL(cur_sens_brd_high)),
    DEVICE_DT_GET(DT_NODELABEL(cur_sens_vdut2)),
    DEVICE_DT_GET(DT_NODELABEL(cur_sens_vdut3)),
    DEVICE_DT_GET(DT_NODELABEL(cur_sens_vin)),
    DEVICE_DT_GET(DT_NODELABEL(cur_sens_3_3_dcdc)),
    DEVICE_DT_GET(DT_NODELABEL(cur_sens_vdout_pwr)),
    DEVICE_DT_GET(DT_NODELABEL(cur_sens_5vd))
};

static const char *const sensor_names[CUR_SENS_NUM_SENSORS] = 
{
    "BRD_LOW (0x40)", "BRD_HIGH (0x41)", "VDUT2 (0x42)", "VDUT3 (0x43)",
    "VIN (0x44)", "3_3_DCDC (0x45)", "VDOUT_PWR (0x46)", "5VD (0x47)"
};

RTIO_DEFINE_WITH_MEMPOOL(ina_rtio, 
                         CUR_SENS_NUM_SENSORS, 
                         CUR_SENS_NUM_SENSORS, 
                         CUR_SENS_NUM_SENSORS * 2, 
                         128, 
                         sizeof(void *));

// Объявление стека в секции DTCM
K_THREAD_STACK_DEFINE(rti_poller_stack, INA228_THREAD_STACK_SIZE) __attribute__((section(".dtcm_noinit")));

/***************************************************************************************************
 *                                        PRIVATE FUNCTIONS
 **************************************************************************************************/

static inline double _q31_to_double(int32_t _value, int8_t _shift)
{
    return (double)_value / (double)(1ULL << (31 - _shift));
}

/**
 * @brief Быстрый асинхронный опрос U и I через RTIO
 */
static void _process_fast_measurements(ina_batch_msg_t *_batch)
{
    int expected_completions = 0;

    for (int i = 0; i < CUR_SENS_NUM_SENSORS; i++) 
    {
        int rc = sensor_read_async_mempool(fast_iodevs[i], &ina_rtio, (void *)(uintptr_t)i);
        if (rc < 0) 
        {
            LOG_ERR("Не удалось запустить чтение для %s: %d", sensor_names[i], rc);
        }
        else
        {
            expected_completions++;
        }
    }

    int completions_received = 0;
    while (completions_received < expected_completions) 
    {
        struct rtio_cqe *cqe = rtio_cqe_consume_block(&ina_rtio);
        if (cqe == NULL) 
        {
            break;
        }

        int idx = (int)(uintptr_t)cqe->userdata;

        if (cqe->result >= 0) 
        {
            uint8_t *buf = NULL;
            uint32_t buf_len = 0;
            rtio_cqe_get_mempool_buffer(&ina_rtio, cqe, &buf, &buf_len);

            const struct sensor_decoder_api *decoder = NULL;
            int dec_err = sensor_get_decoder(devices[idx], &decoder);

            if (dec_err == 0 && decoder != NULL && buf != NULL) 
            {
                uint32_t fit = 0;
                struct sensor_q31_data v_data = {0};
                struct sensor_q31_data i_data = {0};

                // 1. Напряжение
                struct sensor_chan_spec v_spec = { .chan_type = SENSOR_CHAN_VOLTAGE, .chan_idx = 0 };
                if (decoder->decode(buf, v_spec, &fit, 1, &v_data) > 0) 
                {
                    _batch->sensors[idx * 2] = _q31_to_double(v_data.readings[0].value, v_data.shift);
                }

                // 2. Ток
                fit = 0;
                struct sensor_chan_spec i_spec = { .chan_type = SENSOR_CHAN_CURRENT, .chan_idx = 0 };
                if (decoder->decode(buf, i_spec, &fit, 1, &i_data) > 0) 
                {
                    _batch->sensors[idx * 2 + 1] = _q31_to_double(i_data.readings[0].value, i_data.shift);
                }
            }

            rtio_release_buffer(&ina_rtio, buf, buf_len);
        } 
        else 
        {
            LOG_ERR("Сбой чтения %s: %d", sensor_names[idx], cqe->result);
        }

        rtio_cqe_release(&ina_rtio, cqe);
        completions_received++;
    }

    zbus_chan_pub(&ina_batch_chan, _batch, K_NO_WAIT);
}

/**
 * @brief Медленное прямое чтение аппаратного регистра энергии 10 раз в секунду (раз в 100 мс)
 */
static void _process_slow_energy_fetch(void)
{
    ina_energy_msg_t energy_msg;

    for (int i = 0; i < CUR_SENS_NUM_SENSORS; i++) 
    {
        struct sensor_value e_val;
        
        // Точечное прямое чтение регистра энергии INA228
        int rc = sensor_sample_fetch_chan(devices[i], (enum sensor_channel)SENSOR_CHAN_INA2XX_ENERGY);
        if (rc == 0) 
        {
            rc = sensor_channel_get(devices[i], (enum sensor_channel)SENSOR_CHAN_INA2XX_ENERGY, &e_val);
            if (rc == 0) 
            {
                double delta_e = sensor_value_to_double(&e_val);
                total_energy[i] += delta_e; // Накапливаем в суммарный счетчик
            }
        }
        
        energy_msg.energy[i] = total_energy[i];
    }

    // Публикация накопленной энергии в ZBUS
    zbus_chan_pub(&ina_energy_chan, &energy_msg, K_NO_WAIT);
}

/**
 * @brief Главный поток планировщика
 */
static void _rti_poller_thread(void *_p1, void *_p2, void *_p3)
{
    ARG_UNUSED(_p1); ARG_UNUSED(_p2); ARG_UNUSED(_p3);

    for (int i = 0; i < CUR_SENS_NUM_SENSORS; i++) 
    {
        if (!device_is_ready(devices[i])) 
        {
            LOG_ERR("Устройство %s не готово", devices[i]->name);
            return;
        }
    }

    LOG_INF("Все датчики INA228 готовы. Запуск гибридного цикла RTIO/ZBUS.");

    ina_batch_msg_t batch;
    memset(&batch, 0, sizeof(batch)); 

    uint32_t poll_tick = 0;

    while (1) 
    {  
        // 1. Быстрый опрос U и I каждые FAST_POLL_INTERVAL_MS (10 мс)
        _process_fast_measurements(&batch);

        // 2. Опрос энергии раз в 10 тиков (каждые 100 мс)
        if (++poll_tick >= SLOW_ENERGY_POLL_TICKS) 
        {
            poll_tick = 0;
            _process_slow_energy_fetch();
        }

        k_msleep(FAST_POLL_INTERVAL_MS);
    }
}

/***************************************************************************************************
 *                                        PUBLIC FUNCTIONS
 **************************************************************************************************/

/**
 * @brief Запрос сброса накопленной энергии для всех датчиков
 */
void ina228_request_energy_reset(void)
{
    int err = zbus_chan_claim(&ina_energy_chan, K_MSEC(100));
    if (err == 0) 
    {
        ina_energy_msg_t *msg = zbus_chan_msg(&ina_energy_chan);
        for (int i = 0; i < CUR_SENS_NUM_SENSORS; i++) 
        {
            total_energy[i] = 0.0;
            msg->energy[i] = 0.0;
        }
        zbus_chan_finish(&ina_energy_chan);
        zbus_chan_notify(&ina_energy_chan, K_MSEC(100));
    }
}

/**
 * @brief Запрос сброса накопленной энергии для конкретного датчика (0..7)
 */
void ina228_reset_sensor_energy(uint8_t sensor_idx)
{
    if (sensor_idx < CUR_SENS_NUM_SENSORS) 
    {
        int err = zbus_chan_claim(&ina_energy_chan, K_MSEC(100));
        if (err == 0) 
        {
            ina_energy_msg_t *msg = zbus_chan_msg(&ina_energy_chan);
            total_energy[sensor_idx] = 0.0;
            msg->energy[sensor_idx] = 0.0;
            zbus_chan_finish(&ina_energy_chan);
            zbus_chan_notify(&ina_energy_chan, K_MSEC(100));
        }
    }
}

// Запуск потока с использованием стека в DTCM
K_THREAD_DEFINE(ina_thread_id, INA228_THREAD_STACK_SIZE, _rti_poller_thread, 
               NULL, NULL, NULL, 
                INA228_THREAD_PRIORITY, 0, 0);

// Регистрация путей шины параметров для Токов и Напряжений
PARAM_ROUTE_DEFINE(SENS_BRD_LOW_CURRENT,   &ina_batch_chan, 0,  ARRAY_DATA);
PARAM_ROUTE_DEFINE(SENS_BRD_LOW_VOLTAGE,   &ina_batch_chan, 1,  ARRAY_DATA);  
PARAM_ROUTE_DEFINE(SENS_BRD_HIGH_CURRENT,  &ina_batch_chan, 2,  ARRAY_DATA); 
PARAM_ROUTE_DEFINE(SENS_BRD_HIGH_VOLTAGE,  &ina_batch_chan, 3,  ARRAY_DATA); 
PARAM_ROUTE_DEFINE(SENS_VDUT2_CURRENT,     &ina_batch_chan, 4,  ARRAY_DATA);   
PARAM_ROUTE_DEFINE(SENS_VDUT2_VOLTAGE,     &ina_batch_chan, 5,  ARRAY_DATA);  
PARAM_ROUTE_DEFINE(SENS_VDUT3_CURRENT,     &ina_batch_chan, 6,  ARRAY_DATA);
PARAM_ROUTE_DEFINE(SENS_VDUT3_VOLTAGE,     &ina_batch_chan, 7,  ARRAY_DATA);  
PARAM_ROUTE_DEFINE(SENS_VIN_CURRENT,       &ina_batch_chan, 8,  ARRAY_DATA);
PARAM_ROUTE_DEFINE(SENS_VIN_VOLTAGE,       &ina_batch_chan, 9,  ARRAY_DATA);    
PARAM_ROUTE_DEFINE(SENS_DCDC_3_3_CURRENT,  &ina_batch_chan, 10, ARRAY_DATA);
PARAM_ROUTE_DEFINE(SENS_DCDC_3_3_VOLTAGE,  &ina_batch_chan, 11, ARRAY_DATA);
PARAM_ROUTE_DEFINE(SENS_VDOUT_PWR_CURRENT, &ina_batch_chan, 12, ARRAY_DATA);
PARAM_ROUTE_DEFINE(SENS_VDOUT_PWR_VOLTAGE, &ina_batch_chan, 13, ARRAY_DATA);
PARAM_ROUTE_DEFINE(SENS_VD5_CURRENT,       &ina_batch_chan, 14, ARRAY_DATA);      
PARAM_ROUTE_DEFINE(SENS_VD5_VOLTAGE,       &ina_batch_chan, 15, ARRAY_DATA); 

// Регистрация путей шины параметров для Накопленной Энергии
PARAM_ROUTE_DEFINE(SENS_I2C_POWER_BRD_LOW,   &ina_energy_chan, 0, ARRAY_DATA);
PARAM_ROUTE_DEFINE(SENS_I2C_POWER_BRD_HIGH,  &ina_energy_chan, 1, ARRAY_DATA);
PARAM_ROUTE_DEFINE(SENS_I2C_POWER_VDUT2,     &ina_energy_chan, 2, ARRAY_DATA);
PARAM_ROUTE_DEFINE(SENS_I2C_POWER_VDUT3,     &ina_energy_chan, 3, ARRAY_DATA);
PARAM_ROUTE_DEFINE(SENS_I2C_POWER_VIN,       &ina_energy_chan, 4, ARRAY_DATA);
PARAM_ROUTE_DEFINE(SENS_I2C_POWER_DCDC_3_3,  &ina_energy_chan, 5, ARRAY_DATA);
PARAM_ROUTE_DEFINE(SENS_I2C_POWER_VDOUT_PWR, &ina_energy_chan, 6, ARRAY_DATA);
PARAM_ROUTE_DEFINE(SENS_I2C_POWER_VD5,       &ina_energy_chan, 7, ARRAY_DATA);

/***************************************************************************************************
 *                                           END OF FILE
 **************************************************************************************************/