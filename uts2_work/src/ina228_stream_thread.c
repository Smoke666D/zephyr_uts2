/**
 *  @file       ina_rtio_poller.c
 *  @headerfile ina228_stream_thread.h
 *
 *  @date       2026.07.23
 *  @author     Dymov Igor
 *
 *  @brief      Опрос датчиков тока и напряжения INA228 через RTIO и ZBUS
 *  @details    Осуществляет асинхронный опрос группы датчиков INA228, декодирование
 *              данных из формата Q31 в физические величины и публикацию пакета в ZBUS.
 */

/***************************************************************************************************
 *                                          INCLUDED FILES
 **************************************************************************************************/
#include <stdint.h>
#include <stdbool.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor_data_types.h>
#include <zephyr/rtio/rtio.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>

#include <zephyr/dt-bindings/sensor/ina237.h>
#include <zephyr/drivers/sensor/ina2xx.h>

#include "ina228_stream_thread.h"
#include "global_params.h"

/***************************************************************************************************
 *                                           DEFINITIONS
 **************************************************************************************************/
LOG_MODULE_REGISTER(ina_rtio_poller, LOG_LEVEL_INF);

ZBUS_SUBSCRIBER_DEFINE(energy_sub, 1);



// Канал ZBUS для периодических быстрых данных (U, I)
ZBUS_CHAN_DEFINE(ina_batch_chan,
                 ina_batch_msg_t,
                 NULL,
                 NULL,
                 ZBUS_OBSERVERS_EMPTY,
                 ZBUS_MSG_INIT(0));

// Новый канал ZBUS для асинхронной публикации энергии по запросу
ZBUS_CHAN_DEFINE(ina_energy_chan,
                 ina_energy_msg_t,
                 NULL,
                 NULL,
                 ZBUS_OBSERVERS(energy_sub),
                 ZBUS_MSG_INIT(0));

/***************************************************************************************************
 *                                          PRIVATE TYPES
 **************************************************************************************************/
typedef struct ina_param_map 
{
    uint8_t sensor_idx; // Индекс датчика 0..7 в пакете ZBUS
    bool    is_voltage; // Флаг измерения напряжения (true - напряжение, false - ток)
} ina_param_map_t;

/***************************************************************************************************
 *                                   PRIVATE FUNCTION PROTOTYPES
 **************************************************************************************************/
static int    _ina2xx_param_get(PARAM_ID _id, PARAM_VAL *_val);
static double _q31_to_double(int32_t _value, int8_t _shift);
static void   _process_energy_chain(bool _publish_to_zbus);
static void   _process_fast_measurements(ina_batch_msg_t *_batch);
static void   _rti_poller_thread(void *_p1, void *_p2, void *_p3);



/***************************************************************************************************
 *                                           PRIVATE DATA
 **************************************************************************************************/
static K_SEM_DEFINE(sem_read_energy,  0, 1);
static K_SEM_DEFINE(sem_reset_energy, 0, 1);
static K_MUTEX_DEFINE(energy_read_mutex);

// 1. Каналы для быстрого периодического опроса (U, I)
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

// 2. Отдельные IODEV для чтения энергии (энергия сбрасывается аппаратно при чтении)
#define INA_ENERGY_CHANNELS \
    { (enum sensor_channel)SENSOR_CHAN_INA2XX_ENERGY, 0 }

SENSOR_DT_READ_IODEV(ina_energy_iodev_0, DT_NODELABEL(cur_sens_brd_low),   INA_ENERGY_CHANNELS);
SENSOR_DT_READ_IODEV(ina_energy_iodev_1, DT_NODELABEL(cur_sens_brd_high),  INA_ENERGY_CHANNELS);
SENSOR_DT_READ_IODEV(ina_energy_iodev_2, DT_NODELABEL(cur_sens_vdut2),     INA_ENERGY_CHANNELS);
SENSOR_DT_READ_IODEV(ina_energy_iodev_3, DT_NODELABEL(cur_sens_vdut3),     INA_ENERGY_CHANNELS);
SENSOR_DT_READ_IODEV(ina_energy_iodev_4, DT_NODELABEL(cur_sens_vin),       INA_ENERGY_CHANNELS);
SENSOR_DT_READ_IODEV(ina_energy_iodev_5, DT_NODELABEL(cur_sens_3_3_dcdc),  INA_ENERGY_CHANNELS);
SENSOR_DT_READ_IODEV(ina_energy_iodev_6, DT_NODELABEL(cur_sens_vdout_pwr), INA_ENERGY_CHANNELS);
SENSOR_DT_READ_IODEV(ina_energy_iodev_7, DT_NODELABEL(cur_sens_5vd),       INA_ENERGY_CHANNELS);

