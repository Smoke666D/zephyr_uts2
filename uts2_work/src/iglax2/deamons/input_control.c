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


typedef enum
{
    INPUT_GRAY  = 0,
    INPUT_IGN   = 1,
    INPUT_3     = 2,
    INPUT_4     = 3,
    INPUT_CNT   = 4,
} din_names_t;


#define DINPUT_POLL_RATE_MS    1U   // Период опроса воркера = 1 мс

/***************************************************************************************************
 *                                          PRIVATE TYPES
 **************************************************************************************************/
typedef struct
{
    bool value[INPUT_CNT];
} dinput_state_t;

typedef struct
{
    uint32_t value[INPUT_CNT*2];
} dinput_gf_t;

typedef struct {
    const struct gpio_dt_spec gpio; // Привязка к пину DeviceTree
    uint32_t delay_on_ms;           // Задержка подтверждения лог. 1 (ON = 0В на пине)
    uint32_t delay_off_ms;          // Задержка подтверждения лог. 0 (OFF = 3.3В на пине)
    bool     current_raw;           // Текущее физическое состояние
    bool     debounced;             // Фильтрованное состояние
    uint32_t counter_ms;            // Таймер удержания
} dinput_ch_t;

static void _dinput_setting_listener_cb(const struct zbus_channel *_chan);

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

ZBUS_LISTENER_DEFINE(dinput_setting_listener, _dinput_setting_listener_cb);

ZBUS_CHAN_DEFINE(
    dinput_setting_chain,
    dinput_gf_t,
    NULL,
    NULL,
    dinput_setting_listener,
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

/* Массив входов: извлекаем ножки по алиасам din-gray, din-ign, и т.д. */
static dinput_ch_t dinputs[INPUT_CNT] = {
    { .gpio = GPIO_DT_SPEC_GET_OR(DT_NODELABEL(gray_in),   gpios, {0}), .delay_on_ms = 20, .delay_off_ms = 20 },
    { .gpio = GPIO_DT_SPEC_GET_OR(DT_NODELABEL(ign_in),    gpios, {0}), .delay_on_ms = 20, .delay_off_ms = 20 },
    { .gpio = GPIO_DT_SPEC_GET_OR(DT_NODELABEL(in_ch3),    gpios, {0}), .delay_on_ms = 20, .delay_off_ms = 20 },
    { .gpio = GPIO_DT_SPEC_GET_OR(DT_NODELABEL(in_ch4),    gpios, {0}), .delay_on_ms = 20, .delay_off_ms = 20 }
};



/***************************************************************************************************
 *                                        PRIVATE FUNCTIONS
 **************************************************************************************************/

static void _dinput_setting_listener_cb(const struct zbus_channel *_chan)
{
    dinput_gf_t * _msg = zbus_chan_msg(&dinput_setting_chain);
    for (int i = 0; i < INPUT_CNT; i++)
    {

        if (dinputs[i].delay_on_ms !=  _msg->value[i*2])
        {
            dinputs[i].delay_on_ms =  _msg->value[i*2];
        }
        if (dinputs[i].delay_off_ms !=  _msg->value[i*2 + 1])
        {
            dinputs[i].delay_on_ms =  _msg->value[i*2 + 1];
        }
    }
}


static void _publish_dinput_state(void)
{
    dinput_state_t msg;
    for (int i = 0; i < INPUT_CNT; i++) {
        msg.value[i] = dinputs[i].debounced;
    }
    zbus_chan_pub(&dinput_chain, &msg, K_MSEC(5));
}

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
    bool state_changed = false;

    for (int i = 0; i < INPUT_CNT; i++) 
    {
        dinput_ch_t *ch = &dinputs[i];

        if (!gpio_is_ready_dt(&ch->gpio)) {
            continue;
        }

        // Читаем физическое состояние. 
        // Важно: благодаря флагу GPIO_ACTIVE_LOW в DTS, если на ножке 0 Вольт (транзистор замкнут), 
        // функция вернет 1 (true). Это означает, что вход в состоянии ON.
        bool raw_state = (gpio_pin_get_dt(&ch->gpio) > 0);

        if (raw_state != ch->current_raw) 
        {
            ch->current_raw = raw_state;
            ch->counter_ms = 0; // Искрение/дребезг: сбрасываем счетчик
        } 
        else if (raw_state != ch->debounced) 
        {
            ch->counter_ms += DINPUT_POLL_RATE_MS;

            // Раздельная фильтрация: для включения (ON) и выключения (OFF)
            uint32_t threshold = raw_state ? ch->delay_on_ms : ch->delay_off_ms;

            if (ch->counter_ms >= threshold) 
            {
                ch->debounced = raw_state;
                state_changed = true;
            }
        }
    }

    if (state_changed) {
        _publish_dinput_state();
    }

    app_worker_reschedule_submit(&dinputs_dwork, K_MSEC(DINPUT_POLL_RATE_MS), DINPUT_K_WORK);
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
for (int i = 0; i < INPUT_CNT; i++) 
    {
        dinput_ch_t *ch = &dinputs[i];

        if (gpio_is_ready_dt(&ch->gpio)) {
            // Флаги (PULL_UP | ACTIVE_LOW) автоматически подхватятся из DTS
            gpio_pin_configure_dt(&ch->gpio, GPIO_INPUT);
            
            ch->current_raw = (gpio_pin_get_dt(&ch->gpio) > 0);
            ch->debounced   = ch->current_raw;
            ch->counter_ms  = 0;
        } else {
            LOG_ERR("DInput [%d] GPIO is not ready!", i);
        }
    }


    // Публикация стартового состояния
    _publish_dinput_state();

    // Запуск цикла опроса
    k_work_init_delayable(&dinputs_dwork, _dinput_poll_handler);
    app_worker_reschedule_submit(&dinputs_dwork, K_MSEC(DINPUT_POLL_RATE_MS), DINPUT_K_WORK);

    LOG_INF("Discrete input daemon initialized (Poll: %d ms).", DINPUT_POLL_RATE_MS);
    return 0;
}

