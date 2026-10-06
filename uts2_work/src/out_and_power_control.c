#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>
#include "system_bus_model.h"
#include <hc595_chain.h>
#include "out_and_power_control.h"
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include "app_worker.h"
#include <zephyr/shell/shell.h>
#include <zephyr/zbus/zbus.h>



LOG_MODULE_REGISTER(out_and_power, LOG_LEVEL_INF);

static struct k_work out_task; 
static struct k_work discrete_control_task; 

static void out_zbus_listener_callback(const struct zbus_channel *chan)
{
    /* Объявление канала будет ниже, но сам указатель chan уже известен */
    app_worker_submit(&out_task,REAL_TIME_WORKER);
}


ZBUS_LISTENER_DEFINE(hc595_sub, out_zbus_listener_callback);

/* 2. Объявляем канал Zbus для управления выходами */
ZBUS_CHAN_DEFINE(hc595_chan,
                 struct hc595_channels_msg,
                 NULL,
                 NULL,
                 ZBUS_OBSERVERS(hc595_sub),
                 {0}
);


static void discret_control_line_listener_callback(const struct zbus_channel *chan)
{
    /* Объявление канала будет ниже, но сам указатель chan уже известен */
    app_worker_submit(&discrete_control_task,REAL_TIME_WORKER);
}


ZBUS_LISTENER_DEFINE(discret_control_line_listener, discret_control_line_listener_callback);

ZBUS_CHAN_DEFINE(discrete_control_chan,
                 struct discrete_control_line_msg,
                 NULL,
                 NULL,
                 ZBUS_OBSERVERS(discret_control_line_listener),
                 {0}
);



/**
 * @brief Функция расчета маски для сдвигового регистра.
 */
static void hc595_calculate_mask(const struct hc595_channels_msg *msg, uint8_t *tx_data, size_t len)
{
    if (len < 5) {
        return;
    }

    memset(tx_data, 0, len);

    /* Заполняем управляющие сигналы LIN (старшая тетрада первого байта) */
    tx_data[0] = ((uint8_t)msg->channels_mv[18] << 4) |
                 ((uint8_t)msg->channels_mv[19] << 5) |
                 ((uint8_t)msg->channels_mv[20] << 6) |
                 ((uint8_t)msg->channels_mv[21] << 7);

    static const uint8_t state_to_bits[] = {0, 0x02, 0x01};

    /* Заполняем каналы слаботочных драйверов */
    for (int i = 0; i < LOW_CUR_DRIVER_COUNT; i++) {
        uint8_t chip_index = 4 - (i / 4);
        uint8_t bit_indx = (i % 4) * 2;
        uint32_t state = msg->channels_mv[i];

        if (state < 3) {
            tx_data[chip_index] |= (state_to_bits[state] << bit_indx);
        }
    }
}





static struct spi_dt_spec spi_spec = 
    SPI_DT_SPEC_GET(DT_NODELABEL(hc595_chain),
                    SPI_OP_MODE_MASTER | 
                    SPI_WORD_SET(8) | 
                    SPI_TRANSFER_MSB);

          
const struct device *hc595_dev = DEVICE_DT_GET(DT_NODELABEL(hc595_chain));      

static void out_update_handler(struct k_work *work)
{    
    struct hc595_channels_msg msg;
    if (zbus_chan_read(&hc595_chan, &msg, K_MSEC(50)) == 0)
    {
        uint8_t tx_data[5] = {0};

        hc595_calculate_mask(&msg, tx_data, sizeof(tx_data));
                   
         if (hc595_chain_write(hc595_dev, tx_data, sizeof(tx_data)))         
         {
                LOG_ERR("Failed to write registers");
         } 
        // else 
        // {
        //        memcpy(last_tx_data, tx_data, sizeof(tx_data));                                
        // }
    }
}

static const struct gpio_dt_spec power_switches[CONTROL_LINE_CNT] = {
    GPIO_DT_SPEC_GET(DT_NODELABEL(en_dut2), gpios),
    GPIO_DT_SPEC_GET(DT_NODELABEL(en_dut3), gpios),
    GPIO_DT_SPEC_GET(DT_NODELABEL(en_p12v), gpios),
    GPIO_DT_SPEC_GET(DT_NODELABEL(en_p24v), gpios),
    GPIO_DT_SPEC_GET(DT_NODELABEL(en_va),      gpios),
    GPIO_DT_SPEC_GET(DT_NODELABEL(en_usb_out), gpios),
    GPIO_DT_SPEC_GET(DT_NODELABEL(en_usb_boot), gpios),
    GPIO_DT_SPEC_GET(DT_NODELABEL(en_usb_top), gpios)
};

