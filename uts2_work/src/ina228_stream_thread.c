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

#include "ina228_stream_thread.h"
#include "global_params.h"

/***************************************************************************************************
 *                                           DEFINITIONS
 **************************************************************************************************/
LOG_MODULE_REGISTER(ina_rtio_poller, LOG_LEVEL_INF);

#define POLL_INTERVAL_MS   5
#define THREAD_STACK_SIZE  3072
#define THREAD_PRIORITY    4

ZBUS_CHAN_DEFINE(ina_batch_chan,
                 ina_batch_msg_t,
                 NULL,
                 NULL,
                 ZBUS_OBSERVERS_EMPTY,
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
static void   _rti_poller_thread(void *_p1, void *_p2, void *_p3);

/***************************************************************************************************
 *                                           PRIVATE DATA
 **************************************************************************************************/
static const ina_param_map_t param_to_ina_map[] = 
{
    [SENS_BRD_LOW_CURRENT]     = { .sensor_idx = CUR_SENS_BRD_LOW,   .is_voltage = false },
    [SENS_BRD_LOW_VOLTAGE]     = { .sensor_idx = CUR_SENS_BRD_LOW,   .is_voltage = true  },
    
    [SENS_BRD_HIGH_CURRENT]    = { .sensor_idx = CUR_SENS_BRD_HIGH,  .is_voltage = false },
    [SENS_BRD_HIGH_VOLTAGE]    = { .sensor_idx = CUR_SENS_BRD_HIGH,  .is_voltage = true  },
    
    [SENS_VDUT2_CURRENT]       = { .sensor_idx = CUR_SENS_VDUT2,     .is_voltage = false },
    [SENS_VDUT2_VOLTAGE]       = { .sensor_idx = CUR_SENS_VDUT2,     .is_voltage = true  },
    
    [SENS_VDUT3_CURRENT]       = { .sensor_idx = CUR_SENS_VDUT3,     .is_voltage = false },
    [SENS_VDUT3_VOLTAGE]       = { .sensor_idx = CUR_SENS_VDUT3,     .is_voltage = true  },
    
    [SENS_VIN_CURRENT]         = { .sensor_idx = CUR_SENS_VIN,       .is_voltage = false },
    [SENS_VIN_VOLTAGE]         = { .sensor_idx = CUR_SENS_VIN,       .is_voltage = true  },
    
    [SENS_DCDC_3_3_CURRENT]    = { .sensor_idx = CUR_SENS_DCDC_3_3,  .is_voltage = false },
    [SENS_DCDC_3_3_VOLTAGE]    = { .sensor_idx = CUR_SENS_DCDC_3_3,  .is_voltage = true  },
    
    [SENS_VDOUT_PWR_CURRENT]   = { .sensor_idx = CUR_SENS_VDOUT_PWR, .is_voltage = false },
    [SENS_VDOUT_PWR_VOLTAGE]   = { .sensor_idx = CUR_SENS_VDOUT_PWR, .is_voltage = true  },
    
    [SENS_VD5_CURRENT]         = { .sensor_idx = CUR_SENS_VD5,       .is_voltage = false },
    [SENS_VD5_VOLTAGE]         = { .sensor_idx = CUR_SENS_VD5,       .is_voltage = true  },
};

#define INA_CHANNELS \
    { SENSOR_CHAN_VOLTAGE, 0 }, \
    { SENSOR_CHAN_CURRENT, 0 }

SENSOR_DT_READ_IODEV(ina_iodev_0, DT_NODELABEL(cur_sens_brd_low),   INA_CHANNELS);
SENSOR_DT_READ_IODEV(ina_iodev_1, DT_NODELABEL(cur_sens_brd_high),  INA_CHANNELS);
SENSOR_DT_READ_IODEV(ina_iodev_2, DT_NODELABEL(cur_sens_vdut2),     INA_CHANNELS);
SENSOR_DT_READ_IODEV(ina_iodev_3, DT_NODELABEL(cur_sens_vdut3),     INA_CHANNELS);
SENSOR_DT_READ_IODEV(ina_iodev_4, DT_NODELABEL(cur_sens_vin),       INA_CHANNELS);
SENSOR_DT_READ_IODEV(ina_iodev_5, DT_NODELABEL(cur_sens_3_3_dcdc),  INA_CHANNELS);
SENSOR_DT_READ_IODEV(ina_iodev_6, DT_NODELABEL(cur_sens_vdout_pwr), INA_CHANNELS);
SENSOR_DT_READ_IODEV(ina_iodev_7, DT_NODELABEL(cur_sens_5vd),       INA_CHANNELS);

static const struct rtio_iodev *const iodevs[CUR_SENS_NUM_SENSORS] = 
{
    &ina_iodev_0, &ina_iodev_1, &ina_iodev_2, &ina_iodev_3,
    &ina_iodev_4, &ina_iodev_5, &ina_iodev_6, &ina_iodev_7
};

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

// Названия датчиков для понятного логирования
static const char *const sensor_names[CUR_SENS_NUM_SENSORS] = 
{
    "BRD_LOW (0x40)",
    "BRD_HIGH (0x41)",
    "VDUT2 (0x42)",
    "VDUT3 (0x43)",
    "VIN (0x44)",
    "3_3_DCDC (0x45)",
    "VDOUT_PWR (0x46)",
    "5VD (0x47)"
};

RTIO_DEFINE_WITH_MEMPOOL(ina_rtio, 
                         CUR_SENS_NUM_SENSORS, 
                         CUR_SENS_NUM_SENSORS, 
                         CUR_SENS_NUM_SENSORS * 2, 
                         128, 
                         sizeof(void *));

static uint8_t __dtcm_noinit_section __aligned(ARCH_STACK_PTR_ALIGN) rti_poller_stack[THREAD_STACK_SIZE];

// Структура для хранения метаданных потока в ядре
static struct k_thread rti_poller_thread_data;

// Хэндл запущенного потока
static k_tid_t rti_poller_tid;

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
    // Проверяем, что ID входит в диапазон параметров INA228
    if (true
        && _id >= SENS_BRD_LOW_CURRENT
        && _id <= SENS_VD5_VOLTAGE
       ) 
    {
        ina_batch_msg_t batch;
        
        // Читаем последний сохраненный снимок данных из канала ZBUS без блокировки
        int err = zbus_chan_read(&ina_batch_chan, &batch, K_NO_WAIT);
        if (err == 0) 
        {
            ina_param_map_t map = param_to_ina_map[_id];
            
            if (map.is_voltage) 
            {
                double v_volts = batch.sensors[map.sensor_idx].voltage;
                _val->value.real = (float)v_volts;
            } 
            else 
            {
                double i_amps = batch.sensors[map.sensor_idx].current;
                _val->value.real = (float)i_amps;
            }
        }
        return err;
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
 *  @brief      Поток опроса датчиков
 *  @details    Циклически запускает асинхронный опрос всех датчиков INA228,
 *              ждет завершения операций, декодирует данные из формата Q31 
 *              и публикует их в ZBUS-канал.
 *
 *  @param      _p1 - Неиспользуемый параметр 1
 *  @param      _p2 - Неиспользуемый параметр 2
 *  @param      _p3 - Неиспользуемый параметр 3
 */
static void _rti_poller_thread(void *_p1, void *_p2, void *_p3)
{
    ARG_UNUSED(_p1); 
    ARG_UNUSED(_p2); 
    ARG_UNUSED(_p3);

    for (int i = 0; i < CUR_SENS_NUM_SENSORS; i++) 
    {
        if (!device_is_ready(devices[i])) 
        {
            LOG_ERR("Устройство %s [%s] не готово", 
                    devices[i]->name, sensor_names[i]);
            return;
        }
    }

    LOG_INF("Все датчики INA228 готовы. Запуск цикла RTIO/ZBUS.");

    ina_batch_msg_t batch;

    while (1) 
    {
        memset(&batch, 0, sizeof(batch));

        // Старт асинхронного сбора данных со всех 8 узлов
        for (int i = 0; i < CUR_SENS_NUM_SENSORS; i++) 
        {
            int rc = sensor_read_async_mempool(iodevs[i], &ina_rtio, (void *)(uintptr_t)i);
            if (rc < 0) 
            {
                LOG_ERR("Не удалось запустить асинхронное чтение для %s: %d", 
                        sensor_names[i], rc);
            }
        }

        // Ожидание выполнения транзакций и декодирование данных
        int completions_received = 0;
        while (completions_received < CUR_SENS_NUM_SENSORS) 
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
                sensor_get_decoder(devices[idx], &decoder);

                if (true
                    && decoder != NULL 
                    && buf != NULL
                   ) 
                {
                    uint32_t fit = 0;
                    struct sensor_q31_data v_data = {0};
                    struct sensor_q31_data i_data = {0};

                    // Декодирование напряжения
                    int dec_rc = decoder->decode(
                        buf, 
                        (struct sensor_chan_spec){SENSOR_CHAN_VOLTAGE, 0}, 
                        &fit, 1, &v_data
                    );
                    
                    if (dec_rc > 0) 
                    {
                        batch.sensors[idx].voltage = _q31_to_double(
                            v_data.readings[0].value, 
                            v_data.shift
                        );
                    }

                    // Декодирование тока
                    fit = 0;
                    dec_rc = decoder->decode(
                        buf, 
                        (struct sensor_chan_spec){SENSOR_CHAN_CURRENT, 0}, 
                        &fit, 1, &i_data
                    );
                    
                    if (dec_rc > 0) 
                    {
                        batch.sensors[idx].current = _q31_to_double(
                            i_data.readings[0].value, 
                            i_data.shift
                        );
                    }
                }

                rtio_release_buffer(&ina_rtio, buf, buf_len);
            } 
            else 
            {
                LOG_ERR("Сбой чтения датчика %s с ошибкой: %d", 
                        sensor_names[idx], cqe->result);
            }

            rtio_cqe_release(&ina_rtio, cqe);
            completions_received++;
        }

        // Вывод отладочной информации в системный лог
        for (int i = 0; i < CUR_SENS_NUM_SENSORS; i++) 
        {
            LOG_INF("%s | V: %.3f V | I: %.6f A", 
                    sensor_names[i], 
                    batch.sensors[i].voltage, 
                    batch.sensors[i].current);
        }

        // Публикация пакета данных в ZBUS для остальных потоков
        zbus_chan_pub(&ina_batch_chan, &batch, K_NO_WAIT);

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
    // Создание потока с разрешением аппаратного FPU (K_FP_REGS)
    rti_poller_tid = k_thread_create(
        &rti_poller_thread_data,               // Ссылка на структуру k_thread
        (k_thread_stack_t *)rti_poller_stack,  // Указатель на выделенный в DTCM стек
        THREAD_STACK_SIZE,                     // Размер стека
        _rti_poller_thread,                    // Функция входа в поток
        NULL, NULL, NULL,                      // Аргументы p1, p2, p3 (не используются)
        THREAD_PRIORITY,                       // Приоритет задачи
        K_FP_REGS,                             // Опции: Обязательно разрешаем использование FPU
        K_NO_WAIT                              // Запуск без задержки
    );
}

/***************************************************************************************************
 *                                           END OF FILE
 **************************************************************************************************/