SYS_INIT(_dinput_system_init, DINPUT_DEAMON_INIT_LEVEL, DINPUT_DEAMON_INIT_PRIOR);
    

SERVER_IN_CHANNEL_DEFINE(IN_GREY,   &dinput_chain, INPUT_GRAY,  BOOL_DATA);
SERVER_IN_CHANNEL_DEFINE(IN_IGN,    &dinput_chain, INPUT_IGN ,  BOOL_DATA);
SERVER_IN_CHANNEL_DEFINE(IN3,       &dinput_chain, INPUT_3,     BOOL_DATA);
SERVER_IN_CHANNEL_DEFINE(IN4,       &dinput_chain, INPUT_4,     BOOL_DATA);

SERVER_SETTING_CHANNEL_DEFINE(IN_GREY_ON_GF,    &dinput_setting_chain, 0 , UINT_DATA);
SERVER_SETTING_CHANNEL_DEFINE(IN_GREY_OFF_GF,   &dinput_setting_chain, 1 , UINT_DATA);
SERVER_SETTING_CHANNEL_DEFINE(IN_IGN_ON_GF,     &dinput_setting_chain, 2 , UINT_DATA);
SERVER_SETTING_CHANNEL_DEFINE(IN_IGN_OFF_GF,    &dinput_setting_chain, 3 , UINT_DATA);
SERVER_SETTING_CHANNEL_DEFINE(IN_3_ON_GF,       &dinput_setting_chain, 4 , UINT_DATA);
SERVER_SETTING_CHANNEL_DEFINE(IN_3_OFF_GF,      &dinput_setting_chain, 5 , UINT_DATA);
SERVER_SETTING_CHANNEL_DEFINE(IN_4_ON_GF,       &dinput_setting_chain, 6 , UINT_DATA);
SERVER_SETTING_CHANNEL_DEFINE(IN_4_OFF_GF,      &dinput_setting_chain, 7 , UINT_DATA);
