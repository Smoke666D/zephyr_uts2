#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor_data_types.h>
#include <zephyr/rtio/rtio.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>
#include "ina228_stram_thread.h"
#include "global_params.h"

LOG_MODULE_REGISTER(ina_rtio_poller, LOG_LEVEL_INF);

#define NUM_SENSORS 8
#define POLL_INTERVAL_MS 5

ZBUS_CHAN_DEFINE(ina_batch_chan,
                 struct ina_batch_msg,
                 NULL,
                 NULL,
                 ZBUS_OBSERVERS_EMPTY,
                 ZBUS_MSG_INIT(0)
);

/* ----------------- 2. RTIO Описание Устройств ----------------- */

#define INA_CHANNELS \
    { SENSOR_CHAN_VOLTAGE, 0 }, \
    { SENSOR_CHAN_CURRENT, 0 }

SENSOR_DT_READ_IODEV(ina_iodev_0, DT_NODELABEL(cur_sens_brd_low),   INA_CHANNELS);
SENSOR_DT_READ_IODEV(ina_iodev_1, DT_NODELABEL(cur_sens_brd_high),  INA_CHANNELS);
SENSOR_DT_READ_IODEV(ina_iodev_2, DT_NODELABEL(cur_sens_vdut2),     INA_CHANNELS);
SENSOR_DT_READ_IODEV(ina_iodev_3, DT_NODELABEL(cur_sens_vdut3),     INA_CHANNELS);
SENSOR_DT_READ_IODEV(ina_iodev_4, DT_NODELABEL(cur_sens_vin),       INA_CHANNELS);
SENSOR_DT_READ_IODEV(ina_iodev_5, DT_NODELABEL(cur_sens_3_3_dcdc),  INA_CHANNELS);
SENSOR_DT_READ_IODEV(ina_iodev_6, DT_NODELABEL(cur_sens_vdout_pwr), INA_CHANNELS);
SENSOR_DT_READ_IODEV(ina_iodev_7, DT_NODELABEL(cur_sens_5vd),       INA_CHANNELS);

static const struct rtio_iodev *const iodevs[NUM_SENSORS] = {
    &ina_iodev_0, &ina_iodev_1, &ina_iodev_2, &ina_iodev_3,
    &ina_iodev_4, &ina_iodev_5, &ina_iodev_6, &ina_iodev_7
};

static const struct device *const devices[NUM_SENSORS] = {
    DEVICE_DT_GET(DT_NODELABEL(cur_sens_brd_low)),
    DEVICE_DT_GET(DT_NODELABEL(cur_sens_brd_high)),
    DEVICE_DT_GET(DT_NODELABEL(cur_sens_vdut2)),
    DEVICE_DT_GET(DT_NODELABEL(cur_sens_vdut3)),
    DEVICE_DT_GET(DT_NODELABEL(cur_sens_vin)),
    DEVICE_DT_GET(DT_NODELABEL(cur_sens_3_3_dcdc)),
    DEVICE_DT_GET(DT_NODELABEL(cur_sens_vdout_pwr)),
    DEVICE_DT_GET(DT_NODELABEL(cur_sens_5vd))
};

/* Названия датчиков для понятного логирования */
static const char *const sensor_names[NUM_SENSORS] = {
    "BRD_LOW (0x40)",
    "BRD_HIGH (0x41)",
    "VDUT2 (0x42)",
    "VDUT3 (0x43)",
    "VIN (0x44)",
    "3_3_DCDC (0x45)",
    "VDOUT_PWR (0x46)",
    "5VD (0x47)"
};

RTIO_DEFINE_WITH_MEMPOOL(ina_rtio, 
                         NUM_SENSORS, 
                         NUM_SENSORS, 
                         NUM_SENSORS * 2, 
                         128, 
                         sizeof(void *));

static inline double q31_to_double(int32_t value, int8_t shift)
{
    return (double)value / (double)(1ULL << (31 - shift));
}

/* ----------------- 3. Поток Поллинга (Thread) ----------------- */
#define THREAD_STACK_SIZE 3072
#define THREAD_PRIORITY 4

void rti_poller_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    for (int i = 0; i < NUM_SENSORS; i++) {
        if (!device_is_ready(devices[i])) {
            LOG_ERR("Device %s [%s] not ready!", devices[i]->name, sensor_names[i]);
            return;
        }
    }

    LOG_INF("All INA228 sensors are ready. Starting RTIO/ZBUS loop.");

    struct ina_batch_msg batch;

    while (1) {
        memset(&batch, 0, sizeof(batch));

        /* Старт асинхронного сбора данных со всех 8 узлов */
        for (int i = 0; i < NUM_SENSORS; i++) {
            int rc = sensor_read_async_mempool(iodevs[i], &ina_rtio, (void *)(uintptr_t)i);
            if (rc < 0) {
                LOG_ERR("Failed to queue async read for %s: %d", sensor_names[i], rc);
            }
        }

        /* Ожидание выполнения транзакций и декодирование */
        int completions_received = 0;
        while (completions_received < NUM_SENSORS) {
            struct rtio_cqe *cqe = rtio_cqe_consume_block(&ina_rtio);
            if (cqe == NULL) {
                break;
            }

            int idx = (int)(uintptr_t)cqe->userdata;

            if (cqe->result >= 0) {
                uint8_t *buf = NULL;
                uint32_t buf_len = 0;
                rtio_cqe_get_mempool_buffer(&ina_rtio, cqe, &buf, &buf_len);

                const struct sensor_decoder_api *decoder = NULL;
                sensor_get_decoder(devices[idx], &decoder);

                if (decoder != NULL && buf != NULL) {
                    uint32_t fit = 0;
                    struct sensor_q31_data v_data = {0};
                    struct sensor_q31_data i_data = {0};

                    /* Декодируем напряжение */
                    int dec_rc = decoder->decode(buf, 
                                                 (struct sensor_chan_spec){SENSOR_CHAN_VOLTAGE, 0}, 
                                                 &fit, 1, &v_data);
                    if (dec_rc > 0) {
                        batch.sensors[idx].voltage = q31_to_double(v_data.readings[0].value, v_data.shift);
                    }

                    /* Декодируем ток */
                    fit = 0;
                    dec_rc = decoder->decode(buf, 
                                             (struct sensor_chan_spec){SENSOR_CHAN_CURRENT, 0}, 
                                             &fit, 1, &i_data);
                    if (dec_rc > 0) {
                        batch.sensors[idx].current = q31_to_double(i_data.readings[0].value, i_data.shift);
                    }
                }

                rtio_release_buffer(&ina_rtio, buf, buf_len);
            } else {
                LOG_ERR("Sensor %s read failed with error: %d", sensor_names[idx], cqe->result);
            }

            rtio_cqe_release(&ina_rtio, cqe);
            completions_received++;
        }

        /* Вывод отладочной информации в лог */
        for (int i = 0; i < NUM_SENSORS; i++) {
            LOG_INF("%s | V: %.3f V | I: %.6f A", 
                    sensor_names[i], 
                    batch.sensors[i].voltage, 
                    batch.sensors[i].current);
        }

        /* Публикация пакета данных в ZBUS для остальных потоков */
        zbus_chan_pub(&ina_batch_chan, &batch, K_NO_WAIT);

        k_msleep(POLL_INTERVAL_MS);
    }
}

K_THREAD_DEFINE(ina228_rtio_tid, THREAD_STACK_SIZE,
                rti_poller_thread, NULL, NULL, NULL,
                THREAD_PRIORITY, 0, 0);