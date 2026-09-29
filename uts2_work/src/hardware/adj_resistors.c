/***************************************************************************************************
 *                                          INCLUDED FILES
 **************************************************************************************************/
#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include "stdint.h"
#include "app_worker.h"
#include "system_bus_model.h"
#include "ad5243.h"


/***************************************************************************************************
 *                                           DEFINITIONS
 **************************************************************************************************/

LOG_MODULE_REGISTER(adj_resistors, LOG_LEVEL_INF);

#define CHANNEL1_DEF_VAL 0
#define CHANNEL2_DEF_VAL 0

static struct k_work adj_res_task;

static bool _is_device_init = false;

/* 1. Сначала объявляем callback-функцию слушателя */
static void adj_res_zbus_listener_callback(const struct zbus_channel *chan)
{
    if (_is_device_init == true)
    {
        app_worker_submit(&adj_res_task);
    }
}


ZBUS_LISTENER_DEFINE(adj_res_listener, adj_res_zbus_listener_callback);

ZBUS_CHAN_DEFINE(
    adj_resistors_chain,
    FLOAT_ARRAY_CHANNEL_2_t,
    NULL,
    NULL,
    ZBUS_OBSERVERS(adj_res_listener),
    ZBUS_MSG_INIT(0)
);



/***************************************************************************************************
 *                                          PRIVATE TYPES
 **************************************************************************************************/


 /***************************************************************************************************
 *                                   PRIVATE FUNCTION PROTOTYPES
 **************************************************************************************************/
static uint8_t _adj_res_cache[2];
 
/***************************************************************************************************
 *                                        PRIVATE FUNCTIONS
 **************************************************************************************************/


/* 3. Обработчик воркера (физическое переключение пинов) */
static void adj_res_hardware_update_handler(struct k_work *work)
{    

     FLOAT_ARRAY_CHANNEL_2_t chain_data;
     if (zbus_chan_read(&adj_resistors_chain, &chain_data, K_MSEC(50)) == 0)
     {
        if (chain_data.value[0] != _adj_res_cache[0])
        {
            ad5243_set_wiper(AD5243_CHANNEL_1,(uint8_t)chain_data.value[0]);
            _adj_res_cache[0] = (uint8_t)chain_data.value[0];
        }
        if (chain_data.value[1] != _adj_res_cache[1])
        {
            ad5243_set_wiper(AD5243_CHANNEL_2,(uint8_t)chain_data.value[1]);
            _adj_res_cache[1] = (uint8_t)chain_data.value[1];
        }
     }
}

#define I2C1_NODE DT_NODELABEL(i2c1)


static int adj_resistor_init(void)
{
    if (ad5243_init(DEVICE_DT_GET(I2C1_NODE), CHANNEL1_DEF_VAL, CHANNEL2_DEF_VAL) == 0);
    {
        _is_device_init = true;
         _adj_res_cache[0] = CHANNEL1_DEF_VAL;
         _adj_res_cache[1] = CHANNEL2_DEF_VAL;
        k_work_init(&adj_res_task,  adj_res_hardware_update_handler);
    }
    return 0;   
}

SYS_INIT(adj_resistor_init, APPLICATION, 40);

PARAM_ROUTE_DEFINE(RESISTOR_1,  &adj_resistors_chain, 0, ARRAY_DATA);
PARAM_ROUTE_DEFINE(RESISTOR_2,  &adj_resistors_chain, 1, ARRAY_DATA);

 /***************************************************************************************************
 *                                           END OF FILE
 **************************************************************************************************/