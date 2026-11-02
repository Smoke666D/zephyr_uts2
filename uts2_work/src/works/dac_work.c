#include <stdint.h>
#include <stdlib.h>  /* Для strtol */
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/init.h>        /* Для SYS_INIT */
#include <zephyr/shell/shell.h>
#include "app_worker.h"
#include "system_bus_model.h"
#include <zephyr/drivers/i2c.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/drivers/dac.h> /* Добавляем заголовок ЦАП */
#include <math.h>


LOG_MODULE_REGISTER(dac_work, LOG_LEVEL_INF);

#define I2C1_NODE DT_NODELABEL(i2c1)

#define AD5243_DEFAULT_ADDR 0x2F

#define MAX_P40V_OUT  30.0
#define MIN_P40V_OUT  12.0

/* Физические константы потенциометра AD5243BRMZ100 */
#define AD5243_R_AB_KOHM     100.0f  /* Номинал 100 кОм [1.1.2, 1.1.8] */
#define AD5243_R_W_KOHM      0.12f   /* Сопротивление ползунка 120 Ом (0.12 кОм) [1.1.2, 1.1.8] */

/* Максимальное физически достижимое сопротивление (~99.73 кОм) [1.1.2] */
#define AD5243_R_MAX_KOHM    (AD5243_R_W_KOHM + (AD5243_R_AB_KOHM * 255.0f / 256.0f))


typedef enum {
    AD5243_CHANNEL_1 = 0,
    AD5243_CHANNEL_2 = 1
} ad5243_channel_t;

int ad5243_init(struct device *_i2c_dev, uint8_t _ch1_def, uint8_t _ch2_def);
int ad5243_set_wiper(ad5243_channel_t channel, uint8_t value);

typedef struct dac_data_msg {
    float value[TOTAL_DAC_COUNT]; // Наш тестовый буфер (8 шагов * 2 канала)
} dac_data_msg ;

static struct k_work dac_work; 
static dac_data_msg _cache_dac_data={0};
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

static void dac_control_line_listener_callback(const struct zbus_channel *chan)
{
    /* Объявление канала будет ниже, но сам указатель chan уже известен */
    app_worker_submit(&dac_work,COMMON_WORKER);
}


ZBUS_LISTENER_DEFINE(dac_control_line_listener, dac_control_line_listener_callback);

ZBUS_CHAN_DEFINE(dac_chan,
                 struct dac_data_msg,
                 NULL,
                 NULL,
                 ZBUS_OBSERVERS(dac_control_line_listener),
                 {0}
);

static const struct device *dac_dev = DEVICE_DT_GET(DT_NODELABEL(dac1));

static uint32_t cur_dac_value = 0;


int ad5243_set_resistance_kohm( ad5243_channel_t _ch, float target_r_kohm)
{
    uint8_t wiper_val;

    /* 1. Защита диапазона (Clamping) */
    if (target_r_kohm <= AD5243_R_W_KOHM) {
        /* Все, что ниже сопротивления ползунка, приравниваем к коду 0 [1.1.2] */
        wiper_val = 0;
    } else if (target_r_kohm >= AD5243_R_MAX_KOHM) {
        /* Все, что выше максимального физического предела, приравниваем к коду 255 [1.1.2] */
        wiper_val = 255;
    } else {
        /* 
         * 2. Вычисляем код: D = 256 * (R_WB - R_W) / R_AB [1.1.2]
         * Прибавляем 0.5f для математически корректного округления [1.1.2].
         */
        float calc_val = (256.0f * (target_r_kohm - AD5243_R_W_KOHM)) / AD5243_R_AB_KOHM;
        wiper_val = (uint8_t)(calc_val + 0.5f);
    }

    /* 3. Отправляем вычисленный байт в Канал 2 через ваш драйвер I2C */
    return ad5243_set_wiper(_ch, wiper_val);
}



static void dac_update_handler(struct k_work *work)
{    
    dac_data_msg msg;
    if (zbus_chan_read(&dac_chan, &msg, K_MSEC(50)) == 0)
    {

        if (msg.value[0]!=_cache_dac_data.value[0])
        {
            _cache_dac_data.value[0] = msg.value[0];
            float low_v = 0;
            SYSTEM_BUS_GET(ENV_VIN,  &low_v);

            float _temp = msg.value[0] - low_v;
        
            float step = (MAX_P40V_OUT - low_v)/4095;
            cur_dac_value = 4095 - (uint32_t)(_temp/step);

            dac_write_value(dac_dev, 2, (uint32_t)cur_dac_value );
        }
        if (msg.value[1]!=_cache_dac_data.value[1])
        {
            _cache_dac_data.value[1] = msg.value[1];

            ad5243_set_resistance_kohm(AD5243_CHANNEL_1,_cache_dac_data.value[1]);
        

        }
        if (msg.value[2]!=_cache_dac_data.value[2])
        {
            _cache_dac_data.value[2] = msg.value[2];
            ad5243_set_resistance_kohm(AD5243_CHANNEL_2,_cache_dac_data.value[2]);
        }

    }
}



static int dac_control_init(void)
{
    int err;

    if (!device_is_ready(dac_dev)) {
        LOG_ERR("DAC1 hardware device is not ready during boot!");
        return -ENODEV;
    }


    struct dac_channel_cfg dac_ch_cfg = 
    {
        .channel_id = 2, /* PA5 */
        .resolution = 12
    };

    /* Автоматически настраиваем Канал 2 при старте системы [1] */
    err = dac_channel_setup(dac_dev, &dac_ch_cfg);
    if (err) {
        LOG_ERR("Failed to setup DAC channel 2 during boot: %d", err);
        return err;
    }

     k_work_init(&dac_work, dac_update_handler);

    LOG_INF("DAC1 Channel 2 (PA5) successfully initialized during boot.");


    ad5243_init(DEVICE_DT_GET(I2C1_NODE), 0, 0);
    return 0;
}



/* 
 * Регистрируем автозапуск инициализации ЦАП на этапе POST_KERNEL.
 * Приоритет 85 (после драйверов, но до прикладных потоков и Shell) [1].
 */
SYS_INIT(dac_control_init, POST_KERNEL, 85);


PARAM_ROUTE_DEFINE(DAC_VALUE,&dac_chan,0,FLOAT_DATA);
PARAM_ROUTE_DEFINE(ADJ_RES1,&dac_chan,1,FLOAT_DATA);
PARAM_ROUTE_DEFINE(ADJ_RES2,&dac_chan,2,FLOAT_DATA);

