/**
 *  @file       led_manager.c
 *  @headerfile led_manager.h
 *
 *  @date       2026.10.21
 *  @author     Dymov Igor
 *
 *  @brief      Менеджер дискретного управления светодиодами
 *  @details    Принимает состояния светодиодов из очереди сообщений и канала ZBUS,
 *              управляя физическими выводами GPIO зеленого, желтого и красного светодиодов.
 */

/***************************************************************************************************
 *                                          INCLUDED FILES
 **************************************************************************************************/
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include "app_worker.h"
#include "system_bus_model.h"
#include "system_define.h"

/***************************************************************************************************
 *                                           DEFINITIONS
 **************************************************************************************************/
LOG_MODULE_REGISTER(led_manager, LOG_LEVEL_INF);

#define GREEN_LED_NODE  DT_NODELABEL(green_led)
#define YELLOW_LED_NODE DT_NODELABEL(yellow_led)
#define RED_LED_NODE    DT_NODELABEL(red_led)


/***************************************************************************************************
 *                                          PRIVATE TYPES
 **************************************************************************************************/
typedef struct 
{
    bool value[3];    
} led_channel_t;

/***************************************************************************************************
 *                                   PRIVATE FUNCTION PROTOTYPES
 **************************************************************************************************/
static void _led_hardware_update_handler(struct k_work *_work);
static int  _led_manager_system_init(void);

/***************************************************************************************************
 *                                           PRIVATE DATA
 **************************************************************************************************/
static const struct gpio_dt_spec leds[LED_CNT] =
{    
    GPIO_DT_SPEC_GET(GREEN_LED_NODE, gpios),
    GPIO_DT_SPEC_GET(YELLOW_LED_NODE, gpios),
    GPIO_DT_SPEC_GET(RED_LED_NODE, gpios)
};

static led_channel_t      cache_value     __attribute__((section("DTCM")));
static struct k_work_poll led_task        __attribute__((section("DTCM")));
static struct k_poll_event led_poll_events[1] __attribute__((section("DTCM")));

K_MSGQ_DEFINE(led_rx_queue, sizeof(server_request), 3, 4);

ZBUS_CHAN_DEFINE(
    led_state_channel,
    led_channel_t,
    NULL,
    &led_rx_queue,
    ZBUS_OBSERVERS_EMPTY,
    ZBUS_MSG_INIT(0)
);

/***************************************************************************************************
 *                                        PRIVATE FUNCTIONS
 **************************************************************************************************/

/**
 *  @brief      Обработчик воркера аппаратного обновления светодиодов
 *  @details    Вычитывает команду из очереди запросов, обновляет кэш состояний
 *              и переключает вывод GPIO при изменении уровня.
 *
 *  @param      _work - Указатель на структуру задачи
 */
static void _led_hardware_update_handler(struct k_work *_work)
{
    ARG_UNUSED(_work);
    server_request req;
    int err = 0;
    // Считываем запрос из очереди с проверкой успешности
    if (k_msgq_get(&led_rx_queue, &req, K_NO_WAIT) == 0)
    {
        uint8_t led_index = req.id - LED1;
        if (led_index < LED_CNT)
        {
            if (req.value.sd_bool != cache_value.value[led_index])
            {
                cache_value.value[led_index] = req.value.sd_bool;
                gpio_pin_set_dt(&leds[led_index], cache_value.value[led_index] ? 1 : 0);        
                
            }
            zbus_chan_pub(&led_state_channel, &cache_value, K_MSEC(2));
        }
        else 
        {
            err = -EINVAL;
        }        
    }
    if (req.sync != NULL)
    {
        if (req.response != NULL)
        {
            *req.response = 0;
        }
        k_sem_give(req.sync);
    }
    // Сброс статуса события перед повторным сабмитом поллинга
    led_poll_events[0].state = K_POLL_STATE_NOT_READY;
    app_worker_poll_submit(&led_task, led_poll_events, COMMON_WORKER);    
}

/**
 *  @brief      Системная инициализация менеджера светодиодов
 *  @details    Проверяет готовность портов GPIO, конфигурирует пины 
 *              светодиодов на выход и настраивает задачу поллинга очереди.
 *
 *  @return     int - Ноль при успехе, отрицательный код ошибки при сбое
 */
static int _led_manager_system_init(void)
{
    int ret;

    for (int i = 0; i < LED_CNT; i++)
    {
        if (!device_is_ready(leds[i].port))
        {         
            LOG_ERR("LED Ports not ready!");
            return -ENODEV;
        }

        ret = gpio_pin_configure_dt(&leds[i], GPIO_OUTPUT_INACTIVE);
        if (ret < 0) 
        {
            return ret;
        }
    }

    k_poll_event_init(&led_poll_events[0],
                      K_POLL_TYPE_MSGQ_DATA_AVAILABLE, // Тип: появление данных в очереди
                      K_POLL_MODE_NOTIFY_ONLY,         // Режим: только уведомить
                      &led_rx_queue); 

    k_work_poll_init(&led_task, _led_hardware_update_handler); 
    app_worker_poll_submit(&led_task, led_poll_events, COMMON_WORKER);

    LOG_INF("LEDs initialized successfully...");
    return 0;
}

SYS_INIT(_led_manager_system_init, LED_DEAMON_INIT_LEVEL, LED_DEAMON_INIT_PRIOR);

SERVER_OUT_CHANNEL_DEFINE(LED1, &led_state_channel, 0, BOOL_DATA);
SERVER_OUT_CHANNEL_DEFINE(LED2, &led_state_channel, 1, BOOL_DATA);
SERVER_OUT_CHANNEL_DEFINE(LED3, &led_state_channel, 2, BOOL_DATA);

/***************************************************************************************************
 *                                           END OF FILE
 **************************************************************************************************/