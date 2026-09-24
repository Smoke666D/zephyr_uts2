#pragma once
#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>

#define ADS1115_DEFAULT_ADDR 0x4B

struct ads1115_snapshot {
    float voltages_mv[4];
    bool channel_ready[4];
};

/* Объявляем Zbus канал для АЦП */
ZBUS_CHAN_DECLARE(ads_channel);