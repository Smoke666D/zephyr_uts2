#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include "led.h"
#include "app_worker.h"
#include "system_bus_model.h"

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

static const struct gpio_dt_spec green_spec  = GPIO_DT_SPEC_GET(GREEN_LED_NODE, gpios);
static const struct gpio_dt_spec yellow_spec = GPIO_DT_SPEC_GET(YELLOW_LED_NODE, gpios);
static const struct gpio_dt_spec red_spec    = GPIO_DT_SPEC_GET(RED_LED_NODE, gpios);

typedef struct 
{ 
    bool     led_state[32];
} led_command_t;



K_MSGQ_DEFINE(led_queue, sizeof(led_command_t), 4, 4);

static struct k_work led_task;

/* 1. Сначала объявляем callback-функцию слушателя */
static void led_zbus_listener_callback(const struct zbus_channel *chan)
{
    /* Объявление канала будет ниже, но сам указатель chan уже известен */
    extern const struct zbus_channel led_state_channel;
    
    if (chan == &led_state_channel)
    {
        const BOOLEAN_ARRAY_CHANNEL_t *msg = zbus_chan_const_msg(chan);
        led_command_t _msg;
        memcpy(&_msg.led_state,&msg->value,sizeof(led_command_t));
        k_msgq_put(&led_queue, &_msg, K_NO_WAIT);
        app_worker_submit(&led_task);
    }
}

/* 2. Создаем самого слушателя (теперь символ 'led_listener' гарантированно существует) */
ZBUS_LISTENER_DEFINE(led_listener, led_zbus_listener_callback);

/* 3. Обработчик воркера (физическое переключение пинов) */
static void led_hardware_update_handler(struct k_work *work)
{    
    bool led_state[32];
    while (k_msgq_get(&led_queue, led_state, K_NO_WAIT) == 0)
    {
        gpio_pin_set_dt(&green_spec,  led_state[0] ? 1 : 0);
        gpio_pin_set_dt(&yellow_spec, led_state[1] ? 1 : 0);
        gpio_pin_set_dt(&red_spec,    led_state[2] ? 1 : 0);            
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

/* 5. Инициализация железа через SYS_INIT */
static int led_manager_system_init(void)
{
    int ret;

    LOG_INF("Инициализация портов светодиодов...");

    if (!device_is_ready(green_spec.port) || 
        !device_is_ready(yellow_spec.port) || 
        !device_is_ready(red_spec.port)) {
        LOG_ERR("Один из портов GPIO светодиодов не готов!");
        return -ENODEV;
    }

    ret = gpio_pin_configure_dt(&green_spec, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) return ret;
    ret = gpio_pin_configure_dt(&yellow_spec, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) return ret;
    ret = gpio_pin_configure_dt(&red_spec, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) return ret;

    k_work_init(&led_task, led_hardware_update_handler);

    LOG_INF("Светодиоды успешно инициализированы.");
    return 0;
}


SYS_INIT(led_manager_system_init, APPLICATION, 40);


/*static int _led_set(PARAM_ID _id, const PARAM_VAL *_val,  bool is_sync)
{
    if (true
        && _id >= LED1
        && _id <= LED3
    )
    {
        struct led_state_msg new_state;
        new_state.led[_id - LED1] = _val->value.boolean;
        new_state.update_mask = 0x01 << (_id - LED1);
        
        k_sem_init(&led_task.done_sem, 0, 1);
       
        zbus_chan_pub(&led_state_channel, &new_state, K_NO_WAIT);
        if (is_sync == true)
        {
            k_sem_take(&led_task.done_sem, K_MSEC(100));
        }

    }

}*/

PARAM_ROUTE_DEFINE(LED1,&led_state_channel,0,ARRAY_DATA);
PARAM_ROUTE_DEFINE(LED2,&led_state_channel,1,ARRAY_DATA);
PARAM_ROUTE_DEFINE(LED3,&led_state_channel,2,ARRAY_DATA);
