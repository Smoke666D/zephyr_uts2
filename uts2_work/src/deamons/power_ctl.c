/**
 *  @file       work_power.c
 *
 *  @date       2026.10.06
 *  @author     Dymov Igor
 *
 *  @brief      Менеджер управления силовыми выходами и линиями питания
 *  @details    Обеспечивает обработку событий ZBUS для управления цепочкой 
 *              сдвиговых регистров по SPI и дискретными ключами питания на GPIO.
 */

/***************************************************************************************************
 *                                          INCLUDED FILES
 **************************************************************************************************/
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/shell/shell.h>

#include "system_define.h"
#include "system_bus_model.h"
#include "app_worker.h"

/***************************************************************************************************
 *                                           DEFINITIONS
 **************************************************************************************************/
LOG_MODULE_REGISTER(power_control_module, LOG_LEVEL_INF);

typedef struct power_control_line_msg 
{
    bool state[POWER_CONTROL_LINE_CNT]; // Наш тестовый буфер (8 шагов * 2 канала)
} power_control_line_msg ;

/***********************************************************************************
 *                                   PRIVATE FUNCTION PROTOTYPES
 **********************************************************************************/

static void _enable_port_init(void);
static void _power_control_update_handler(struct k_work *_work);

/***************************************************************************************************
 *                                           PRIVATE DATA
 **************************************************************************************************/

static struct k_work_poll  pwr_ctl_task                         __attribute__((section("DTCM")));
static struct k_poll_event pwr_ctl_poll_events[1]               __attribute__((section("DTCM")));
static struct power_control_line_msg  cache_enable_state        __attribute__((section("DTCM"))) = {0};

K_MSGQ_DEFINE(pwr_ctl_rx_queue, sizeof(server_request), 8, 4);

ZBUS_CHAN_DEFINE(discrete_control_chan,
                 struct power_control_line_msg,
                 NULL,
                 &pwr_ctl_rx_queue,
                 ZBUS_OBSERVERS_EMPTY,
                 {0}
);

static const struct gpio_dt_spec power_switches[POWER_CONTROL_LINE_CNT] = 
{
    GPIO_DT_SPEC_GET(DT_NODELABEL(en_dut2), gpios),
    GPIO_DT_SPEC_GET(DT_NODELABEL(en_dut3), gpios),
    GPIO_DT_SPEC_GET(DT_NODELABEL(en_p12v), gpios),
    GPIO_DT_SPEC_GET(DT_NODELABEL(en_p24v), gpios),
    GPIO_DT_SPEC_GET(DT_NODELABEL(en_va), gpios),
    GPIO_DT_SPEC_GET(DT_NODELABEL(en_usb_out), gpios),
    GPIO_DT_SPEC_GET(DT_NODELABEL(en_usb_boot), gpios),
    GPIO_DT_SPEC_GET(DT_NODELABEL(en_usb_top), gpios)
};

/***************************************************************************************************
 *                                        PRIVATE FUNCTIONS
 **************************************************************************************************/


/**
 *  @brief      Инициализация выходных портов управления питанием
 *  @details    Конфигурирует выводы GPIO силовых ключей на выход и переводит
 *              их в активное логическое состояние по умолчанию (включено).
 */
static void _enable_port_init(void)
{                
    for (int i = 0; i < POWER_CONTROL_LINE_CNT; i++) 
    {
        if (gpio_is_ready_dt(&power_switches[i])) 
        {
             gpio_pin_configure_dt(&power_switches[i], GPIO_OUTPUT_INACTIVE);                        
        }
    }
    zbus_chan_pub(&discrete_control_chan, &cache_enable_state, K_MSEC(10));
    
}

static void  _pwr_ctl_update_handler(struct k_work *_work)
{
    ARG_UNUSED(_work);
    int err = 0;
    server_request req;
    if (k_msgq_get(&pwr_ctl_rx_queue, &req, K_NO_WAIT) == 0)
    {
        uint32_t _ch_id = (req.id - EN_DUT2_PSU);
        if (true
            && _ch_id >= 0 
            && _ch_id < POWER_CONTROL_LINE_CNT            
        )
        {
            if  (cache_enable_state.state[_ch_id] != req.value.sd_bool)
            {
                cache_enable_state.state[_ch_id] = req.value.sd_bool;
                gpio_pin_set_dt(&power_switches[_ch_id], cache_enable_state.state[_ch_id] ? 1 : 0); 
            }
            zbus_chan_pub(&discrete_control_chan, &cache_enable_state, K_MSEC(2));            
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
    pwr_ctl_poll_events[0].state = K_POLL_STATE_NOT_READY;
    app_worker_poll_submit(&pwr_ctl_task, pwr_ctl_poll_events, COMMON_WORKER);
}

/**
 *  @brief      Инициализация менеджера управления выходами питания
 *  @details    Инициализирует воркеры обновления, вызывает процедуру
 *              первоначальной конфигурации GPIO и регистрирует модуль в системе.
 *
 *  @return     int - Ноль при успешной инициализации
 */
static int _power_control_init(void)
{

   k_poll_event_init(&pwr_ctl_poll_events[0],
                      K_POLL_TYPE_MSGQ_DATA_AVAILABLE, // Тип: появление данных в очереди
                      K_POLL_MODE_NOTIFY_ONLY,         // Режим: только уведомить
                      &pwr_ctl_rx_queue); 

    k_work_poll_init(&pwr_ctl_task, _pwr_ctl_update_handler); 
    app_worker_poll_submit(&pwr_ctl_task, pwr_ctl_poll_events, COMMON_WORKER);

    _enable_port_init();
    return 0;
}

SYS_INIT(_power_control_init, PWR_CTL_INIT_LEVEL,  PWR_CTL_INIT_PRIOR);


SERVER_OUT_CHANNEL_DEFINE(EN_DUT2_PSU, &discrete_control_chan, 0, BOOL_DATA);
SERVER_OUT_CHANNEL_DEFINE(EN_DUT3_PSU, &discrete_control_chan, 1, BOOL_DATA);
SERVER_OUT_CHANNEL_DEFINE(EN_P12V,     &discrete_control_chan, 2, BOOL_DATA);
SERVER_OUT_CHANNEL_DEFINE(EN_P24V,     &discrete_control_chan, 3, BOOL_DATA);
SERVER_OUT_CHANNEL_DEFINE(EN_VA,       &discrete_control_chan, 4, BOOL_DATA);
SERVER_OUT_CHANNEL_DEFINE(EN_USB_OUT,  &discrete_control_chan, 5, BOOL_DATA);
SERVER_OUT_CHANNEL_DEFINE(EN_USB_BOOT, &discrete_control_chan, 6, BOOL_DATA);
SERVER_OUT_CHANNEL_DEFINE(EN_USB_TOP,  &discrete_control_chan, 7, BOOL_DATA);

/***************************************************************************************************
 *                                           END OF FILE
 **************************************************************************************************/
