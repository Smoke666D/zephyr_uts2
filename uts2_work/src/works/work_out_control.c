/**
 *  @file       out_and_power_control.c
 *  @headerfile out_and_power_control.h
 *
 *  @date       2026.10.06
 *  @author     Dymov Igor
 *
 *  @brief      Менеджер управления силовыми выходами и линиями питания
 *  @details    Обеспечивает обработку событий ZBUS для управления цепочкой 
 *              сдвиговых регистров по SPI и дискретными ключами питания на GPIO.
 */

/***************************************************************************************************
 *                                          INCLUDED FILES
 **************************************************************************************************/
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/shell/shell.h>

#include <hc595_chain.h>

#include "system_bus_model.h"
#include "app_worker.h"

/***************************************************************************************************
 *                                           DEFINITIONS
 **************************************************************************************************/
LOG_MODULE_REGISTER(out_and_power, LOG_LEVEL_INF);

#define LOW_CUR_CHANNELS_CNT   16
#define HIGH_CUR_CHANNELS_CNT  2

typedef struct hc595_channels_msg {
    uint32_t channels_mv[CHANNEL_DATA_SIZE]; // Наш тестовый буфер (8 шагов * 2 канала)
};

/***********************************************************************************
 *                                   PRIVATE FUNCTION PROTOTYPES
 **********************************************************************************/
static void _out_zbus_listener_callback(const struct zbus_channel *_chan);
static void _hc595_calculate_mask(const struct hc595_channels_msg *_msg, 
                                  uint8_t *_tx_data, size_t _len);
static void _out_update_handler(struct k_work *_work);
static int  _out_manager_system_init(void);

/***************************************************************************************************
 *                                           PRIVATE DATA
 **************************************************************************************************/
static struct k_work out_task; 


ZBUS_LISTENER_DEFINE(hc595_sub, _out_zbus_listener_callback);

// Объявляем канал Zbus для управления выходами
ZBUS_CHAN_DEFINE(hc595_chan,
                 struct hc595_channels_msg,
                 NULL,
                 NULL,
                 ZBUS_OBSERVERS(hc595_sub),
                 {0}
);


static const struct device *const hc595_dev = DEVICE_DT_GET(DT_NODELABEL(hc595_chain));      


/***************************************************************************************************
 *                                        PRIVATE FUNCTIONS
 **************************************************************************************************/

/**
 *  @brief      Коллбэк слушателя ZBUS для канала hc595_chan
 *  @details    Вызывается при публикации сообщений в канале hc595_chan,
 *              планируя выполнение задачи обновления регистров.
 *
 *  @param      _chan - Указатель на канал ZBUS
 */
static void _out_zbus_listener_callback(const struct zbus_channel *_chan)
{
    ARG_UNUSED(_chan);
    app_worker_submit(&out_task, REAL_TIME_WORKER);
}



/**
 *  @brief      Расчет маски для сдвигового регистра HC595
 *  @details    Заполняет переданный буфер управляющими битами LIN и слаботочных драйверов
 *              на основе данных из пакетного ZBUS-сообщения.
 *
 *  @param      _msg     - Указатель на сообщение с каналами
 *  @param      _tx_data - Указатель на выходной буфер для маски
 *  @param      _len     - Длина выходного буфера в байтах
 */
static void _hc595_calculate_mask(const struct hc595_channels_msg *_msg, 
                                  uint8_t *_tx_data, size_t _len)
{
    if (_len < 5) 
    {
        return;
    }

    memset(_tx_data, 0, _len);

    // Заполнение управляющих сигналов LIN (старшая тетрада первого байта)
    _tx_data[0] = ((uint8_t)_msg->channels_mv[18] << 4) |
                  ((uint8_t)_msg->channels_mv[19] << 5) |
                  ((uint8_t)_msg->channels_mv[20] << 6) |
                  ((uint8_t)_msg->channels_mv[21] << 7);

     static const uint8_t state_to_bits[] = {0x00, 0x02, 0x01, 0x03};

    // Заполнение каналов слаботочных драйверов
    for (int i = 0; i < LOW_CUR_DRIVER_COUNT; i++) 
    {
        uint8_t chip_index = 4 - (i / 4);
        uint8_t bit_indx = (i % 4) * 2;
        uint32_t state = _msg->channels_mv[i];

       uint32_t max_allowed_state = (i < LOW_CUR_CHANNELS_CNT) ? 4 : 3;

        if (state < max_allowed_state) 
        {
            _tx_data[chip_index] |= (state_to_bits[state] << bit_indx);
        }
    }
}

/**
 *  @brief      Обработчик воркера обновления сдвиговых регистров
 *  @details    Считывает сообщение из канала ZBUS, рассчитывает маску
 *              и записывает её в цепочку сдвиговых регистров по SPI.
 *
 *  @param      _work - Указатель на структуру задачи
 */
static void _out_update_handler(struct k_work *_work)
{    
    ARG_UNUSED(_work);
    struct hc595_channels_msg msg;
    
    if (zbus_chan_read(&hc595_chan, &msg, K_MSEC(50)) == 0)
    {
        uint8_t tx_data[5] = {0};

        _hc595_calculate_mask(&msg, tx_data, sizeof(tx_data));
                   
        if (hc595_chain_write(hc595_dev, tx_data, sizeof(tx_data)))         
        {
            LOG_ERR("Failed to write registers");
        } 
    }
}



/**
 *  @brief      Инициализация менеджера управления выходами питания
 *  @details    Инициализирует воркеры обновления, вызывает процедуру
 *              первоначальной конфигурации GPIO и регистрирует модуль в системе.
 *
 *  @return     int - Ноль при успешной инициализации
 */
static int _out_manager_system_init(void)
{
    k_work_init(&out_task, _out_update_handler);
    return 0;
}

SYS_INIT(_out_manager_system_init, APPLICATION, 40);

PARAM_ROUTE_DEFINE(DOUT1, &hc595_chan, 0, ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT2, &hc595_chan, 1, ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT3, &hc595_chan, 2, ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT4, &hc595_chan, 3, ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT5, &hc595_chan, 4, ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT6, &hc595_chan, 5, ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT7, &hc595_chan, 6, ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT8, &hc595_chan, 7, ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT9, &hc595_chan, 8, ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT10, &hc595_chan, 9, ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT11, &hc595_chan, 10, ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT12, &hc595_chan, 11, ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT13, &hc595_chan, 12, ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT14, &hc595_chan, 13, ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT15, &hc595_chan, 14, ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT16, &hc595_chan, 15, ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT17, &hc595_chan, 16, ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT18, &hc595_chan, 17, ARRAY_DATA);
PARAM_ROUTE_DEFINE(LIN_PD1, &hc595_chan, 18, ARRAY_DATA);
PARAM_ROUTE_DEFINE(LIN_PD2, &hc595_chan, 19, ARRAY_DATA);
PARAM_ROUTE_DEFINE(LIN_PD3, &hc595_chan, 20, ARRAY_DATA);
PARAM_ROUTE_DEFINE(LIN_PD4, &hc595_chan, 21, ARRAY_DATA);


/***************************************************************************************************
 *                                           END OF FILE
 **************************************************************************************************/

