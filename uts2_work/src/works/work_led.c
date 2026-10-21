/**
 *  @file       led_manager.c
 *  @headerfile led_manager.h
 *
 *  @date       2026.10.21
 *  @author     Dymov Igor
 *
 *  @brief      Менеджер дискретного управления светодиодами
 *  @details    Принимает состояния светодиодов из канала ZBUS и переключает
 *              физические выводы GPIO зеленым, желтым и красным светодиодами.
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

/***************************************************************************************************
 *                                           DEFINITIONS
 **************************************************************************************************/
LOG_MODULE_REGISTER(led_manager, LOG_LEVEL_INF);

#define LED_CNT 3

#define GREEN_LED_NODE  DT_NODELABEL(green_led)
#define YELLOW_LED_NODE DT_NODELABEL(yellow_led)
#define RED_LED_NODE    DT_NODELABEL(red_led)

#if !DT_NODE_HAS_STATUS(GREEN_LED_NODE, okay)
#error "Узел green_led не найден в DeviceTree!"
#endif
#if !DT_NODE_HAS_STATUS(YELLOW_LED_NODE, okay)
#error "Узел yellow_led не найден в DeviceTree!"
#endif
#if !DT_NODE_HAS_STATUS(RED_LED_NODE, okay)
#error "Узел red_led не найден в DeviceTree!"
#endif

/***************************************************************************************************
 *                                          PRIVATE TYPES
 **************************************************************************************************/
typedef struct 
{ 
    bool led_state[32];
} led_command_t;

/**
 *  @brief Структура дескриптора светодиода
 */
typedef struct led_handler 
{
    const struct gpio_dt_spec spec; 
    bool                       state;
} led_handler_t;

/***************************************************************************************************
 *                                   PRIVATE FUNCTION PROTOTYPES
 **************************************************************************************************/
static void _led_zbus_listener_callback(const struct zbus_channel *_chan);
static void _led_hardware_update_handler(struct k_work *_work);
static int  _led_manager_system_init(void);

/***************************************************************************************************
 *                                           PRIVATE DATA
 **************************************************************************************************/
static led_handler_t leds[LED_CNT] =
{
    [0] = 
    {
        .spec  = GPIO_DT_SPEC_GET(GREEN_LED_NODE, gpios),
        .state = false,
    },
    [1] = 
    {
        .spec  = GPIO_DT_SPEC_GET(YELLOW_LED_NODE, gpios),
        .state = false,
    },
    [2] = 
    {
        .spec  = GPIO_DT_SPEC_GET(RED_LED_NODE, gpios),
        .state = false,
    }
};

static struct k_work led_task;

// Предварительная декларация канала ZBUS
ZBUS_CHAN_DECLARE(led_state_channel);

ZBUS_LISTENER_DEFINE(led_listener, _led_zbus_listener_callback);

ZBUS_CHAN_DEFINE(
    led_state_channel,
    BOOLEAN_ARRAY_CHANNEL_t,
    NULL,
    NULL,
    ZBUS_OBSERVERS(led_listener),
    ZBUS_MSG_INIT(0)
);

/***************************************************************************************************
 *                                        PRIVATE FUNCTIONS
 **************************************************************************************************/

/**
 *  @brief      Коллбэк слушателя ZBUS для состояния светодиодов
 *  @details    При изменении состояния в канале led_state_channel отправляет
 *              задачу обновления аппаратных выводов в воркер.
 *
 *  @param      _chan - Указатель на канал ZBUS
 */
static void _led_zbus_listener_callback(const struct zbus_channel *_chan)
{
    if (_chan == &led_state_channel)
    {
        app_worker_submit(&led_task, COMMON_WORKER);
    }
}

/**
 *  @brief      Обработчик воркера аппаратного обновления светодиодов
 *  @details    Вычитывает массив состояний из ZBUS и обновляет физические 
 *              уровни на пинах GPIO только при изменении состояния.
 *
 *  @param      _work - Указатель на структуру задачи
 */
static void _led_hardware_update_handler(struct k_work *_work)
{    
    ARG_UNUSED(_work);
    bool led_state[32];   

    if (zbus_chan_read(&led_state_channel, led_state, K_MSEC(50)) == 0)
    {
        for (int i = 0; i < LED_CNT; i++)
        {            
            if (led_state[i] != leds[i].state)
            {
                leds[i].state = led_state[i];
                gpio_pin_set_dt(&leds[i].spec, led_state[i] ? 1 : 0);        
            }
        } 
    }
}

/**
 *  @brief      Системная инициализация менеджера светодиодов
 *  @details    Проверяет готовность портов GPIO, конфигурирует пины 
 *              светодиодов на выход и инициализирует воркер.
 *
 *  @return     int - Ноль при успехе, отрицательный код ошибки при сбое
 */
static int _led_manager_system_init(void)
{
    int ret;

    LOG_INF("Инициализация портов светодиодов...");

    for (int i = 0; i < LED_CNT; i++)
    {
        if (!device_is_ready(leds[i].spec.port))
        {         
            LOG_ERR("Один из портов GPIO светодиодов не готов!");
            return -ENODEV;
        }

        ret = gpio_pin_configure_dt(&leds[i].spec, GPIO_OUTPUT_INACTIVE);
        if (ret < 0) 
        {
            return ret;
        }
    }

    k_work_init(&led_task, _led_hardware_update_handler);

    LOG_INF("Светодиоды успешно инициализированы.");
    return 0;
}

SYS_INIT(_led_manager_system_init, APPLICATION, 40);

PARAM_ROUTE_DEFINE(LED1, &led_state_channel, 0, ARRAY_DATA);
PARAM_ROUTE_DEFINE(LED2, &led_state_channel, 1, ARRAY_DATA);
PARAM_ROUTE_DEFINE(LED3, &led_state_channel, 2, ARRAY_DATA);

/***************************************************************************************************
 *                                           END OF FILE
 **************************************************************************************************/