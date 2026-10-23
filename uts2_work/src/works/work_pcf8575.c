#include "app_worker.h"
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <stdint.h>
#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include "system_bus_model.h"
#include "app_worker.h"

LOG_MODULE_REGISTER(pcf_out, LOG_LEVEL_INF);

#define I2C1_NODE DT_NODELABEL(i2c1)


#define PCF8575_ADDR 0x20

/* 1. Структура для булевого канала (9 дискретных пинов) */
typedef struct pcf_bool_array_9_msg 
{
    bool states[9]; 
} pcf_bool_array_9_msg ;

/* 2. Структура для uint32_t канала (упакованы две группы) */
struct pcf_mux_msgs {
    uint8_t group_1_3;   /* Допустимо: 1, 2, 4, 5, 6 */
    uint8_t group_10_12; /* Допустимо: 0, 1, 2, 3 */
};

static void pcf_zbus_listener(const struct zbus_channel *chan);
/* Текущие состояния сообщений от обоих каналов */
static struct pcf_bool_array_9_msg current_bools = { {false} };
static struct pcf_mux_msgs         current_mux   = {0};

static struct k_work pcf_work;
static struct i2c_dt_spec pcf_spec;

/* Определяем Zbus канал для булевых пинов */
ZBUS_LISTENER_DEFINE(pcf_listener, pcf_zbus_listener);

/* 2. Привязываем наблюдателя прямо внутри макроса определения каналов (поле observers) */
ZBUS_CHAN_DEFINE(pcf_bool_channel,
                 struct pcf_bool_array_9_msg,
                 NULL, NULL,
                 ZBUS_OBSERVERS(pcf_listener),
                 ZBUS_MSG_INIT({ {false} }));

ZBUS_CHAN_DEFINE(pcf_mux_channel,
                 struct pcf_mux_msgs,
                 NULL, NULL,
                 ZBUS_OBSERVERS(pcf_listener),
                 ZBUS_MSG_INIT(0));

static const uint8_t pcf_pin_mapping[9] = {
    0,  // индекс 0 -> PO0  (бит 0)
    4,  // индекс 1 -> PO4  (бит 4)
    5,  // индекс 2 -> PO5  (бит 5)
    6,  // индекс 3 -> PO6  (бит 6)
    7,  // индекс 4 -> PO7  (бит 7)
    11, // индекс 5 -> PO13 (бит 11, порт 1 пин 3)
    13, // индекс 6 -> PO15 (бит 13, порт 1 пин 5)
    14, // индекс 7 -> PO16 (бит 14, порт 1 пин 6)
    15  // индекс 8 -> PO17 (бит 15, порт 1 пин 7)
};


/* Функция сборки маски и отправки в PCF8575 (выполняется в воркере) */
static void pcf_work_handler(struct k_work *work)
{
    uint16_t mask = 0x0000;

    /* 1. Собираем маску из массива ровно в 9 элементов по таблице маппинга */
    for (int i = 0; i < 9; i++) {
        if (current_bools.states[i]) {
            mask |= (1 << pcf_pin_mapping[i]);
        }
    }

    /* 2. Накладываем группу PO1 - PO3 (биты 1, 2, 3) */
    mask &= ~((1 << 1) | (1 << 2) | (1 << 3));
    uint8_t g1 = current_mux.group_1_3;
    if (g1 == 1 || g1 == 2 || g1 == 4 || g1 == 5 || g1 == 6) {
        mask |= ((g1 & 0x07) << 1);
    }

    /* 3. Накладываем группу PO10 - PO12 (биты 8, 9, 10) */
    mask &= ~((1 << 8) | (1 << 9) | (1 << 10));
    uint8_t g2 = current_mux.group_10_12;
    if (g2 <= 3) {
        mask |= ((g2 & 0x03) << 8);
    }

 mask = 0xFFFF;
    /* 4. Отправляем 2 байта на шину I2C1 */
    uint8_t tx_buf[2];
    tx_buf[0] = (uint8_t)(mask & 0xFF);        /* P07–P00 (Младший байт) */
    tx_buf[1] = (uint8_t)((mask >> 8) & 0xFF); /* P17–P10 (Старший байт) */

    int ret = i2c_write_dt(&pcf_spec, tx_buf, sizeof(tx_buf));
    if (ret < 0) {
        LOG_ERR("Failed to write to PCF8575: %d", ret);
    } else {
        LOG_INF("PCF8575 9-array mask sent: 0x%04X", mask);
    }
}

/* Единый Zbus Слушатель (Listener), который цепляется на оба канала */
static void pcf_zbus_listener(const struct zbus_channel *chan)
{
    if (chan == &pcf_bool_channel) {
        zbus_chan_read(&pcf_bool_channel, &current_bools, K_NO_WAIT);
    } else if (chan == &pcf_mux_channel) {
        zbus_chan_read(&pcf_mux_channel, &current_mux, K_NO_WAIT);
    }

    app_worker_submit(&pcf_work, REAL_TIME_WORKER);
             
}

/* Регистрируем слушателя для обоих каналов */

static int pcf8575_out_init(void)
{
    const struct device *i2c_dev = DEVICE_DT_GET(I2C1_NODE);
    if (!device_is_ready(i2c_dev)) {
        LOG_ERR("I2C1 bus not ready for PCF8575 outputs!");
        return -ENODEV;
    }

    pcf_spec.bus = i2c_dev;
    pcf_spec.addr = PCF8575_ADDR;

    k_work_init(&pcf_work, pcf_work_handler);

    LOG_INF("PCF8575 Zbus-driven Output Driver initialized!");
    return 0;
}

SYS_INIT(pcf8575_out_init, APPLICATION, 70);

PARAM_ROUTE_DEFINE(JTAG_MUX0, &pcf_mux_channel, 0, ARRAY_DATA);
PARAM_ROUTE_DEFINE(USB_FB_MUX, &pcf_mux_channel, 1, ARRAY_DATA);

PARAM_ROUTE_DEFINE(JTAG_MUX_EN, &pcf_bool_channel, 0, BOOL_DATA);
PARAM_ROUTE_DEFINE(EN_USB_UART, &pcf_bool_channel, 1, BOOL_DATA);
PARAM_ROUTE_DEFINE(EN_USB_OUT1,     &pcf_bool_channel, 2, BOOL_DATA);
PARAM_ROUTE_DEFINE(EN_USB_OUT2,     &pcf_bool_channel, 3, BOOL_DATA);
PARAM_ROUTE_DEFINE(EN_USB_OUT3,       &pcf_bool_channel, 4, BOOL_DATA);
PARAM_ROUTE_DEFINE(UART_FB_EN,  &pcf_bool_channel, 5, BOOL_DATA);
PARAM_ROUTE_DEFINE(UART_RX_EN, &pcf_bool_channel, 6, BOOL_DATA);
PARAM_ROUTE_DEFINE(OK2,  &pcf_bool_channel, 7, BOOL_DATA);
PARAM_ROUTE_DEFINE(OK1,  &pcf_bool_channel, 8, BOOL_DATA);
