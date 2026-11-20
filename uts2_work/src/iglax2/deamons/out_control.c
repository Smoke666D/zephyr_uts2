#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

#include "system_define.h"
#include "system_bus_model.h"
#include "app_worker.h"

LOG_MODULE_REGISTER(input_control_module, LOG_LEVEL_INF);

#define OUT_CONTROL_LINE_CNT 11

typedef struct out_control_line_msg 
{
    bool state[OUT_CONTROL_LINE_CNT]; // Наш тестовый буфер (8 шагов * 2 канала)
} out_control_line_msg ;


/***********************************************************************************
 *                                   PRIVATE FUNCTION PROTOTYPES
 **********************************************************************************/

static void _enable_port_init(void);
static void  _out_ctl_update_handler(struct k_work *_work);

/***************************************************************************************************
 *                                           PRIVATE DATA
 **************************************************************************************************/

static struct k_work_poll  out_ctl_task;
static struct k_poll_event out_ctl_poll_events[1];
static struct out_control_line_msg  cache_enable_state  = {0};


K_MSGQ_DEFINE(out_ctl_rx_queue, sizeof(server_request), 8, 4);


ZBUS_CHAN_DEFINE(out_control_chan,
                 struct out_control_line_msg,
                 NULL,
                 &out_ctl_rx_queue,
                 ZBUS_OBSERVERS_EMPTY,
                 {0}
);

static const struct gpio_dt_spec out_switches[OUT_CONTROL_LINE_CNT] = 
{
    GPIO_DT_SPEC_GET(DT_NODELABEL(od1), gpios),
    GPIO_DT_SPEC_GET(DT_NODELABEL(od2), gpios),
    GPIO_DT_SPEC_GET(DT_NODELABEL(od3), gpios),
    GPIO_DT_SPEC_GET(DT_NODELABEL(od4), gpios),
    GPIO_DT_SPEC_GET(DT_NODELABEL(od5), gpios),
    GPIO_DT_SPEC_GET(DT_NODELABEL(od6), gpios),
    GPIO_DT_SPEC_GET(DT_NODELABEL(od7), gpios),
    GPIO_DT_SPEC_GET(DT_NODELABEL(od8), gpios),
    GPIO_DT_SPEC_GET(DT_NODELABEL(od9), gpios),
    GPIO_DT_SPEC_GET(DT_NODELABEL(od10), gpios),
    GPIO_DT_SPEC_GET(DT_NODELABEL(od11), gpios),    
};


/***************************************************************************************************
 *                                        PRIVATE FUNCTIONS
 **************************************************************************************************/

 static void _enable_port_init(void)
{                
    for (int i = 0; i < OUT_CONTROL_LINE_CNT; i++) 
    {
        if (gpio_is_ready_dt(&out_switches[i])) 
        {
             gpio_pin_configure_dt(&out_switches[i], GPIO_OUTPUT_INACTIVE);                        
        }
    }
    zbus_chan_pub(&out_control_chan, &cache_enable_state, K_MSEC(10));
    
}

static void  _pwr_ctl_update_handler(struct k_work *_work)
{
    ARG_UNUSED(_work);
    int err = 0;
    server_request req;
    if (k_msgq_get(&out_ctl_rx_queue, &req, K_NO_WAIT) == 0)
    {
        uint32_t _ch_id = (req.id - OD1);
        if (true
            && _ch_id >= 0 
            && _ch_id < OUT_CONTROL_LINE_CNT            
        )
        {
            if  (cache_enable_state.state[_ch_id] != req.value.sd_bool)
            {
                cache_enable_state.state[_ch_id] = req.value.sd_bool;
                gpio_pin_set_dt(&out_switches[_ch_id], cache_enable_state.state[_ch_id] ? 1 : 0); 
            }
            zbus_chan_pub(&out_control_chan, &cache_enable_state, K_MSEC(2));            
        }        
        else 
        {
            err = -EINVAL;
        }
    }    
    if (req.sync != NULL)
    {
        if (req.response != NULL)
        {
            *req.response = 0;
        }
        k_sem_give(req.sync);
    }
    out_ctl_poll_events[0].state = K_POLL_STATE_NOT_READY;
    app_worker_poll_submit(&out_ctl_task, 
                            out_ctl_poll_events,
                            ARRAY_SIZE(out_ctl_poll_events), 
                            OUT_CTL_K_WORK);
}

static int _power_control_init(void)
{

    k_poll_event_init(&out_ctl_poll_events[0],
                      K_POLL_TYPE_MSGQ_DATA_AVAILABLE, // Тип: появление данных в очереди
                      K_POLL_MODE_NOTIFY_ONLY,         // Режим: только уведомить
                      &out_ctl_rx_queue); 

    k_work_poll_init(&out_ctl_task, _pwr_ctl_update_handler); 
    app_worker_poll_submit(&out_ctl_task, 
                            out_ctl_poll_events,
                            ARRAY_SIZE(out_ctl_poll_events),
                            OUT_CTL_K_WORK);

    _enable_port_init();
    return 0;
}

SYS_INIT(_power_control_init, OUT_CTL_INIT_LEVEL,  OUT_CTL_INIT_PRIOR);


SERVER_OUT_CHANNEL_DEFINE(OD1, &out_control_chan, 0, BOOL_DATA);
SERVER_OUT_CHANNEL_DEFINE(OD2, &out_control_chan, 1, BOOL_DATA);
SERVER_OUT_CHANNEL_DEFINE(OD3, &out_control_chan, 2, BOOL_DATA);
SERVER_OUT_CHANNEL_DEFINE(OD4, &out_control_chan, 3, BOOL_DATA);
SERVER_OUT_CHANNEL_DEFINE(OD5, &out_control_chan, 4, BOOL_DATA);
SERVER_OUT_CHANNEL_DEFINE(OD6, &out_control_chan, 5, BOOL_DATA);
SERVER_OUT_CHANNEL_DEFINE(OD7, &out_control_chan, 6, BOOL_DATA);
SERVER_OUT_CHANNEL_DEFINE(OD8, &out_control_chan, 7, BOOL_DATA);
SERVER_OUT_CHANNEL_DEFINE(OD9, &out_control_chan, 8, BOOL_DATA);
SERVER_OUT_CHANNEL_DEFINE(OD10, &out_control_chan, 9, BOOL_DATA);
SERVER_OUT_CHANNEL_DEFINE(OD11, &out_control_chan, 10, BOOL_DATA);



/***************************************************************************************************
 *                                           END OF FILE
 **************************************************************************************************/