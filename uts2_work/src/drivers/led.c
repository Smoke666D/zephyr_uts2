#include "global_params.h"
#include "led.h"


ZBUS_CHAN_DEFINE(led_chan,
                 led_state_t,
                 NULL,
                 NULL,
                 ZBUS_OBSERVERS_EMPTY,
                 ZBUS_MSG_INIT(0)
);