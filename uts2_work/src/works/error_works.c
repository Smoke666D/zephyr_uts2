#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include "app_worker.h"
#include "system_bus_model.h"

/***************************************************************************************************
 *                                           DEFINITIONS
 **************************************************************************************************/
LOG_MODULE_REGISTER(error_manager, LOG_LEVEL_INF);



typedef struct 
{
    uint32_t  value[3];    
} error_channel_t;


static int  _error_manager_system_init(void);

/***************************************************************************************************
 *                                           PRIVATE DATA
 **************************************************************************************************/


// Предварительная декларация канала ZBUS




ZBUS_CHAN_DEFINE(
    error_channel,
    error_channel_t,
    NULL,
    NULL,
    ZBUS_OBSERVERS_EMPTY,
    ZBUS_MSG_INIT(0)
);

/***************************************************************************************************
 *                                        PRIVATE FUNCTIONS
 **************************************************************************************************/


static int _error_manager_system_init(void)
{   
    return 0;
}

SYS_INIT(_error_manager_system_init, APPLICATION, 10);

PARAM_ROUTE_DEFINE(HARDWARE_ERROR_REGISTER1, &error_channel, 0, UINT_DATA);
PARAM_ROUTE_DEFINE(HARDWARE_ERROR_REGISTER2, &error_channel, 1, UINT_DATA);
PARAM_ROUTE_DEFINE(HARDWARE_ERROR_REGISTER3, &error_channel, 2, UINT_DATA);