#include "driver_ads1115.h"
#include "app_worker.h"
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ads_driver, LOG_LEVEL_DBG);

#define I2C1_NODE DT_NODELABEL(i2c1)

#define GPIOD_NODE DT_NODELABEL(gpiod)
static const struct gpio_dt_spec int_pin = {
    .port = DEVICE_DT_GET(GPIOD_NODE),
    .pin = 11,
    .dt_flags = GPIO_INPUT | GPIO_PULL_UP
};

static struct gpio_callback int_cb_data;
static struct k_work ads_work;
static struct i2c_dt_spec ads_spec;

static uint8_t current_channel = 0;
static struct ads1115_snapshot current_snapshot = 
{
    .voltages_mv = {0},
    .channel_ready = {false}
};

ZBUS_CHAN_DEFINE(ads_channel,
                 struct ads1115_snapshot,
                 NULL,
                 NULL,
                 ZBUS_OBSERVERS_EMPTY,
                 ZBUS_MSG_INIT(
                     .voltages_mv = {0.0f, 0.0f, 0.0f, 0.0f},
                     .channel_ready = {false, false, false, false}
                 ));

static void ads_work_handler(struct k_work *work)
{
    uint8_t read_buf[2];
    uint8_t pointer_reg = 0x00;

    // Читаем данные (это должно сбросить INT в HIGH)
    int ret = i2c_write_read_dt(&ads_spec, &pointer_reg, 1, read_buf, 2);
    if (ret < 0) {
        LOG_ERR("I2C read failed: %d", ret);
        return;
    }

    int16_t raw_value = (int16_t)((read_buf[0] << 8) | read_buf[1]);
    float voltage_mv = (float)raw_value * 0.125f;

    current_snapshot.voltages_mv[current_channel] = voltage_mv;
    current_snapshot.channel_ready[current_channel] = true;
    zbus_chan_pub(&ads_channel, &current_snapshot, K_NO_WAIT);

    LOG_DBG("Ch %d -> Voltage: %.2f mV", current_channel, voltage_mv);

    // Переключаем канал
    current_channel = (current_channel + 1) % 4;

    uint8_t mux_bits = 0x80 + (current_channel << 4);
    uint8_t config_cmd[3] = { 
        0x01,              
        mux_bits | 0x03,   // Single-shot, OS=1, PGA=±4.096V, MODE=1
        0x00               // Алерт активен
    };
    
    i2c_write_dt(&ads_spec, config_cmd, sizeof(config_cmd));
}

static void ads_isr_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    if (!k_work_is_pending(&ads_work)) {
        app_worker_submit(&ads_work);
    }
}

static int ads1115_driver_init(void)
{
    const struct device *i2c_dev = DEVICE_DT_GET(I2C1_NODE);
    if (!device_is_ready(i2c_dev)) {
        LOG_ERR("I2C1 bus not ready!");
        return -ENODEV;
    }

    if (!device_is_ready(int_pin.port)) {
        LOG_ERR("GPIOD port not ready!");
        return -ENODEV;
    }

    ads_spec.bus = i2c_dev;
    ads_spec.addr = ADS1115_DEFAULT_ADDR;

    k_work_init(&ads_work, ads_work_handler);

    // Задаем пороги для Conversion Ready
    uint8_t lo_thresh[3] = { 0x02, 0x00, 0x00 };
    uint8_t hi_thresh[3] = { 0x03, 0x80, 0x00 };
    i2c_write_dt(&ads_spec, lo_thresh, 3);
    i2c_write_dt(&ads_spec, hi_thresh, 3);

    // ВАЖНО: Настраиваем прерывание на ОБА ФРОНТА (и падение, и поднятие), 
    // чтобы увидеть в логах любую активность на ножке PD11!
    gpio_pin_configure_dt(&int_pin, GPIO_INPUT);
    gpio_pin_interrupt_configure_dt(&int_pin, GPIO_INT_EDGE_FALLING);

    gpio_init_callback(&int_cb_data, ads_isr_handler, BIT(int_pin.pin));
    gpio_add_callback(int_pin.port, &int_cb_data);

    // Стартуем первый замер на канале 0
    uint8_t config_cmd[3] = { 0x01, 0x83, 0x00 }; // AIN0, Single-shot, OS=1, Алерт активен
    i2c_write_dt(&ads_spec, config_cmd, sizeof(config_cmd));

    LOG_INF("ADS1115 Edge-Debug Driver initialized!");
    return 0;
}

SYS_INIT(ads1115_driver_init, APPLICATION, 90);