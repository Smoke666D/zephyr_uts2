#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include "app_worker.h"
#include "system_bus_model.h"

#define LED_CNT 3


LOG_MODULE_REGISTER(led_manager, LOG_LEVEL_INF);

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



typedef struct 
{ 
    bool     led_state[32];
} led_command_t;

typedef struct 
{
    const struct gpio_dt_spec spec; 
    bool state;
} led_heandler_t;

led_heandler_t leds[LED_CNT] =
{
    [0] = 
    {
        .spec = GPIO_DT_SPEC_GET(GREEN_LED_NODE, gpios),
        .state =false,
    },
    [1] = 
    {
        .spec = GPIO_DT_SPEC_GET(YELLOW_LED_NODE, gpios),
        .state =false,
    },
    [2] = 
    {
        .spec = GPIO_DT_SPEC_GET(RED_LED_NODE, gpios),
        .state =false,
    }
};



//K_MSGQ_DEFINE(led_queue, sizeof(led_command_t), 4, 4);

static struct k_work led_task;

/* 1. Сначала объявляем callback-функцию слушателя */
static void led_zbus_listener_callback(const struct zbus_channel *chan)
{
    /* Объявление канала будет ниже, но сам указатель chan уже известен */
    extern const struct zbus_channel led_state_channel;
    
    if (chan == &led_state_channel)
    {
        app_worker_submit(&led_task,COMMON_WORKER);
    }
}


/* 4. Теперь определяем канал ZBUS, ссылаясь на уже созданный 'led_listener' */
ZBUS_CHAN_DEFINE(led_state_channel,
                 BOOLEAN_ARRAY_CHANNEL_t,
                 NULL,
                 NULL,
                 ZBUS_OBSERVERS(led_listener),
                 ZBUS_MSG_INIT(0)
);

/* 2. Создаем самого слушателя (теперь символ 'led_listener' гарантированно существует) */
ZBUS_LISTENER_DEFINE(led_listener, led_zbus_listener_callback);

/* 3. Обработчик воркера (физическое переключение пинов) */
static void led_hardware_update_handler(struct k_work *work)
{    
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



/* 5. Инициализация железа через SYS_INIT */
static int led_manager_system_init(void)
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
        if (ret < 0) return ret;
    }

    
    

    k_work_init(&led_task, led_hardware_update_handler);

    LOG_INF("Светодиоды успешно инициализированы.");
    return 0;
}


SYS_INIT(led_manager_system_init, APPLICATION, 40);


PARAM_ROUTE_DEFINE(LED1,&led_state_channel,0,ARRAY_DATA);
PARAM_ROUTE_DEFINE(LED2,&led_state_channel,1,ARRAY_DATA);
PARAM_ROUTE_DEFINE(LED3,&led_state_channel,2,ARRAY_DATA);