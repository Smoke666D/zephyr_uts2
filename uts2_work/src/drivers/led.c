#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include "led.h"
#include "app_worker.h"

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

    LOG_INF("ФИЗИЧЕСКОЕ ПЕРЕКЛЮЧЕНИЕ LED:");
    LOG_INF("  Green (LD1)  -> %s", ctx->state.led1 ? "ON" : "OFF");
    LOG_INF("  Yellow (LD2) -> %s", ctx->state.led2 ? "ON" : "OFF");
    LOG_INF("  Red (LD3)    -> %s", ctx->state.led3 ? "ON" : "OFF");

    gpio_pin_set_dt(&green_spec,  ctx->state.led1 ? 1 : 0);
    gpio_pin_set_dt(&yellow_spec, ctx->state.led2 ? 1 : 0);
    gpio_pin_set_dt(&red_spec,    ctx->state.led3 ? 1 : 0);

    k_msleep(40);
    k_sem_give(&ctx->done_sem);
}

/* 4. Теперь определяем канал ZBUS, ссылаясь на уже созданный 'led_listener' */
ZBUS_CHAN_DEFINE(led_state_channel,
                 struct led_state_msg,
                 NULL,
                 NULL,
                 ZBUS_OBSERVERS(led_listener),
                 ZBUS_MSG_INIT(.led1 = false, .led2 = false, .led3 = false)
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

int led_manager_set_states_sync(bool l1, bool l2, bool l3, k_timeout_t timeout)
{
    struct led_state_msg new_state = { .led1 = l1, .led2 = l2, .led3 = l3 };

    k_sem_init(&led_task.done_sem, 0, 1);

    int err = zbus_chan_pub(&led_state_channel, &new_state, K_NO_WAIT);
    if (err) {
        LOG_ERR("Ошибка публикации в ZBUS: %d", err);
        return err;
    }

    err = k_sem_take(&led_task.done_sem, timeout);
    if (err == -EAGAIN) {
        LOG_WRN("Таймаут синхронизации светодиодов!");
        return -ETIMEDOUT;
    }

    return 0;
}

SYS_INIT(led_manager_system_init, APPLICATION, 40);