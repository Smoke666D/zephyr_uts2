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

#include <hc595_chain.h>

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

static void _power_control_listener_cb(const struct zbus_channel *_chan);
static void _out_update_handler(struct k_work *_work);
static void _enable_port_init(void);
static void _power_control_update_handler(struct k_work *_work);

/***************************************************************************************************
 *                                           PRIVATE DATA
 **************************************************************************************************/
static struct k_work power_control_work; 


ZBUS_LISTENER_DEFINE(power_control_listener, _power_control_listener_cb);

ZBUS_CHAN_DEFINE(discrete_control_chan,
                 struct power_control_line_msg,
                 NULL,
                 NULL,
                 ZBUS_OBSERVERS(power_control_listener),
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

static struct power_control_line_msg  cache_enable_state = {0};

/***************************************************************************************************
 *                                        PRIVATE FUNCTIONS
 **************************************************************************************************/

/**
 *  @brief      Коллбэк слушателя ZBUS для канала hc595_chan
 *  @details    Вызывается при публикации сообщений в канале hc595_chan,
 *              планируя выполнение задачи обновления регистров.
 *
 *  @param      _chan - Указатель на канал ZBUS
 */
static void _power_control_listener_cb(const struct zbus_channel *_chan)
{
    ARG_UNUSED(_chan);
    app_worker_submit(&power_control_work, REAL_TIME_WORKER);
}


/**
 *  @brief      Инициализация выходных портов управления питанием
 *  @details    Конфигурирует выводы GPIO силовых ключей на выход и переводит
 *              их в активное логическое состояние по умолчанию (включено).
 */
static void _enable_port_init(void)
{
    if (zbus_chan_claim(&discrete_control_chan, K_FOREVER) == 0)
    {        
        // Получение прямого указателя на сообщение в памяти Zbus
        struct power_control_line_msg  *msg = 
            (struct power_control_line_msg *)zbus_chan_msg(&discrete_control_chan);
            
        for (int i = 0; i < POWER_CONTROL_LINE_CNT; i++) 
        {
            if (gpio_is_ready_dt(&power_switches[i])) 
            {
                gpio_pin_configure_dt(&power_switches[i], GPIO_OUTPUT_INACTIVE);                        
                cache_enable_state.state[i] = false;
                msg->state[i] = false;
            }
        }
        zbus_chan_finish(&discrete_control_chan);
    }
}

/**
 *  @brief      Обработчик воркера обновления дискретных линий
 *  @details    Считывает состояние дискретных каналов из ZBUS, сравнивает
 *              с кэшем и обновляет физические уровни на пинах GPIO.
 *
 *  @param      _work - Указатель на структуру задачи
 */
static void _power_control_update_handler(struct k_work *_work)
{    
    ARG_UNUSED(_work);
    struct power_control_line_msg msg;
    
    if (zbus_chan_read(&discrete_control_chan, &msg, K_MSEC(50)) == 0)
    {
        for (int i = 0; i < POWER_CONTROL_LINE_CNT; i++)
        {
            if (cache_enable_state.state[i] != msg.state[i])
            {
                cache_enable_state.state[i] = msg.state[i];
                gpio_pin_set_dt(&power_switches[i], cache_enable_state.state[i] ? 1 : 0); 
            }
        }
    }
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

    k_work_init(&power_control_work, _power_control_update_handler);
    _enable_port_init();
    return 0;
}

SYS_INIT(_power_control_init, APPLICATION, 40);

PARAM_ROUTE_DEFINE(EN_DUT2_PSU, &discrete_control_chan, 0, BOOL_DATA);
PARAM_ROUTE_DEFINE(EN_DUT3_PSU, &discrete_control_chan, 1, BOOL_DATA);
PARAM_ROUTE_DEFINE(EN_P12V,     &discrete_control_chan, 2, BOOL_DATA);
PARAM_ROUTE_DEFINE(EN_P24V,     &discrete_control_chan, 3, BOOL_DATA);
PARAM_ROUTE_DEFINE(EN_VA,       &discrete_control_chan, 4, BOOL_DATA);
PARAM_ROUTE_DEFINE(EN_USB_OUT,  &discrete_control_chan, 5, BOOL_DATA);
PARAM_ROUTE_DEFINE(EN_USB_BOOT, &discrete_control_chan, 6, BOOL_DATA);
PARAM_ROUTE_DEFINE(EN_USB_TOP,  &discrete_control_chan, 7, BOOL_DATA);

/***************************************************************************************************
 *                                           END OF FILE
 **************************************************************************************************/
