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
#include <zephyr/drivers/dac.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/stm32_clock_control.h>
#include <stm32h7xx_ll_dac.h>
#include <stm32h7xx_ll_tim.h>
#include "system_define.h"
#include "system_bus_model.h"
#include "app_worker.h"

/***************************************************************************************************
 *                                           DEFINITIONS
 **************************************************************************************************/
LOG_MODULE_REGISTER(int_dac_work, LOG_LEVEL_INF);

/***************************************************************************************************
 *                                          PRIVATE TYPES
 **************************************************************************************************/

typedef enum
{
    INT_DAC      = 0,
    AD5243_CH1   = 1,
    AD5243_CH2   = 2,
    DAC_CH_CNT   = 3
} dac_ch_target_t;



/**
 *  @brief Структура сообщения ZBUS с массивом данных ЦАП
 */
typedef struct dac_data_msg 
{
    uint32_t value[DAC_CH_CNT]; // Буфер значений (8 шагов * 2 канала)
} dac_data_msg_t;

/***************************************************************************************************
 *                                   PRIVATE FUNCTION PROTOTYPES
 **************************************************************************************************/

static void _dac_update_handler(struct k_work *_work);
static int  _dac_control_init(void);

/***************************************************************************************************
 *                                           PRIVATE DATA
 **************************************************************************************************/
static const struct device *dac_dev = DEVICE_DT_GET(DT_NODELABEL(dac1));


static struct k_work_poll  int_dac_task             __attribute__((section("DTCM")));
static struct k_poll_event int_dac_poll_events[1]   __attribute__((section("DTCM")));
static dac_data_msg_t      cache_dac_data           __attribute__((section("DTCM"))) = {0};




K_MSGQ_DEFINE(int_dac_rx_queue, sizeof(server_request), 3, 4);


ZBUS_CHAN_DEFINE(
    int_dac_chan,
    uint32_t,
    NULL,
    &int_dac_rx_queue,
    ZBUS_OBSERVERS_EMPTY,
    ZBUS_MSG_INIT(0)
);

/***************************************************************************************************
 *                                        PRIVATE FUNCTIONS
 **************************************************************************************************/

static void _int_dac_update_handler(struct k_work *_work)
{
    ARG_UNUSED(_work);
    int err = 0;
    server_request req;

    if (k_msgq_get(&int_dac_rx_queue, &req, K_NO_WAIT) == 0)
    {    

        if (true
            && req.id == DAC_VALUE
            && cache_dac_data.value[0] != req.value.sd_uint
           )
        {
            cache_dac_data.value[0] = req.value.sd_uint;                            
            dac_write_value(dac_dev, 2, cache_dac_data.value[0]);
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
    int_dac_poll_events[0].state = K_POLL_STATE_NOT_READY;
    app_worker_poll_submit(&int_dac_task, int_dac_poll_events, ARRAY_SIZE(int_dac_poll_events), INT_DAC_K_WORK);  
}

/**
 *  @brief      Инициализация подсистемы ЦАП и цифрового потенциометра
 *  @details    Настраивает канал 2 ЦАП, проверяет готовность шины I2C,
 *              устанавливает начальные значения потенциометра и регистрирует воркер.
 *
 *  @return     int - Ноль при успехе, отрицательный код ошибки при сбое
 */
static int int_dac_control_init(void)
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
        .resolution = 12,
        .buffered   = true  
    };

    // Автоматически настраиваем канал 2 при старте системы
    err = dac_channel_setup(dac_dev, &dac_ch_cfg);
    if (err != 0) 
    {
        LOG_ERR("Failed to setup DAC channel 2 during boot: %d", err);
        return err;
    }


    k_poll_event_init(&int_dac_poll_events[0],
                      K_POLL_TYPE_MSGQ_DATA_AVAILABLE, // Тип: появление данных в очереди
                      K_POLL_MODE_NOTIFY_ONLY,         // Режим: только уведомить
                      &int_dac_rx_queue); 


    k_work_poll_init(&int_dac_task, _int_dac_update_handler); 
    app_worker_poll_submit(&int_dac_task, int_dac_poll_events,ARRAY_SIZE(int_dac_poll_events), INT_DAC_K_WORK);

    LOG_INF("DAC1 Channel 2 (PA5) successfully initialized!");
    return 0;
}

SYS_INIT(int_dac_control_init, DAC_DEAMON_INIT_LEVEL, DAC_DEAMON_INIT_PRIOR);
    
SERVER_OUT_CHANNEL_DEFINE(DAC_VALUE, &int_dac_chan, INT_DAC, UINT_DATA);


/***************************************************************************************************
 *                                           END OF FILE
 **************************************************************************************************/