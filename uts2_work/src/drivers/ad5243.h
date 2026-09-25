
#pragma once
#include <zephyr/kernel.h>
#include "stdint.h"

#define AD5243_DEFAULT_ADDR 0x2F

typedef enum {
    AD5243_CHANNEL_1 = 0,
    AD5243_CHANNEL_2 = 1
} ad5243_channel_t;



int ad5243_init(struct device *_i2c_dev, uint8_t _ch1_def, uint8_t _ch2_def);
int ad5243_set_wiper(ad5243_channel_t channel, uint8_t value);