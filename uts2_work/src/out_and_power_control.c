/**
 *  @file       out_and_power_control.c
 *  @headerfile out_and_power_control.h
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
#include "out_and_power_control.h"
#include "app_worker.h"

/***************************************************************************************************
 *                                           DEFINITIONS
 **************************************************************************************************/
LOG_MODULE_REGISTER(out_and_power, LOG_LEVEL_INF);

/***********************************************************************************
 *                                   PRIVATE FUNCTION PROTOTYPES
 **********************************************************************************/
static void _out_zbus_listener_callback(const struct zbus_channel *_chan);
static void _discrete_control_line_listener_cb(const struct zbus_channel *_chan);
static void _hc595_calculate_mask(const struct hc595_channels_msg *_msg, 
                                  uint8_t *_tx_data, size_t _len);
static void _out_update_handler(struct k_work *_work);
static void _enable_port_init(void);
static void _discrete_control_update_handler(struct k_work *_work);
static int  _out_manager_system_init(void);

/***************************************************************************************************
 *                                           PRIVATE DATA
 **************************************************************************************************/
static struct k_work out_task; 
static struct k_work discrete_control_task; 

ZBUS_LISTENER_DEFINE(hc595_sub, _out_zbus_listener_callback);

// Объявляем канал Zbus для управления выходами
ZBUS_CHAN_DEFINE(hc595_chan,
                 struct hc595_channels_msg,
                 NULL,
                 NULL,
                 ZBUS_OBSERVERS(hc595_sub),
                 {0}
);

ZBUS_LISTENER_DEFINE(discret_control_line_listener, _discrete_control_line_listener_cb);

ZBUS_CHAN_DEFINE(discrete_control_chan,
                 struct discrete_control_line_msg,
                 NULL,
                 NULL,
                 ZBUS_OBSERVERS(discret_control_line_listener),
                 {0}
);

static struct spi_dt_spec spi_spec = 
    SPI_DT_SPEC_GET(DT_NODELABEL(hc595_chain),
                    SPI_OP_MODE_MASTER | 
                    SPI_WORD_SET(8) | 
                    SPI_TRANSFER_MSB);

static const struct device *const hc595_dev = DEVICE_DT_GET(DT_NODELABEL(hc595_chain));      

static const struct gpio_dt_spec power_switches[CONTROL_LINE_CNT] = 
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

static struct discrete_control_line_msg cache_enable_state = {0};

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
static void _out_zbus_listener_callback(const struct zbus_channel *_chan)
{
    ARG_UNUSED(_chan);
    app_worker_submit(&out_task, REAL_TIME_WORKER);
}

/**
 *  @brief      Коллбэк слушателя ZBUS для дискретных линий
 *  @details    Вызывается при публикации сообщений в канале discrete_control_chan,
 *              планируя выполнение задачи обновления выходов GPIO.
 *
 *  @param      _chan - Указатель на канал ZBUS
 */
static void _discrete_control_line_listener_cb(const struct zbus_channel *_chan)
{
    ARG_UNUSED(_chan);
    app_worker_submit(&discrete_control_task, REAL_TIME_WORKER);
}

/**
 *  @brief      Расчет маски для сдвигового регистра HC595
 *  @details    Заполняет переданный буфер управляющими битами LIN и слаботочных драйверов
 *              на основе данных из пакетного ZBUS-сообщения.
 *
 *  @param      _msg     - Указатель на сообщение с каналами
 *  @param      _tx_data - Указатель на выходной буфер для маски
 *  @param      _len     - Длина выходного буфера в байтах
 */
static void _hc595_calculate_mask(const struct hc595_channels_msg *_msg, 
                                  uint8_t *_tx_data, size_t _len)
{
    if (_len < 5) 
    {
        return;
    }

    memset(_tx_data, 0, _len);

    // Заполнение управляющих сигналов LIN (старшая тетрада первого байта)
    _tx_data[0] = ((uint8_t)_msg->channels_mv[18] << 4) |
                  ((uint8_t)_msg->channels_mv[19] << 5) |
                  ((uint8_t)_msg->channels_mv[20] << 6) |
                  ((uint8_t)_msg->channels_mv[21] << 7);

    static const uint8_t state_to_bits[] = {0, 0x02, 0x01};

    // Заполнение каналов слаботочных драйверов
    for (int i = 0; i < LOW_CUR_DRIVER_COUNT; i++) 
    {
        uint8_t chip_index = 4 - (i / 4);
        uint8_t bit_indx = (i % 4) * 2;
        uint32_t state = _msg->channels_mv[i];

        if (state < 3) 
        {
            _tx_data[chip_index] |= (state_to_bits[state] << bit_indx);
        }
    }
}

