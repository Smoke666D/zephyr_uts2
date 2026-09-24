#include "driver_ads1115.h"
#include "app_worker.h"
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>


LOG_MODULE_REGISTER(ads_driver, LOG_LEVEL_INF);

#define ADS_NODE DT_NODELABEL(ads1115_custom)

#if !DT_NODE_EXISTS(ADS_NODE)
#error "Node ads1115_custom is not defined in devicetree!"
#endif

/* Шина I2C и адрес из DTS */
static const struct i2c_dt_spec ads_spec = I2C_DT_SPEC_GET(ADS_NODE);

/* Пин прерывания по имени свойства 'ads-rdy' (препроцессор превратит в ads_rdy) */
static const struct gpio_dt_spec int_pin = GPIO_DT_SPEC_GET(ADS_NODE, ads_rdy);

static struct gpio_callback int_cb_data;
static struct k_work ads_work;

static uint8_t current_channel = 0;
static uint8_t dr_config_bits = 0xE0; // Дефолт 860 SPS

static struct ads1115_snapshot current_snapshot = {

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


    int ret = i2c_write_read_dt(&ads_spec, &pointer_reg, 1, read_buf, 2);
    if (ret < 0) {
        LOG_ERR("I2C read failed: %d", ret);
        return;
    }

    int16_t raw_value = (int16_t)((read_buf[0] << 8) | read_buf[1]);
    float voltage_mv = (float)raw_value * 0.125f;


   if (current_channel == 0)
    {
        float slope_mv_per_db = 21.5f;   // 21.5 мВ на 1 дБ (из даташита AD8314)
        float intercept_mv = 300.0f;     // Базовое смещение в мВ (пример для калибровки)
        
        // Вычисляем относительный уровень мощности (условные dBm или dB)
        float power_db = (voltage_mv - intercept_mv) / slope_mv_per_db;
        current_snapshot.voltages_mv[current_channel] = power_db;
    }
    else 
    {
        current_snapshot.voltages_mv[current_channel] = voltage_mv;
    }
    
    current_snapshot.channel_ready[current_channel] = true;
    zbus_chan_pub(&ads_channel, &current_snapshot, K_NO_WAIT);

   

    // Переключаем канал
    current_channel = (current_channel + 1) % 4;

    uint8_t mux_bits = 0x80 + ((current_channel | 0x4) << 4);
    uint8_t config_cmd[3] = { 
        0x01,              
        mux_bits | 0x03,   
        dr_config_bits     


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

    if (!device_is_ready(ads_spec.bus)) {
        LOG_ERR("I2C bus for ADS1115 is not ready!");
        return -ENODEV;
    }

    if (!gpio_is_ready_dt(&int_pin)) {
        LOG_ERR("ADS1115 RDY pin device is not ready!");
        return -ENODEV;
    }

    /* Читаем обязательный параметр ads-sps из DTS */
    int sps = DT_PROP(ADS_NODE, ads_sps);
    
    if      (sps <= 8)   dr_config_bits = 0x00;
    else if (sps <= 16)  dr_config_bits = 0x20;
    else if (sps <= 32)  dr_config_bits = 0x40;
    else if (sps <= 64)  dr_config_bits = 0x60;
    else if (sps <= 128) dr_config_bits = 0x80;
    else if (sps <= 250) dr_config_bits = 0xA0;
    else if (sps <= 475) dr_config_bits = 0xC0;
    else                 dr_config_bits = 0xE0; // 860 SPS

    

    // ВАЖНО: Задаем пороги компаратора для активации пина ALERT/RDY (Conversion Ready)

   
    uint8_t lo_thresh[3] = { 0x02, 0x00, 0x00 };
    uint8_t hi_thresh[3] = { 0x03, 0x80, 0x00 };
    i2c_write_dt(&ads_spec, lo_thresh, 3);
    i2c_write_dt(&ads_spec, hi_thresh, 3);


    k_work_init(&ads_work, ads_work_handler);

    // Настраиваем прерывание

    gpio_pin_configure_dt(&int_pin, GPIO_INPUT);
    gpio_pin_interrupt_configure_dt(&int_pin, GPIO_INT_EDGE_FALLING);

    gpio_init_callback(&int_cb_data, ads_isr_handler, BIT(int_pin.pin));
    gpio_add_callback(int_pin.port, &int_cb_data);


    // Стартуем первый замер на канале 0 со скоростью из DTS
    uint8_t config_cmd[3] = { 0x01, 0x83, dr_config_bits }; 
    i2c_write_dt(&ads_spec, config_cmd, sizeof(config_cmd));

    LOG_INF("ADS1115 Custom-YAML Driver initialized! (Addr: 0x%02X)", ads_spec.addr);

    return 0;
}

SYS_INIT(ads1115_driver_init, APPLICATION, 90);