static const struct rtio_iodev *const energy_iodevs[CUR_SENS_NUM_SENSORS] = 
{
    &ina_energy_iodev_0, &ina_energy_iodev_1, &ina_energy_iodev_2, &ina_energy_iodev_3,
    &ina_energy_iodev_4, &ina_energy_iodev_5, &ina_energy_iodev_6, &ina_energy_iodev_7
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

static uint8_t __dtcm_noinit_section __aligned(ARCH_STACK_PTR_ALIGN) 
    rti_poller_stack[INA228_THREAD_STACK_SIZE];

static struct k_thread rti_poller_thread_data;
static k_tid_t         rti_poller_tid;


/***************************************************************************************************
 *                                        PRIVATE FUNCTIONS
 **************************************************************************************************/

/**
 *  @brief      Обработчик запроса параметров датчиков INA228
 *  @details    Извлекает из сохраненного в ZBUS пакета значение напряжения 
 *              или тока для запрашиваемого ID параметра.
 *
 *  @param      _id  - Идентификатор запрашиваемого параметра
 *  @param      _val - Указатель для записи значения параметра
 *
 *  @return     int - Ноль при успехе, отрицательный код ошибки при сбое
 */
static int _ina2xx_param_get(PARAM_ID _id, PARAM_VAL *_val)
{
    // 1. Проверяем, попадает ли ID в непрерывный диапазон напряжений
    if (_id >= SENS_BRD_LOW_VOLTAGE && _id <= SENS_VD5_VOLTAGE)
    {
        ina_batch_msg_t batch;

        // Читаем последний сохраненный снимок данных из канала ZBUS
        int err = zbus_chan_read(&ina_batch_chan, &batch, K_NO_WAIT);
        if (err == 0) 
        {
            // Простейшее вычисление индекса датчика без использования таблиц
            uint32_t sensor_idx = _id - SENS_BRD_LOW_VOLTAGE;
            _val->value.real = (float)batch.sensors[sensor_idx].voltage;
        }
        return err;
    }
    // 2. Проверяем, попадает ли ID в непрерывный диапазон токов
    else if (_id >= SENS_BRD_LOW_CURRENT && _id <= SENS_VD5_CURRENT)
    {
        ina_batch_msg_t batch;

        // Читаем последний сохраненный снимок данных из канала ZBUS
        int err = zbus_chan_read(&ina_batch_chan, &batch, K_NO_WAIT);
        if (err == 0) 
        {
            // Простейшее вычисление индекса датчика без использования таблиц
            uint32_t sensor_idx = _id - SENS_BRD_LOW_CURRENT;
            _val->value.real = (float)batch.sensors[sensor_idx].current;
        }
        return err;
    }
    
    return -ENOTSUP;
}

/**
 *  @brief      Обработчик запроса параметров датчиков INA228
 *  @details    Извлекает из сохраненного в ZBUS пакета значение напряжения 
 *              или тока для запрашиваемого ID параметра.
 *
 *  @param      _id  - Идентификатор запрашиваемого параметра
 *  @param      _val - Указатель для записи значения параметра
 *
 *  @return     int - Ноль при успехе, отрицательный код ошибки при сбое
 */
static int _ina2xx_param_get_power(PARAM_ID _id, PARAM_VAL *_val)
{
    if (true
        && _id >= SENS_I2C_POWER_BRD_LOW
        && _id <= SENS_I2C_POWER_VD5
       )
    {
        int err = k_mutex_lock(&energy_read_mutex, K_MSEC(1000));
        if (err != 0)
        {
            return -EAGAIN;
        }

        // 1. Сбрасываем (вычитываем) старые, необработанные уведомления из очереди, если они там были
        const struct zbus_channel *chan;
        while (zbus_sub_wait(&energy_sub, &chan, K_NO_WAIT) == 0) {
            // Холостой цикл для очистки очереди подписчика
        }
        
        // 2. Запускаем преобразование
        k_sem_give(&sem_read_energy);
        
        // 3. Блокируем поток и ждем уведомления о НОВОЙ публикации в канале
        err = zbus_sub_wait(&energy_sub, &chan, K_MSEC(500));
        if (err != 0)
        {
            LOG_ERR("Таймаут ожидания уведомления ZBUS по энергии");
            k_mutex_unlock(&energy_read_mutex);
            return -ETIMEDOUT;
        }

        ina_energy_msg_t batch;
        
        // 4. Считываем свежие данные из пришедшего в уведомлении канала
        err = zbus_chan_read(chan, &batch, K_NO_WAIT);
        if (err == 0) 
        {                                  
            uint32_t sensor_idx = _id - SENS_I2C_POWER_BRD_LOW;
            if (sensor_idx < CUR_SENS_NUM_SENSORS)
            {
                double energy = batch.energy[sensor_idx];
                _val->value.real = (float)energy;
            }
            else
            {
                err = -EINVAL;
            }
        }

        k_mutex_unlock(&energy_read_mutex);
        return err;
    }
    return -ENOTSUP;
}

/**
 *  @brief      Обработчик записи параметров мощности/энергии датчиков INA228
 *  @details    При записи значения 0.0 сбрасывает соответствующий элемент в ZBUS
 *              и запускает цикл аппаратного сброса накопителей энергии в датчиках.
 *
 *  @param      _id  - Идентификатор записываемого параметра
 *  @param      _val - Указатель на структуру с записываемым значением
 *
 *  @return     int - Ноль при успехе, отрицательный код ошибки при сбое
 */
static int _ina2xx_param_set_power(PARAM_ID _id, const PARAM_VAL *_val)
{
    // Проверяем диапазон параметров энергии
    if (true
        && _id >= SENS_I2C_POWER_BRD_LOW
        && _id <= SENS_I2C_POWER_VD5
       )
    {
        // Сброс запускается только при записи числового нуля (0.0f)
        if (_val->value.real == 0.0f)
        {
            ina_energy_msg_t batch;

            // 1. Читаем текущий снимок данных из ZBUS
            int err = zbus_chan_read(&ina_energy_chan, &batch, K_NO_WAIT);
            if (err == 0)
            {
                uint32_t sensor_idx = _id - SENS_I2C_POWER_BRD_LOW;
                if (sensor_idx < CUR_SENS_NUM_SENSORS)
                {
                    // 2. Сбрасываем в 0 значение только для запрашиваемого датчика
                    batch.energy[sensor_idx] = 0.0;
                    
                    // 3. Публикуем обновленный пакет обратно в ZBUS, чтобы
                    // внешние потребители мгновенно увидели 0.0, не дожидаясь нового опроса
                    zbus_chan_pub(&ina_energy_chan, &batch, K_NO_WAIT);
                }
            }

            // 4. Выдаем семафор на фоновый аппаратный сброс накопителей в чипах INA228
            k_sem_give(&sem_reset_energy);

            return 0;
        }
        
        // Попытка записать любое другое значение, кроме 0, не поддерживается
        return -ENOTSUP;
    }
    
    return -ENOTSUP;
}

// Регистрация путей в ROM-таблицу маршрутизации
PARAM_ROUTE_RO(SENS_BRD_LOW_CURRENT,    _ina2xx_param_get);
PARAM_ROUTE_RO(SENS_BRD_LOW_VOLTAGE,    _ina2xx_param_get);
PARAM_ROUTE_RO(SENS_BRD_HIGH_CURRENT,   _ina2xx_param_get);
PARAM_ROUTE_RO(SENS_BRD_HIGH_VOLTAGE,   _ina2xx_param_get);
PARAM_ROUTE_RO(SENS_VDUT2_CURRENT,      _ina2xx_param_get);
PARAM_ROUTE_RO(SENS_VDUT2_VOLTAGE,      _ina2xx_param_get);
PARAM_ROUTE_RO(SENS_VDUT3_CURRENT,      _ina2xx_param_get);
PARAM_ROUTE_RO(SENS_VDUT3_VOLTAGE,      _ina2xx_param_get);
PARAM_ROUTE_RO(SENS_VIN_CURRENT,        _ina2xx_param_get);
PARAM_ROUTE_RO(SENS_VIN_VOLTAGE,        _ina2xx_param_get);
PARAM_ROUTE_RO(SENS_DCDC_3_3_CURRENT,   _ina2xx_param_get);
PARAM_ROUTE_RO(SENS_DCDC_3_3_VOLTAGE,   _ina2xx_param_get);
PARAM_ROUTE_RO(SENS_VDOUT_PWR_CURRENT,  _ina2xx_param_get);
PARAM_ROUTE_RO(SENS_VDOUT_PWR_VOLTAGE,  _ina2xx_param_get);
PARAM_ROUTE_RO(SENS_VD5_CURRENT,        _ina2xx_param_get);
PARAM_ROUTE_RO(SENS_VD5_VOLTAGE,        _ina2xx_param_get);
PARAM_ROUTE_RW(SENS_I2C_POWER_BRD_LOW,   _ina2xx_param_set_power, _ina2xx_param_get_power);
PARAM_ROUTE_RW(SENS_I2C_POWER_BRD_HIGH,  _ina2xx_param_set_power, _ina2xx_param_get_power);
PARAM_ROUTE_RW(SENS_I2C_POWER_VDUT2,     _ina2xx_param_set_power, _ina2xx_param_get_power);
PARAM_ROUTE_RW(SENS_I2C_POWER_VDUT3,     _ina2xx_param_set_power, _ina2xx_param_get_power);
PARAM_ROUTE_RW(SENS_I2C_POWER_VIN,       _ina2xx_param_set_power, _ina2xx_param_get_power);
PARAM_ROUTE_RW(SENS_I2C_POWER_DCDC_3_3,  _ina2xx_param_set_power, _ina2xx_param_get_power);
PARAM_ROUTE_RW(SENS_I2C_POWER_VDOUT_PWR, _ina2xx_param_set_power, _ina2xx_param_get_power);
PARAM_ROUTE_RW(SENS_I2C_POWER_VD5,       _ina2xx_param_set_power, _ina2xx_param_get_power);


/**
 *  @brief      Преобразование формата Q31 в вещественное число
 *  @details    Принимает 32-битное число со знаком в формате с фиксированной 
 *              точкой и преобразует его в формат double с учетом сдвига.
 *
 *  @param      _value - Исходное значение в формате Q31
 *  @param      _shift - Вес двоичной точки (число знаков после запятой)
 *
 *  @return     double - Преобразованное вещественное число
 */
static inline double _q31_to_double(int32_t _value, int8_t _shift)
{
    return (double)_value / (double)(1ULL << (31 - _shift));
}

/**
 *  @brief      Внеочередная цепочка обработки энергии (чтение или сброс)
 *  @details    Асинхронно считывает каналы энергии со всех датчиков. 
 *              При необходимости декодирует данные и публикует их в ZBUS.
 *
 *  @param      _publish_to_zbus - Флаг публикации данных в ZBUS
 */
static void _process_energy_chain(bool _publish_to_zbus)
{
    int expected_completions = 0;

    // Асинхронный запуск чтения канала энергии
    for (int i = 0; i < CUR_SENS_NUM_SENSORS; i++) 
    {
        int rc = sensor_read_async_mempool(energy_iodevs[i], &ina_rtio, (void *)(uintptr_t)i);
        if (rc < 0) 
        {
            LOG_ERR("Не удалось запустить чтение энергии для %s: %d", sensor_names[i], rc);
        }
        else
        {
            expected_completions++;
        }
    }

    ina_energy_msg_t energy_msg = {0};
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

            // Если запрашивалось именно чтение, декодируем данные
            if (_publish_to_zbus) 
            {
                const struct sensor_decoder_api *decoder = NULL;
                int dec_err = sensor_get_decoder(devices[idx], &decoder);

                if (true
                    && dec_err == 0 
                    && decoder != NULL 
                    && buf != NULL
                   ) 
                {
                    uint32_t fit = 0;
                    struct sensor_q31_data e_data = {0};
                    struct sensor_chan_spec chan_spec = 
                    {
                        .chan_type = (uint16_t)SENSOR_CHAN_INA2XX_ENERGY, 
                        .chan_idx  = 0
                    };

                    int dec_rc = decoder->decode(buf, chan_spec, &fit, 1, &e_data);
                    
                    if (dec_rc > 0) 
                    {
                        energy_msg.energy[idx] = _q31_to_double(
                            e_data.readings[0].value, 
                            e_data.shift
                        );
                    }
                }
            }

            rtio_release_buffer(&ina_rtio, buf, buf_len);
        } 
        else 
        {
            LOG_ERR("Сбой чтения энергии %s с ошибкой: %d", sensor_names[idx], cqe->result);
        }

        rtio_cqe_release(&ina_rtio, cqe);
        completions_received++;
    }

    // Если это было чтение, отправляем данные в ZBUS
    if (_publish_to_zbus) 
    {
        zbus_chan_pub(&ina_energy_chan, &energy_msg, K_NO_WAIT);
        LOG_INF("Пакет накопленной энергии опубликован в ZBUS.");
    } 
    else 
    {
        LOG_INF("Аппаратные накопители энергии сброшены.");
    }
}

