#include "app_worker.h"
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <stdint.h>
#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include "system_bus_model.h"

LOG_MODULE_REGISTER(mcp_out, LOG_LEVEL_INF);

#define I2C1_NODE DT_NODELABEL(i2c1)
#define MCP23017_ADDR 0x20

#define MUX_EN_CHANNEL 9

typedef struct pcf_bool_array_9_msg 
{
    bool states[9]; 
} pcf_bool_array_9_msg;

struct pcf_mux_msgs {
    uint32_t group[2];       
};

static void pcf_zbus_listener(const struct zbus_channel *chan);

static struct pcf_bool_array_9_msg current_bools = { {false} };
static struct pcf_mux_msgs         current_mux   = {0};

static struct k_work_poll  mcp_task        __attribute__((section("DTCM")));
static struct k_poll_event mcp_poll_events[1] __attribute__((section("DTCM")));

static struct k_work pcf_work;
static struct i2c_dt_spec mcp_spec;

ZBUS_LISTENER_DEFINE(pcf_listener, pcf_zbus_listener);



K_MSGQ_DEFINE(mcp_rx_queue, sizeof(server_request), 3, 4);



ZBUS_CHAN_DEFINE(pcf_bool_channel,
                 struct pcf_bool_array_9_msg,
                 NULL, 
                 &mcp_rx_queue,
                 ZBUS_OBSERVERS_EMPTY,
                 ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(pcf_mux_channel,
                 struct pcf_mux_msgs,
                 NULL, 
                 NULL,
                 ZBUS_OBSERVERS(pcf_listener),
                 ZBUS_MSG_INIT(0));

static const uint8_t mcp_pin_mapping[9] = {
    8,  
    12,  
    13,  
    14,  
    15, 
    3,  
    5, 
    6,
    7 
};

int _set_mcp_data()
{
    uint8_t porta = 0x00; 
    uint8_t portb = 0x00; 

    /* 1. Обрабатываем булевы пины */
    for (int i = 0; i < 9; i++) 
    {
        if (current_bools.states[i]) 
        {
            uint8_t pin = mcp_pin_mapping[i];
            if (pin < 8) 
            {
                porta |= (1 << pin);
            } 
            else 
            {
                portb |= (1 << (pin - 8));
            }
        }
    }        

    uint32_t val_g1 = current_mux.group[1];
    if (val_g1 > 0) 
    {
        uint8_t mapped_val = (uint8_t)(val_g1 - 1) & 0x07; // Берем (val - 1), маска на 3 бита
        porta |= mapped_val; // Записываем в биты 0, 1, 2 Порта А
    }
    
    uint32_t val_g0 = current_mux.group[0];    
    // Сначала очищаем биты 1, 2, 3 в portb перед записью новой комбинации
    portb &= ~((1 << 1) | (1 << 2) | (1 << 3));

    switch (val_g0) 
    {
        case 1:
            // бит 9 ("1"), бит 10 ("0"), бит 11 ("0") -> в portb это бит 1 = 1
            portb |= (1 << 1);
            break;
        case 2:
            // бит 9 ("0"), бит 10 ("1"), бит 11 ("0") -> в portb это бит 2 = 1
            portb |= (1 << 2);
            break;
        case 3:
            // бит 9 ("0"), бит 10 ("0"), бит 11 ("1") -> в portb это бит 1 = 1
            portb |= (1 << 3);
            break;
        case 4:
            // бит 9 ("1"), бит 10 ("0"), бит 11 ("1") -> в portb биты 1 и 3 = 1
            portb |= (1 << 3) | (1 << 1);
            break;
        case 5:
            // бит 9 ("1"), бит 10 ("1"), бит 11 ("0") -> в portb биты 1 и 2 = 1
            portb |= (1 << 3) | (1 << 2);
            break;
        default:
            break;
    }

    LOG_INF("Target values to write -> porta=0x%02X, portb=0x%02X (g0=%d, g1=%d)", 
             porta, portb, (int)val_g0, (int)val_g1);

    // Записываем данные в GPIOA (0x12) и GPIOB (0x13)
    uint8_t cmd_a[2] = { 0x12, porta }; 
    uint8_t cmd_b[2] = { 0x13, portb }; 

    int ret = i2c_write_dt(&mcp_spec, cmd_a, sizeof(cmd_a));
    ret|= i2c_write_dt(&mcp_spec, cmd_b, sizeof(cmd_b));
    
    return ret;
}

static void _mcp_update_handler(struct k_work *_work)
{
    ARG_UNUSED(_work);
    int err = 0;
    server_request req;

    if (k_msgq_get(&mcp_rx_queue, &req, K_NO_WAIT) == 0)
    {    
        int _channel = (req.id - JTAG_MUX_EN);
        if (true
            && _channel >= 0
            && _channel < MUX_EN_CHANNEL           
        )
        {
            if (current_bools.states[_channel] != req.value.sd_bool)
            {
                pcf_bool_array_9_msg _temp = current_bools;
                current_bools.states[_channel] = req.value.sd_bool;
                err = _set_mcp_data();
                if (err!=0)
                {
                    current_bools = _temp;
                }
            }
            if (err == 0)
            {
                zbus_chan_pub(&pcf_bool_channel, &current_bools, K_MSEC(2));   
            }
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
            *req.response = err;
        }
        k_sem_give(req.sync);
    }
    mcp_poll_events[0].state = K_POLL_STATE_NOT_READY;
    app_worker_poll_submit(&mcp_task, mcp_poll_events, COMMON_WORKER);  
}


static void pcf_work_handler(struct k_work *work)
{
    uint8_t porta = 0x00; 
    uint8_t portb = 0x00; 

    /* 1. Обрабатываем булевы пины */
    for (int i = 0; i < 9; i++) {
        if (current_bools.states[i]) {
            uint8_t pin = mcp_pin_mapping[i];
            if (pin < 8) {
                porta |= (1 << pin);
            } else {
                portb |= (1 << (pin - 8));
            }
        }
    }        

    /* 
     * 2. Обрабатываем group[1] (1-е слово mux канала).
     * Значение минус 1 пишется в первые 3 бита Порта А (биты 0, 1, 2).
     */
    uint32_t val_g1 = current_mux.group[1];
    if (val_g1 > 0) {
        uint8_t mapped_val = (uint8_t)(val_g1 - 1) & 0x07; // Берем (val - 1), маска на 3 бита
        porta |= mapped_val; // Записываем в биты 0, 1, 2 Порта А
    }

    /* 
     * 3. Обрабатываем group[0] (0-е слово mux канала).
     * Значения от 1 до 5 маппятся в биты 9, 10, 11 Порта B (в байт portb это биты 1, 2, 3, так как pin - 8).
     * Нам нужно управлять битами 9, 10, 11 -> в байтах portb (начиная с GPB0) это соответствует битам 1, 2, 3.
     */
    uint32_t val_g0 = current_mux.group[0];
    
    // Сначала очищаем биты 1, 2, 3 в portb перед записью новой комбинации
    portb &= ~((1 << 1) | (1 << 2) | (1 << 3));

    switch (val_g0) {
        case 1:
            // бит 9 ("1"), бит 10 ("0"), бит 11 ("0") -> в portb это бит 1 = 1
            portb |= (1 << 1);
            break;
        case 2:
            // бит 9 ("0"), бит 10 ("1"), бит 11 ("0") -> в portb это бит 2 = 1
            portb |= (1 << 2);
            break;
        case 3:
            // бит 9 ("0"), бит 10 ("0"), бит 11 ("1") -> в portb это бит 1 = 1
            portb |= (1 << 3);
            break;
        case 4:
            // бит 9 ("1"), бит 10 ("0"), бит 11 ("1") -> в portb биты 1 и 3 = 1
            portb |= (1 << 3) | (1 << 1);
            break;
        case 5:
            // бит 9 ("1"), бит 10 ("1"), бит 11 ("0") -> в portb биты 1 и 2 = 1
            portb |= (1 << 3) | (1 << 2);
            break;
        default:
            break;
    }

    LOG_INF("Target values to write -> porta=0x%02X, portb=0x%02X (g0=%d, g1=%d)", 
             porta, portb, (int)val_g0, (int)val_g1);

    // Записываем данные в GPIOA (0x12) и GPIOB (0x13)
    uint8_t cmd_a[2] = { 0x12, porta }; 
    uint8_t cmd_b[2] = { 0x13, portb }; 

    i2c_write_dt(&mcp_spec, cmd_a, sizeof(cmd_a));
    i2c_write_dt(&mcp_spec, cmd_b, sizeof(cmd_b));
}

static void pcf_zbus_listener(const struct zbus_channel *chan)
{
    if (chan == &pcf_mux_channel) 
    {
        struct pcf_mux_msgs *msg = zbus_chan_msg(&pcf_mux_channel);
        current_mux = *msg; 
    }

    app_worker_submit(&pcf_work,COMMON_WORKER);
}

static int mcp23017_out_init(void)
{
    const struct device *i2c_dev = DEVICE_DT_GET(I2C1_NODE);
    if (!device_is_ready(i2c_dev)) {
        LOG_ERR("I2C1 bus not ready for MCP23017!");
        return -ENODEV;
    }

    mcp_spec.bus = i2c_dev;
    mcp_spec.addr = MCP23017_ADDR;

    k_work_init(&pcf_work, pcf_work_handler);


    k_poll_event_init(&mcp_poll_events[0],
                      K_POLL_TYPE_MSGQ_DATA_AVAILABLE, // Тип: появление данных в очереди
                      K_POLL_MODE_NOTIFY_ONLY,         // Режим: только уведомить
                      &mcp_rx_queue); 

    k_work_poll_init(&mcp_task, _mcp_update_handler); 
    app_worker_poll_submit(&mcp_task, mcp_poll_events, COMMON_WORKER);


    /* 
     * КРИТИЧЕСКИ ВАЖНО: 
     * 1. Сбрасываем регистр конфигурации IOCON (адрес 0x0A) в 0x00, 
     *    чтобы перевести чип в стандартный BANK=0 (отключаем сдвиги банков).
     * 2. Настраиваем IODIRA (0x00) и IODIRB (0x01) на выходы (0x00).
     */
    uint8_t iocon_cmd[2]  = { 0x0A, 0x00 }; // Сброс IOCON в дефолт
    uint8_t iodira_cmd[2] = { 0x00, 0x00 }; // IODIRA = выходы
    uint8_t iodirb_cmd[2] = { 0x01, 0x00 }; // IODIRB = выходы

    i2c_write_dt(&mcp_spec, iocon_cmd, sizeof(iocon_cmd));
    i2c_write_dt(&mcp_spec, iodira_cmd, sizeof(iodira_cmd));
    i2c_write_dt(&mcp_spec, iodirb_cmd, sizeof(iodirb_cmd));

    return 0;
}

SYS_INIT(mcp23017_out_init, APPLICATION, 70);

PARAM_ROUTE_DEFINE(JTAG_MUX0,  &pcf_mux_channel, 0, UINT_DATA);
PARAM_ROUTE_DEFINE(USB_FB_MUX, &pcf_mux_channel, 1, UINT_DATA);

PARAM_ROUTE_DEFINE(JTAG_MUX_EN, &pcf_bool_channel, 0, BOOL_DATA);
PARAM_ROUTE_DEFINE(EN_USB_UART, &pcf_bool_channel, 1, BOOL_DATA);
PARAM_ROUTE_DEFINE(EN_USB_OUT1, &pcf_bool_channel, 2, BOOL_DATA);
PARAM_ROUTE_DEFINE(EN_USB_OUT2, &pcf_bool_channel, 3, BOOL_DATA);
PARAM_ROUTE_DEFINE(EN_USB_OUT3, &pcf_bool_channel, 4, BOOL_DATA);
PARAM_ROUTE_DEFINE(UART_FB_EN,  &pcf_bool_channel, 5, BOOL_DATA);
PARAM_ROUTE_DEFINE(UART_RX_EN,  &pcf_bool_channel, 6, BOOL_DATA);
PARAM_ROUTE_DEFINE(OK2,         &pcf_bool_channel, 7, BOOL_DATA);
PARAM_ROUTE_DEFINE(OK1,         &pcf_bool_channel, 8, BOOL_DATA);