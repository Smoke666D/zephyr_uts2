
#include <zephyr/kernel.h>
#include <stdio.h>
#include <string.h>
#include "dio_thread.h"
#include <zephyr/drivers/gpio.h>
#include <zephyr/device.h>
#include "system_status.h"
#include <zephyr/kernel.h>


static const struct gpio_dt_spec leds[] = {
    GPIO_DT_SPEC_GET(LED0_NODE, gpios),
    GPIO_DT_SPEC_GET(LED1_NODE, gpios),
    GPIO_DT_SPEC_GET(LED2_NODE, gpios),
};

#define NUM_LEDS 3


/* Определение канала */
ZBUS_CHAN_DEFINE(
    leds_chan,
    struct leds_state,
    NULL,
    NULL,
    ZBUS_OBSERVERS(led_sub),
    ZBUS_MSG_INIT(.led = {
        { .on = false, .blinking = false, .period_ms = 0 },
        { .on = false, .blinking = false, .period_ms = 0 },
        { .on = false, .blinking = false, .period_ms = 0 },
    })
);
/* Предыдущее состояние (для сравнения) */
static struct leds_state prev_state;

/* Таймеры для мигания */
static uint32_t last_toggle[NUM_LEDS];

/* Подписчик */
ZBUS_SUBSCRIBER_DEFINE(led_sub, 4);

static void apply_state(const struct leds_state *state)
{
    for (int i = 0; i < NUM_LEDS; i++) {
        if (!gpio_is_ready_dt(&leds[i])) {
            continue;
        }

        /* Изменилось ли состояние? */
        if (state->led[i].on != prev_state.led[i].on ||
            state->led[i].blinking != prev_state.led[i].blinking ||
            state->led[i].period_ms != prev_state.led[i].period_ms) {

            if (state->led[i].blinking) {
                /* Мигание: начальное включение */
                gpio_pin_set_dt(&leds[i], 1);
                last_toggle[i] = k_uptime_get();
            } else {
                /* Просто вкл/выкл */
                gpio_pin_set_dt(&leds[i], state->led[i].on ? 1 : 0);
            }
        }
    }

    /* Сохраняем как предыдущее */
    prev_state = *state;
}

static void handle_blinking(void)
{
    uint32_t now = k_uptime_get();

    for (int i = 0; i < NUM_LEDS; i++) {
        if (prev_state.led[i].blinking && prev_state.led[i].period_ms > 0) {
            if (now - last_toggle[i] >= prev_state.led[i].period_ms) {
                gpio_pin_toggle_dt(&leds[i]);
                last_toggle[i] = now;
            }
        }
    }
}


K_THREAD_STACK_DEFINE(stack1, STACK_SIZE);

static struct k_thread thread_data;

static void func(void *arg1, void *arg2, void *arg3)
{
    struct leds_state state;

    /* Инициализация */
    for (int i = 0; i < NUM_LEDS; i++) {
        if (gpio_is_ready_dt(&leds[i])) {
            gpio_pin_configure_dt(&leds[i], GPIO_OUTPUT_INACTIVE);
        }
        prev_state.led[i].on = false;
        prev_state.led[i].blinking = false;
        last_toggle[i] = k_uptime_get();
    }   
     const struct zbus_channel *chan = &leds_chan;  /* Указатель на канал */
    while (1) 
    {
        		
         int ret = zbus_sub_wait(&led_sub, &chan, K_MSEC(50));

        if (ret == 0) {
            /* Прочитали новый статус */         
            zbus_chan_read(&leds_chan, &state, K_NO_WAIT);
            apply_state(&state);
        }

        /* Мигание обрабатывается всегда */
       // handle_blinking();


      
    }
}

int dio_thread_start(void)
{
   

    
    // Создаем поток для работы с меню
    k_thread_create(&thread_data, stack1, STACK_SIZE,
                    func, NULL, NULL, NULL, PRIORITY, 0, K_NO_WAIT);


  // led_init();
	   
    return 0;
}