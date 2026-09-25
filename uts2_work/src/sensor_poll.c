#include "app_worker.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

LOG_MODULE_REGISTER(sensor_poll, LOG_LEVEL_INF);

static struct k_work_delayable sensor_poll_dwork;

/* Структура кэша данных датчика */
struct sensor_cache_slot {
    const struct device *dev;
    enum sensor_channel channel;
    float cached_value;
    bool valid;
};

/* Инициализируем массив слотов кэша */
static struct sensor_cache_slot sensors_cache_slots[] = {
    { .dev = DEVICE_DT_GET(DT_NODELABEL(tmp112_gnd)), .channel = SENSOR_CHAN_AMBIENT_TEMP },
    { .dev = DEVICE_DT_GET(DT_NODELABEL(tmp112_vdd)), .channel = SENSOR_CHAN_AMBIENT_TEMP },
    { .dev = DEVICE_DT_GET(DT_NODELABEL(bh1750_gnd)), .channel = SENSOR_CHAN_LIGHT },
    { .dev = DEVICE_DT_GET(DT_NODELABEL(bh1750_vdd)), .channel = SENSOR_CHAN_LIGHT },
};

K_MUTEX_DEFINE(cache_mutex);

/* 
 * Обработчик фонового воркера: опросит датчики стандартным методом fetch/get
 */
static void sensor_poll_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    k_mutex_lock(&cache_mutex, K_FOREVER);

    for (int i = 0; i < ARRAY_SIZE(sensors_cache_slots); i++) {
        struct sensor_cache_slot *slot = &sensors_cache_slots[i];

        if (!device_is_ready(slot->dev)) {
            slot->valid = false;
            continue;
        }

        /* Читаем данные классическим методом сенсоров */
        if (sensor_sample_fetch(slot->dev) == 0) {
            struct sensor_value val;
            if (sensor_channel_get(slot->dev, slot->channel, &val) == 0) {
                /* Переводим struct sensor_value в стандартный float */
                slot->cached_value = (float)val.val1 + (float)val.val2 / 1000000.0f;
                slot->valid = true;
            } else {
                slot->valid = false;
            }
        } else {
            slot->valid = false;
        }
    }

    k_mutex_unlock(&cache_mutex);

    /* Перезапускаем воркер ровно через 500 мс */
    app_worker_reschedule_submit(&sensor_poll_dwork, K_MSEC(500));
}

static int app_worker_system_init(void)
{
    k_work_init_delayable(&sensor_poll_dwork, sensor_poll_handler);
    
    /* Запуск первого опроса через 100 мс */
    app_worker_reschedule_submit(&sensor_poll_dwork, K_MSEC(100));

    LOG_INF("Фоновый опрос датчиков успешно инициализирован");
    return 0;
}

/* Публичная функция для чтения уже готового float из других файлов */
bool app_worker_get_sensor_float(int index, float *out_val)
{
    bool success = false;
    if (index < 0 || index >= ARRAY_SIZE(sensors_cache_slots)) {
        return false;
    }

    k_mutex_lock(&cache_mutex, K_FOREVER);
    if (sensors_cache_slots[index].valid) {
        *out_val = sensors_cache_slots[index].cached_value;
        success = true;
    }
    k_mutex_unlock(&cache_mutex);

    return success;
}

SYS_INIT(app_worker_system_init, APPLICATION, 50);