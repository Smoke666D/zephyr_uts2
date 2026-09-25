#include "ad5243.h"
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ad5243, LOG_LEVEL_INF);

static bool dev_is_init = false;

struct i2c_dt_spec spec = 
{
    .addr = AD5243_DEFAULT_ADDR
};

int ad5243_init(struct device *_i2c_dev, uint8_t _ch1_def, uint8_t _ch2_def)
{
        int ret;

    spec.bus = _i2c_dev;
   // 1. Сначала проверяем, готова ли шина I2C
    if (!device_is_ready(spec.bus)) {
        LOG_ERR("I2C шина не готова!");
        return -ENODEV;
    }

    // 2. Устанавливаем дефолтное значение для канала 1
    ret = ad5243_set_wiper(AD5243_CHANNEL_1, _ch1_def);
    if (ret < 0) {
        LOG_ERR("Ошибка установки канала 1, код: %d", ret);
        return ret;
    }

    // 3. Устанавливаем дефолтное значение для канала 2
    ret = ad5243_set_wiper(AD5243_CHANNEL_2, _ch2_def);
    if (ret < 0) {
        LOG_ERR("ЁЁОшибка установки канала 2, код: %d", ret);
        return ret;
    }

    dev_is_init = true;
    LOG_INF("AD5243 успешно инициализирован");
    return 0;
}

int ad5243_set_wiper(ad5243_channel_t channel, uint8_t value)
{
    
    uint8_t tx_data[2];
    
    if (channel == AD5243_CHANNEL_1) {
        tx_data[0] = 0x00; // Выбор канала 1 (W1)
    } else {
        tx_data[0] = 0x80; // Выбор канала 2 (W2)
    }

    tx_data[1] = value; // Значение положения движка (0..255)
    int ret = i2c_write_dt(&spec, tx_data, sizeof(tx_data));
    if (ret < 0) {
        LOG_DBG("i2c_write_dt failed with error %d for channel %d", ret, channel);
    }
    
    return ret;  
  
        
}
