/**
 *  @file       dac.c
 *  @headerfile dac.h
 *
 *  @date       2026.11.02
 *  @author     Dymov Igor
 *
 *  @brief      Управление ЦАП и цифровым потенциометром AD5243
 *  @details    Обрабатывает сообщения и запросы из очереди для настройки 
 *              выходного напряжения ЦАП и изменения сопротивления каналов AD5243.
 */

/***************************************************************************************************
 *                                          INCLUDED FILES
 **************************************************************************************************/
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/shell/shell.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/dac.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>

#include "system_define.h"
#include "system_bus_model.h"
#include "app_worker.h"

/***************************************************************************************************
 *                                           DEFINITIONS
 **************************************************************************************************/
LOG_MODULE_REGISTER(dac_work, LOG_LEVEL_INF);

#define I2C1_NODE            DT_NODELABEL(i2c1)
#define AD5243_DEFAULT_ADDR  0x2F

#define MAX_P40V_OUT         30.0f
#define MIN_P40V_OUT         12.0f

// Физические константы потенциометра AD5243BRMZ100
#define AD5243_R_AB_KOHM     100.0f  // Номинал 100 кОм
#define AD5243_R_W_KOHM      0.12f   // Сопротивление ползунка 120 Ом (0.12 кОм)
#define AD5243_R_MAX_KOHM    (AD5243_R_W_KOHM + (AD5243_R_AB_KOHM * 255.0f / 256.0f))

/***************************************************************************************************
 *                                          PRIVATE TYPES
 **************************************************************************************************/
typedef enum ad5243_channel
{
    AD5243_CHANNEL_1 = 0,
    AD5243_CHANNEL_2 = 1
} ad5243_channel_t;

/**
 *  @brief Структура сообщения ZBUS с массивом данных ЦАП
 */
typedef struct dac_data_msg 
{
    uint32_t value[TOTAL_DAC_COUNT]; // Буфер значений (8 шагов * 2 канала)
} dac_data_msg_t;

/***************************************************************************************************
 *                                   PRIVATE FUNCTION PROTOTYPES
 **************************************************************************************************/
static int  _ad5243_set_wiper(ad5243_channel_t _channel, uint8_t _value);
static void _dac_update_handler(struct k_work *_work);
static int  _dac_control_init(void);

/***************************************************************************************************
 *                                           PRIVATE DATA
 **************************************************************************************************/
static const struct device *dac_dev = DEVICE_DT_GET(DT_NODELABEL(dac1));

static struct k_work_poll  dac_task        __attribute__((section("DTCM")));
static struct k_poll_event dac_poll_events[1] __attribute__((section("DTCM")));
static uint32_t            cur_dac_value   __attribute__((section("DTCM"))) = 0;
static dac_data_msg_t      cache_dac_data  __attribute__((section("DTCM"))) = {0};

static struct i2c_dt_spec i2c_spec = 
{
    .addr = AD5243_DEFAULT_ADDR,
    .bus  = DEVICE_DT_GET(I2C1_NODE)
};

K_MSGQ_DEFINE(dac_rx_queue, sizeof(server_request), 3, 4);

ZBUS_CHAN_DEFINE(
    dac_chan,
    struct dac_data_msg,
    NULL,
    &dac_rx_queue,
    ZBUS_OBSERVERS_EMPTY,
    ZBUS_MSG_INIT(0)
);

/***************************************************************************************************
 *                                        PRIVATE FUNCTIONS
 **************************************************************************************************/

/**
 *  @brief      Установка положения ползунка цифрового потенциометра AD5243
 *  @details    Отправляет по I2C управляющий байт канала и байт положения ползунка.
 *
 *  @param      _channel - Канал потенциометра (AD5243_CHANNEL_1 или AD5243_CHANNEL_2)
 *  @param      _value   - Положение ползунка (0..255)
 *
 *  @return     int - Ноль при успехе, отрицательный код ошибки при сбое
 */
static int _ad5243_set_wiper(ad5243_channel_t _channel, uint8_t _value)
{
    uint8_t tx_data[2];
    
    if (_channel == AD5243_CHANNEL_1) 
    {
        tx_data[0] = 0x00; // Выбор канала 1 (W1)
    } 
    else 
    {
        tx_data[0] = 0x80; // Выбор канала 2 (W2)
    }

    tx_data[1] = _value; // Значение положения движка (0..255)
    int ret = i2c_write_dt(&i2c_spec, tx_data, sizeof(tx_data));
    if (ret < 0) 
    {
        LOG_DBG("i2c_write_dt failed with error %d for channel %d", ret, _channel);
    }
    
    return ret;         
}


