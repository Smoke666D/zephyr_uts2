#pragma once

#include <stdbool.h>
#include <zephyr/zbus/zbus.h>

/* Полное описание структуры */
typedef struct {
    bool led1_state;
    bool led2_state;
    bool led3_state;
} led_state_t;

ZBUS_CHAN_DECLARE(led_chan);