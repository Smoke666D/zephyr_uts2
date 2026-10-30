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

static struct k_work pcf_work;
static struct i2c_dt_spec mcp_spec;

ZBUS_LISTENER_DEFINE(pcf_listener, pcf_zbus_listener);

ZBUS_CHAN_DEFINE(pcf_bool_channel,
                 struct pcf_bool_array_9_msg,
                 NULL, NULL,
                 ZBUS_OBSERVERS(pcf_listener),
                 ZBUS_MSG_INIT(0));

ZBUS_CHAN_DEFINE(pcf_mux_channel,
                 struct pcf_mux_msgs,
                 NULL, NULL,
                 ZBUS_OBSERVERS(pcf_listener),
                 ZBUS_MSG_INIT(0));

static const uint8_t mcp_pin_mapping[9] = {
    8,  // 0 -> GPA0
    12,  // 1 -> GPA4
    13,  // 2 -> GPA5
    14,  // 3 -> GPA6
    15,  // 4 -> GPA7
    3,  // 5 -> GPB3 (индекс 8 в маске = порт B, пин 3)
    5, // 6 -> GPB5
    6, // 7 -> GPB6
    7  // 8 -> GPB7
};

static void mcp23017_dump_all_registers(const struct i2c_dt_spec *spec, const char *tag)
{
    uint8_t all_regs[22];
    for (uint8_t reg = 0; reg <= 0x15; reg++) {
        uint8_t reg_addr = reg;
        uint8_t val = 0;
        if (i2c_write_read_dt(spec, &reg_addr, 1, &val, 1) < 0) {
            all_regs[reg] = 0xEE;
        } else {
            all_regs[reg] = val;
        }
    }

    LOG_INF("========== MCP23017 DUMP: %s ==========", tag);
    LOG_INF("0x00 IODIRA: 0x%02X | 0x01 IODIRB: 0x%02X", all_regs[0x00], all_regs[0x01]);
    LOG_INF("0x0A IOCONA: 0x%02X | 0x0B IOCONB: 0x%02X", all_regs[0x0A], all_regs[0x0B]);
    LOG_INF("0x12 GPIOA:  0x%02X | 0x13 GPIOB:  0x%02X", all_regs[0x12], all_regs[0x13]);
    LOG_INF("0x14 OLATA:  0x%02X | 0x15 OLATB:  0x%02X", all_regs[0x14], all_regs[0x15]);
    LOG_INF("============================================");
}

static void pcf_work_handler(struct k_work *work)
{
    uint8_t porta = 0x00; 
    uint8_t portb = 0x00; 

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
    portb |= (current_mux.group[0] & 0x0003) << 1;
    porta |= (current_mux.group[1] & 0x0003);

    LOG_INF("Target values to write -> porta=0x%02X, portb=0x%02X", porta, portb);


    // Записываем данные в GPIOA (0x12) и GPIOB (0x13)
    uint8_t cmd_a[2] = { 0x12, porta }; 
    uint8_t cmd_b[2] = { 0x13, portb }; 

    i2c_write_dt(&mcp_spec, cmd_a, sizeof(cmd_a));
    i2c_write_dt(&mcp_spec, cmd_b, sizeof(cmd_b));


}

static void pcf_zbus_listener(const struct zbus_channel *chan)
{
    if (chan == &pcf_bool_channel) 
    {
        struct pcf_bool_array_9_msg *msg = zbus_chan_msg(&pcf_bool_channel);
        current_bools = *msg; 
    } 
    else if (chan == &pcf_mux_channel) 
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

PARAM_ROUTE_DEFINE(JTAG_MUX0, &pcf_mux_channel, 0, UINT_DATA);
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