/**
 *  @brief      Обработчик воркера обновления параметров ЦАП и AD5243
 *  @details    Вычитывает запрос из очереди msgq, обновляет ЦАП или канал AD5243
 *              в зависимости от ID запроса и отсылает уведомление/ответ.
 *
 *  @param      _work - Указатель на структуру задачи
 */
static void _dac_update_handler(struct k_work *_work)
{
    ARG_UNUSED(_work);
    int err = 0;
    server_request req;

    if (k_msgq_get(&dac_rx_queue, &req, K_NO_WAIT) == 0)
    {    

        if (true
            && req.id == DAC_VALUE
            && cache_dac_data.value[0] != req.value.sd_float
           )
        {
            cache_dac_data.value[0] = req.value.sd_uint;                            
            dac_write_value(dac_dev, 2, cache_dac_data.value[0]);
        }
        else if (true
                 && req.id == ADJ_RES1
                 && cache_dac_data.value[1] != req.value.sd_uint
                )        
        {
            cache_dac_data.value[1] = req.value.sd_uint;
            err = _ad5243_set_wiper(AD5243_CHANNEL_1, cache_dac_data.value[1]);
        }
        else if (true
                 && req.id == ADJ_RES2
                 && cache_dac_data.value[2] != req.value.sd_uint
                )                
        {
            cache_dac_data.value[2] = req.value.sd_uint;
            err |= _ad5243_set_wiper(AD5243_CHANNEL_2, cache_dac_data.value[2]);
        }

        if (err < 0)
        {
            LOG_ERR("AD5243 connection error!");
        }        
    }
    if (req.sync != NULL)
    {
        if (req.response != NULL)
        {
            *req.response = err;
        }
        k_sem_give(req.sync);
    }
    dac_poll_events[0].state = K_POLL_STATE_NOT_READY;
    app_worker_poll_submit(&dac_task, dac_poll_events, COMMON_WORKER);  
}

/**
 *  @brief      Инициализация подсистемы ЦАП и цифрового потенциометра
 *  @details    Настраивает канал 2 ЦАП, проверяет готовность шины I2C,
 *              устанавливает начальные значения потенциометра и регистрирует воркер.
 *
 *  @return     int - Ноль при успехе, отрицательный код ошибки при сбое
 */
static int _dac_control_init(void)
{
    int err;

    if (!device_is_ready(dac_dev)) 
    {
        LOG_ERR("DAC1 hardware device is not ready during boot!");
        return -ENODEV;
    }

    struct dac_channel_cfg dac_ch_cfg = 
    {
        .channel_id = 2, // PA5
        .resolution = 12
    };

    // Автоматически настраиваем канал 2 при старте системы
    err = dac_channel_setup(dac_dev, &dac_ch_cfg);
    if (err != 0) 
    {
        LOG_ERR("Failed to setup DAC channel 2 during boot: %d", err);
        return err;
    }

    k_poll_event_init(&dac_poll_events[0],
                      K_POLL_TYPE_MSGQ_DATA_AVAILABLE, // Тип: появление данных в очереди
                      K_POLL_MODE_NOTIFY_ONLY,         // Режим: только уведомить
                      &dac_rx_queue); 

    k_work_poll_init(&dac_task, _dac_update_handler); 
    app_worker_poll_submit(&dac_task, dac_poll_events, COMMON_WORKER);

    LOG_INF("DAC1 Channel 2 (PA5) successfully initialized!");

    if (!device_is_ready(i2c_spec.bus)) 
    {
        LOG_ERR("I2C bus not ready!");
        return -ENODEV;
    }

    // Проверка связи с I2C ЦАП и установка начальных значений
    if (false
        || _ad5243_set_wiper(AD5243_CHANNEL_1, 0) < 0
        || _ad5243_set_wiper(AD5243_CHANNEL_2, 0) < 0
       ) 
    {
        SERVER_CH_SET_BIT(AD5243_ERROR_CH, AD5243_IU_ERROR);
        LOG_ERR("AD5243 connection error!");
    }
    else 
    {
        SERVER_CH_CLEAR_BIT(AD5243_ERROR_CH, AD5243_IU_ERROR);
        LOG_INF("AD5243 successfully initialized!");
    }

    return 0;
}

SYS_INIT(_dac_control_init, DAC_DEAMON_INIT_LEVEL, DAC_DEAMON_INIT_PRIOR);

SERVER_OUT_CHANNEL_DEFINE(DAC_VALUE, &dac_chan, 0, UINT_DATA);
SERVER_OUT_CHANNEL_DEFINE(ADJ_RES1,  &dac_chan, 1, UINT_DATA);
SERVER_OUT_CHANNEL_DEFINE(ADJ_RES2,  &dac_chan, 2, UINT_DATA);

/***************************************************************************************************
 *                                           END OF FILE
 **************************************************************************************************/