static discrete_control_line_msg  _cash_enable_state = {0};

static void _enable_port_init()
{
   
   if ( zbus_chan_claim(&discrete_control_chan, K_FOREVER) == 0)
   {        
        /* Получаем прямой указатель на сообщение в памяти Zbus [2] */
        struct discrete_control_line_msg *msg = 
            (struct discrete_control_line_msg *)zbus_chan_msg(&discrete_control_chan);
        for (int i = 0; i < CONTROL_LINE_CNT; i++) 
        {
            if (gpio_is_ready_dt(&power_switches[i])) 
            {
                gpio_pin_configure_dt(&power_switches[i], GPIO_OUTPUT_ACTIVE);                        
                _cash_enable_state.state[i]  = true;
                msg->state[i] = true;
            }
        }
         zbus_chan_finish(&discrete_control_chan);
   }
}

static void discrete_contol_update_handler(struct k_work *work)
{    
    struct discrete_control_line_msg  msg;
    if (zbus_chan_read(&discrete_control_chan, &msg, K_MSEC(50)) == 0)
    {
        for (int i = 0; i < CONTROL_LINE_CNT; i++ )
        {
            if (_cash_enable_state.state[i] != msg.state[i])
            {
                _cash_enable_state.state[i] = msg.state[i];
                gpio_pin_set_dt(&power_switches[i], _cash_enable_state.state[i] ? 1 : 0); 
            }
        }
    }
}


static int out_manager_system_init(void)
{
   
    k_work_init(&out_task, out_update_handler);
    k_work_init(&discrete_control_task, discrete_contol_update_handler);
    _enable_port_init();
    return 0;
}


SYS_INIT(out_manager_system_init, APPLICATION, 40);


PARAM_ROUTE_DEFINE(DOUT1,&hc595_chan,0,ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT2,&hc595_chan,1,ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT3,&hc595_chan,2,ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT4,&hc595_chan,3,ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT5,&hc595_chan,4,ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT6,&hc595_chan,5,ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT7,&hc595_chan,6,ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT8,&hc595_chan,7,ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT9,&hc595_chan,8,ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT10,&hc595_chan,9,ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT11,&hc595_chan,10,ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT12,&hc595_chan,11,ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT13,&hc595_chan,12,ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT14,&hc595_chan,13,ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT15,&hc595_chan,14,ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT16,&hc595_chan,15,ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT17,&hc595_chan,16,ARRAY_DATA);
PARAM_ROUTE_DEFINE(DOUT18,&hc595_chan,17,ARRAY_DATA);
PARAM_ROUTE_DEFINE(LIN_PD1,&hc595_chan,18,ARRAY_DATA);
PARAM_ROUTE_DEFINE(LIN_PD2,&hc595_chan,19,ARRAY_DATA);
PARAM_ROUTE_DEFINE(LIN_PD3,&hc595_chan,20,ARRAY_DATA);
PARAM_ROUTE_DEFINE(LIN_PD4,&hc595_chan,22,ARRAY_DATA);

PARAM_ROUTE_DEFINE(EN_DUT2_PSU,&discrete_control_chan,0,ARRAY_DATA);
PARAM_ROUTE_DEFINE(EN_DUT3_PSU,&discrete_control_chan,1,ARRAY_DATA);
PARAM_ROUTE_DEFINE(EN_P12V,    &discrete_control_chan,2,ARRAY_DATA);
PARAM_ROUTE_DEFINE(EN_P24V,    &discrete_control_chan,3,ARRAY_DATA);
PARAM_ROUTE_DEFINE(EN_VA,      &discrete_control_chan,4,ARRAY_DATA);
PARAM_ROUTE_DEFINE(EN_USB_OUT, &discrete_control_chan,5,ARRAY_DATA);
PARAM_ROUTE_DEFINE(EN_USB_BOOT,&discrete_control_chan,6,ARRAY_DATA);
PARAM_ROUTE_DEFINE(EN_USB_TOP, &discrete_control_chan,7,ARRAY_DATA);


