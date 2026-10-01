
#pragma once

/***************************************************************************************************
 *                                      INCLUDED FILES
 **************************************************************************************************/
#include <stdint.h>
#include <stdbool.h>
#include <zephyr/zbus/zbus.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DISPATCHER_STACK_SIZE 2048

typedef enum
{
    STATE_IN = 0,
    STATE_LOW = 1,
    STATE_HIGH = 2,
} LOW_CUR_OUT_STATE_t;

#define LOW_CUR_DRIVER_COUNT 18
#define LIN_CONTROL_CNT  4
#define CHANNEL_DATA_SIZE (LOW_CUR_DRIVER_COUNT + LIN_CONTROL_CNT)

typedef struct hc595_channels_msg {
    float channels_mv[CHANNEL_DATA_SIZE]; // Наш тестовый буфер (8 шагов * 2 канала)
};

#ifdef __cplusplus
}
#endif