/**
 *  @brief      Обработчик воркера обновления сдвиговых регистров
 *  @details    Считывает сообщение из канала ZBUS, рассчитывает маску
 *              и записывает её в цепочку сдвиговых регистров по SPI.
 *
 *  @param      _work - Указатель на структуру задачи
 */
static void _out_update_handler(struct k_work *_work)
{    
    ARG_UNUSED(_work);
    struct hc595_channels_msg msg;
    
    if (zbus_chan_read(&hc595_chan, &msg, K_MSEC(50)) == 0)
    {
        uint8_t tx_data[5] = {0};

        _hc595_calculate_mask(&msg, tx_data, sizeof(tx_data));
                   
        if (hc595_chain_write(hc595_dev, tx_data, sizeof(tx_data)))         
        {
            LOG_ERR("Failed to write registers");
        } 
    }
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
        struct discrete_control_line_msg *msg = 
            (struct discrete_control_line_msg *)zbus_chan_msg(&discrete_control_chan);
            
        for (int i = 0; i < CONTROL_LINE_CNT; i++) 
        {
            if (gpio_is_ready_dt(&power_switches[i])) 
            {
                gpio_pin_configure_dt(&power_switches[i], GPIO_OUTPUT_ACTIVE);                        
                cache_enable_state.state[i] = true;
                msg->state[i] = true;
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
static void _discrete_control_update_handler(struct k_work *_work)
{    
    ARG_UNUSED(_work);
    struct discrete_control_line_msg msg;
    
    if (zbus_chan_read(&discrete_control_chan, &msg, K_MSEC(50)) == 0)
    {
        for (int i = 0; i < CONTROL_LINE_CNT; i++)
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
static int _out_manager_system_init(void)
{
    k_work_init(&out_task, _out_update_handler);
    k_work_init(&discrete_control_task, _discrete_control_update_handler);
    _enable_port_init();
    
    return 0;
}

SYS_INIT(_out_manager_system_init, APPLICATION, 40);

PARAM_ROUTE_DEFINE(DOUT1, &hc595_chan, 0, ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT2, &hc595_chan, 1, ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT3, &hc595_chan, 2, ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT4, &hc595_chan, 3, ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT5, &hc595_chan, 4, ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT6, &hc595_chan, 5, ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT7, &hc595_chan, 6, ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT8, &hc595_chan, 7, ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT9, &hc595_chan, 8, ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT10, &hc595_chan, 9, ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT11, &hc595_chan, 10, ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT12, &hc595_chan, 11, ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT13, &hc595_chan, 12, ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT14, &hc595_chan, 13, ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT15, &hc595_chan, 14, ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT16, &hc595_chan, 15, ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT17, &hc595_chan, 16, ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT18, &hc595_chan, 17, ARRAY_DATA);
PARAM_ROUTE_DEFINE(LIN_PD1, &hc595_chan, 18, ARRAY_DATA);
PARAM_ROUTE_DEFINE(LIN_PD2, &hc595_chan, 19, ARRAY_DATA);
PARAM_ROUTE_DEFINE(LIN_PD3, &hc595_chan, 20, ARRAY_DATA);
PARAM_ROUTE_DEFINE(LIN_PD4, &hc595_chan, 21, ARRAY_DATA);

PARAM_ROUTE_DEFINE(EN_DUT2_PSU, &discrete_control_chan, 0, ARRAY_DATA);
PARAM_ROUTE_DEFINE(EN_DUT3_PSU, &discrete_control_chan, 1, ARRAY_DATA);
PARAM_ROUTE_DEFINE(EN_P12V,     &discrete_control_chan, 2, ARRAY_DATA);
PARAM_ROUTE_DEFINE(EN_P24V,     &discrete_control_chan, 3, ARRAY_DATA);
PARAM_ROUTE_DEFINE(EN_VA,       &discrete_control_chan, 4, ARRAY_DATA);
PARAM_ROUTE_DEFINE(EN_USB_OUT,  &discrete_control_chan, 5, ARRAY_DATA);
PARAM_ROUTE_DEFINE(EN_USB_BOOT, &discrete_control_chan, 6, ARRAY_DATA);
PARAM_ROUTE_DEFINE(EN_USB_TOP,  &discrete_control_chan, 7, ARRAY_DATA);

/***************************************************************************************************
 *                                           END OF FILE
 **************************************************************************************************/

