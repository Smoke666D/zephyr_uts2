#include <stdint.h>
#include <stdlib.h>  /* Для strtol */
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/init.h>        /* Для SYS_INIT */
#include <zephyr/shell/shell.h>
#include "app_worker.h"
#include "system_bus_model.h"
#include <zephyr/zbus/zbus.h>
#include <zephyr/drivers/dac.h> /* Добавляем заголовок ЦАП */



#define MAX_P40V_OUT  30.0
#define MIN_P40V_OUT  12.0

LOG_MODULE_REGISTER(dac_work, LOG_LEVEL_INF);

static struct k_work dac_work; 

static void dac_control_line_listener_callback(const struct zbus_channel *chan)
{
    /* Объявление канала будет ниже, но сам указатель chan уже известен */
    app_worker_submit(&dac_work);
}




ZBUS_LISTENER_DEFINE(dac_control_line_listener, dac_control_line_listener_callback);

ZBUS_CHAN_DEFINE(dac_chan,
                 float,
                 NULL,
                 NULL,
                 ZBUS_OBSERVERS(dac_control_line_listener),
                 {0}
);

static const struct device *dac_dev = DEVICE_DT_GET(DT_NODELABEL(dac1));

static uint32_t cur_dac_value = 0;

static void dac_update_handler(struct k_work *work)
{    
    float msg;
    if (zbus_chan_read(&dac_chan, &msg, K_MSEC(50)) == 0)
    {

            float low_v = 0;
            SYSTEM_BUS_GET(ENV_VIN,  &low_v);

            msg = msg - low_v;
        
            float step = (MAX_P40V_OUT - low_v)/4095;
            cur_dac_value = 4095 - (uint32_t)(msg/step);

            dac_write_value(dac_dev, 2, (uint32_t)cur_dac_value );

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
    return 0;
}



/* 
 * Регистрируем автозапуск инициализации ЦАП на этапе POST_KERNEL.
 * Приоритет 85 (после драйверов, но до прикладных потоков и Shell) [1].
 */
SYS_INIT(dac_control_init, POST_KERNEL, 85);


PARAM_ROUTE_DEFINE(DAC_VALUE,&dac_chan,0,ARRAY_DATA);

static int cmd_dac_set(const struct shell *sh, size_t argc, char **argv)
{
    if (argc != 2) {
        shell_error(sh, "Usage: dac_set <0-4095>");
        return -EINVAL;
    }

     /* Парсим вещественное значение напряжения (float) из аргумента */
    char *endptr;
    float voltage = strtof(argv[1], &endptr);
    
    /* Проверяем корректность парсинга и границы диапазона */
    if (*endptr != '\0' || voltage < 0.0f || voltage > 30.0f) {
        shell_error(sh, "Invalid voltage: %s (Must be between 0.0 and 30 V)", argv[1]);
        return -EINVAL;
    }

    if (!device_is_ready(dac_dev)) {
        shell_error(sh, "DAC1 hardware device is not ready!");
        return -ENODEV;
    }

    SYSTEM_BUS_SET(DAC_VALUE,voltage);
    


return 0;
}



SHELL_CMD_REGISTER(dac_set, NULL, 
                   "Set DAC output value: dac_set <0-4095>", 
                   cmd_dac_set);