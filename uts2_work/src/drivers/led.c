#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include "led.h"
#include "app_worker.h"
#include "param_server.h"

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

struct led_work_ctx {
    struct k_work work;
    struct led_state_msg state;
    struct k_sem done_sem;
};

static struct led_work_ctx led_task;

/* 1. Сначала объявляем callback-функцию слушателя */
static void led_zbus_listener_callback(const struct zbus_channel *chan)
{
    /* Объявление канала будет ниже, но сам указатель chan уже известен */
    extern const struct zbus_channel led_state_channel;
    
    if (chan == &led_state_channel) {
        const struct led_state_msg *msg = zbus_chan_msg(chan);
        led_task.state = *msg;
        app_worker_submit(&led_task.work);
    }
}

/* 2. Создаем самого слушателя (теперь символ 'led_listener' гарантированно существует) */
ZBUS_LISTENER_DEFINE(led_listener, led_zbus_listener_callback);

/* 3. Обработчик воркера (физическое переключение пинов) */
static void led_hardware_update_handler(struct k_work *work)
{
    struct led_work_ctx *ctx = CONTAINER_OF(work, struct led_work_ctx, work);

    if (ctx->state.update_mask & 0x01)
    gpio_pin_set_dt(&green_spec,  ctx->state.led[0] ? 1 : 0);
if (ctx->state.update_mask & 0x02)
    gpio_pin_set_dt(&yellow_spec, ctx->state.led[1] ? 1 : 0);
if (ctx->state.update_mask & 0x04)
    gpio_pin_set_dt(&red_spec,    ctx->state.led[2] ? 1 : 0);

  
        k_sem_give(&ctx->done_sem);
    
}

/* 4. Теперь определяем канал ZBUS, ссылаясь на уже созданный 'led_listener' */
ZBUS_CHAN_DEFINE(led_state_channel,
                 struct led_state_msg,
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

    k_work_init(&led_task.work, led_hardware_update_handler);

    LOG_INF("Светодиоды успешно инициализированы.");
    return 0;
}


SYS_INIT(led_manager_system_init, APPLICATION, 40);


static int _led_set(PARAM_ID _id, const PARAM_VAL *_val,  bool is_sync)
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

}

PARAM_ROUTE_WO(LED1,   _led_set);
PARAM_ROUTE_WO(LED2,   _led_set);
PARAM_ROUTE_WO(LED3,   _led_set);