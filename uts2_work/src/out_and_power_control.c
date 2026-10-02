#include <stdint.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>
#include "system_bus_model.h"
#include <hc595_chain.h>
#include "out_and_power_control.h"
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include "app_worker.h"


LOG_MODULE_REGISTER(out_and_power, LOG_LEVEL_INF);

static struct k_work out_task; 

static void out_zbus_listener_callback(const struct zbus_channel *chan)
{
    /* Объявление канала будет ниже, но сам указатель chan уже известен */
    app_worker_submit(&out_task);
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



/* Функция потока диспетчера (коллбэк) */
static void hc595_dispatcher_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    const struct device *hc595_dev = DEVICE_DT_GET(DT_NODELABEL(hc595_chain));

    if (!device_is_ready(hc595_dev)) {
        LOG_ERR("74HC595 device not ready in dispatcher thread");
        return;
    }

    /* На старте выходы аппаратно выключены */
    hc595_chain_output_enable(hc595_dev, false);

    struct hc595_channels_msg current_msg = {0};  /* Желаемое состояние из Zbus */
    uint8_t last_tx_data[5] = {0};
    bool first_run = true;
    const struct zbus_channel *chan;

    while (1) 
    {
        /* Поток засыпает и ждет публикации в канале [2, 3] */
        int err = zbus_sub_wait(&hc595_sub, &chan, K_FOREVER);
        if (err != 0) {
            continue;
        }
                
            err = zbus_chan_read(&hc595_chan, &current_msg, K_NO_WAIT);
            if (err != 0) 
            {
                continue;
            }

            uint8_t tx_data[5] = {0x00,0x00,0x00,0x00,0x02};

            //hc595_calculate_mask(&current_msg, tx_data, sizeof(tx_data));
                       
            err = hc595_chain_write(hc595_dev, tx_data, sizeof(tx_data));
            if (err) 
            {
                LOG_ERR("Failed to write registers: %d", err);
            } 
            else 
            {
                memcpy(last_tx_data, tx_data, sizeof(tx_data));                                
            }
    }        
}

static struct spi_dt_spec spi_spec = 
    SPI_DT_SPEC_GET(DT_NODELABEL(hc595_chain),
                    SPI_OP_MODE_MASTER | 
                    SPI_WORD_SET(8) | 
                    SPI_TRANSFER_MSB);




/* Функция потока диспетчера (коллбэк) */
static void hc595_dispatcher_test(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

  int err;

 uint8_t test_pattern[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
    
    struct spi_buf tx_buf = {
        .buf = test_pattern,
        .len = sizeof(test_pattern),
    };
    struct spi_buf_set tx_bufs = {
        .buffers = &tx_buf,
        .count = 1,
    };

    while (1) {
        /* Отправляем пачку 5 байт синхронно [1.2.2] */
        err = spi_write_dt(&spi_spec, &tx_bufs);
        if (err) {
            LOG_ERR("SPI write failed: %d", err);
        }

        /* Пауза 10 миллисекунд между пачками */
        k_msleep(100);
    }
}


K_THREAD_DEFINE(hc595_dispatcher_thread_data, DISPATCHER_STACK_SIZE, hc595_dispatcher_test, 
            NULL, NULL, NULL, 
                7, 0, 0);



          
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


static int out_manager_system_init(void)
{
   
    k_work_init(&out_task, out_update_handler);
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
