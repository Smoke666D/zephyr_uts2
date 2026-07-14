#ifndef SYSTEM_STATUS_H
#define SYSTEM_STATUS_H

#include <zephyr/zbus/zbus.h>

/* Статус одного светодиода */
struct led_status 
{
    bool on;
    bool blinking;
    int period_ms;
};

/* Статус всех светодиодов */
struct leds_state {
    struct led_status led[3];
};

extern const struct zbus_channel leds_chan;




#endif