/**
 *  @brief      Стандартная циклическая цепочка опроса U и I
 *  @details    Асинхронно считывает каналы напряжения и тока со всех датчиков,
 *              декодирует их и отправляет пакет в ZBUS.
 *
 *  @param      _batch - Указатель на структуру для записи результатов опроса
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

            if (true
                && dec_err == 0 
                && decoder != NULL 
                && buf != NULL
               ) 
            {
                uint32_t fit = 0;
                struct sensor_q31_data v_data = {0};
                struct sensor_q31_data i_data = {0};
                struct sensor_chan_spec  v_spec = 
                {
                    .chan_type = SENSOR_CHAN_VOLTAGE, 
                    .chan_idx  = 0
                };

                // Декодирование напряжения
                int dec_rc = decoder->decode(buf, v_spec, &fit, 1, &v_data);
                if (dec_rc > 0) 
                {
                    _batch->sensors[idx].voltage = _q31_to_double(
                        v_data.readings[0].value, 
                        v_data.shift
                    );
                }

                // Декодирование тока
                fit = 0;
                struct sensor_chan_spec  i_spec = 
                {
                    .chan_type = SENSOR_CHAN_CURRENT, 
                    .chan_idx  = 0
                };

                dec_rc = decoder->decode(buf, i_spec, &fit, 1, &i_data);
                if (dec_rc > 0) 
                {
                    _batch->sensors[idx].current = _q31_to_double(
                        i_data.readings[0].value, 
                        i_data.shift
                    );
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
 *  @brief      Основной поток планировщика
 *  @details    Циклически проверяет семафоры запросов чтения/сброса энергии,
 *              вызывая соответствующую цепочку, либо производит циклический
 *              опрос быстрых параметров (U, I).
 *
 *  @param      _p1 - Неиспользуемый параметр 1
 *  @param      _p2 - Неиспользуемый параметр 2
 *  @param      _p3 - Неиспользуемый параметр 3
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
    // Инициализируем структуру один раз, чтобы сохранять предыдущие значения при сбоях
    memset(&batch, 0, sizeof(batch)); 

    while (1) 
    {
        // Проверяем семафоры запросов
        bool do_read_energy  = (k_sem_take(&sem_read_energy, K_NO_WAIT) == 0);
        bool do_reset_energy = (k_sem_take(&sem_reset_energy, K_NO_WAIT) == 0);

        if (false
            || do_read_energy 
            || do_reset_energy
           ) 
        {
            // Выполняем внеочередную цепочку работы с энергией.
            // Если do_read_energy истинно — декодируем и публикуем в ZBUS,
            // в противном случае выполняем холостое чтение для аппаратного сброса.
            _process_energy_chain(do_read_energy);
        } 
        else 
        {
            // Обычный циклический опрос быстрых параметров
            _process_fast_measurements(&batch);
        }

        k_msleep(POLL_INTERVAL_MS);
    }
}

/***************************************************************************************************
 *                                        PUBLIC FUNCTIONS
 **************************************************************************************************/

/**
 *  @brief      Запуск потока опроса датчиков INA228
 *  @details    Создает и запускает поток с приоритетом THREAD_PRIORITY,
 *              выделяя стек rti_poller_stack и подключая поддержку регистров FPU.
 */
void start_ina228_poller_thread(void)
{
    rti_poller_tid = k_thread_create(
        &rti_poller_thread_data,
        (k_thread_stack_t *)rti_poller_stack,
        INA228_THREAD_STACK_SIZE,
        _rti_poller_thread,
        NULL, NULL, NULL,
        INA228_THREAD_PRIORITY,
        K_FP_REGS,
        K_NO_WAIT
    );
}

/***************************************************************************************************
 *                                           END OF FILE
 **************************************************************************************************/