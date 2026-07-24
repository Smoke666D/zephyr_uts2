
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
    AO1,
    AO2,
    AO3,
    AO4,
    AO5,
    AO6,
    AO7,
    AO8,
    AO9,
    AO10,
    AO11,
    AO12,
    AO13,
    AO14,
    AO15,
    AO16,
    AO17,
    AO18
} AIN_MUX_CHANNEL_NUMBER;

#ifdef __cplusplus
}
#endif
