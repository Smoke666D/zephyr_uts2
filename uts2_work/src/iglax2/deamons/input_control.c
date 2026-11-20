/**
 *  @file       app_worker.c
 *  @headerfile app_worker.h
 *
 *  @date       2026.09.26
 *  @author     Dymov Igor
 *
 *  @brief      Планировщик фонового опроса I2C-датчиков температуры и освещенности
 *  @details    Реализует периодический опрос датчиков TMP112 и BH1750 на базе
 *              отложенной задачи воркера Zephyr RTOS и публикацию результатов в ZBUS.
 */

/***************************************************************************************************
 *                                          INCLUDED FILES
 **************************************************************************************************/
#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/gpio.h>


#include "app_worker.h"
#include "system_define.h"
#include "system_bus_model.h"

/***************************************************************************************************
 *                                          PRIVATE TYPES
 **************************************************************************************************/
typedef struct
{
    bool value[4];
} dinput_state_t;


/***************************************************************************************************
 *                                           DEFINITIONS
 **************************************************************************************************/
LOG_MODULE_REGISTER(dinput_module, LOG_LEVEL_INF);


ZBUS_CHAN_DEFINE(
    dinput_chain,
    dinput_state_t,
    NULL,
    NULL,
    ZBUS_OBSERVERS_EMPTY,
    ZBUS_MSG_INIT(0)
);

/***************************************************************************************************
 *                                   PRIVATE FUNCTION PROTOTYPES
 **************************************************************************************************/
static void _dinput_poll_handler(struct k_work *_work);
static int  _dinput_system_init(void);

/***************************************************************************************************
 *                                           PRIVATE DATA
 **************************************************************************************************/
static struct k_work_delayable dinputs_dwork;



/***************************************************************************************************
 *                                        PRIVATE FUNCTIONS
 **************************************************************************************************/

/**
 *  @brief      Обработчик отложенного воркера опроса датчиков
 *  @details    Последовательно опрашивает все активные датчики в слотах кэша,
 *              конвертирует сырые показания в float и отправляет их в ZBUS.
 *
 *  @param      _work - Указатель на структуру задачи
 */
static void _dinput_poll_handler(struct k_work *_work)
{
    ARG_UNUSED(_work);
    
    app_worker_reschedule_submit(&dinputs_dwork, K_MSEC(10),DINPUT_K_WORK);
}

/**
 *  @brief      Инициализация системного планировщика опроса датчиков
 *  @details    Инициализирует структуру отложенной задачи и ставит её
 *              в очередь на первый запуск через 100 мс.
 *
 *  @return     int - Ноль при успешной инициализации
 */
static int _dinput_system_init(void)
{
    k_work_init_delayable(&dinputs_dwork, _dinput_poll_handler);
    
    app_worker_reschedule_submit(&dinputs_dwork, K_MSEC(100),DINPUT_K_WORK);

    LOG_INF("Discrete input deamon init successfull!");
    return 0;
}

SYS_INIT(_dinput_system_init, DINPUT_DEAMON_INIT_LEVEL, DINPUT_DEAMON_INIT_PRIOR);

SERVER_IN_CHANNEL_DEFINE(IN_GREAY,  &dinput_chain, 0, BOOL_DATA);
SERVER_IN_CHANNEL_DEFINE(IN_IGN,    &dinput_chain, 1, BOOL_DATA);
SERVER_IN_CHANNEL_DEFINE(IN3,       &dinput_chain, 2, BOOL_DATA);
SERVER_IN_CHANNEL_DEFINE(IN4,       &dinput_chain, 3, BOOL_DATA);