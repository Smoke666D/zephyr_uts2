#include <stdint.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>
#include <zephyr/rtio/rtio.h>
#include <zephyr/drivers/spi.h>
#include "system_data_bus.h"
#include <hc595_chain.h>


#include "out_and_power_control.h"

LOG_MODULE_REGISTER(out_and_power, LOG_LEVEL_INF);

/* ------------------------------------------------------------------ */
/* 1. ОБЪЯВЛЕНИЕ ПРИКЛАДНЫХ И СИСТЕМНЫХ КАНАЛОВ ZBUS                  */
/* ------------------------------------------------------------------ */

/* Прикладной канал команд (принимает желаемые состояния от приложения) */
ZBUS_CHAN_DEFINE(app_control_chan,
                 struct hc595_channels_msg,
                 NULL, NULL,
                 ZBUS_OBSERVERS_EMPTY, /* Обсервером будет листенер */
                 {0}
);

/* Системный канал примененных состояний (обновляется строго после SPI-транзакции) */
ZBUS_CHAN_DEFINE(sys_applied_chan,
                 struct hc595_channels_msg,
                 NULL, NULL,
                 ZBUS_OBSERVERS_EMPTY,
                 {0}
);

/* Прикладной канал обратной связи (диагностика, КЗ и статус) */
ZBUS_CHAN_DEFINE(app_status_chan,
                 struct hc595_status_msg,
                 NULL, NULL,
                 ZBUS_OBSERVERS_EMPTY,
                 {0}
);

/* ------------------------------------------------------------------ */
/* 2. НАСТРОЙКА ПОДСИСТЕМЫ RTIO ДЛЯ SPI                               */
/* ------------------------------------------------------------------ */

/* Объявляем контекст RTIO: размер очереди отправки 4, очереди завершения 4 */
RTIO_DEFINE(spi_rtio, 4, 4);

/* 
 * Объявляем асинхронное I/O устройство (iodev) для шины SPI нашего сдвигового регистра.
 * Связываем его с узлом "hc595_chain" из Devicetree.
 */
SPI_DT_IODEV_DEFINE(hc595_iodev, 
                    DT_NODELABEL(hc595_chain), 
                    SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB, 
                    0);

/* 
 * Буферы в оперативной памяти для асинхронной передачи.
 * Объявлены статической (глобальной) памятью, чтобы их жизненный цикл 
 * не прерывался при завершении функции листенера.
 */
static uint8_t spi_tx_buf[5];
static struct hc595_channels_msg last_submitted_msg;

/**
 * @brief Функция расчета маски для сдвигового регистра.
 */
static void hc595_calculate_mask(const struct hc595_channels_msg *msg, uint8_t *tx_data, size_t len)
{
    if (len < 5) {
        return;
    }
    memset(tx_data, 0, len);

    tx_data[0] = ((uint8_t)msg->lin_pb[0] << 4) |
                 ((uint8_t)msg->lin_pb[1] << 5) |
                 ((uint8_t)msg->lin_pb[2] << 6) |
                 ((uint8_t)msg->lin_pb[3] << 7);

    static const uint8_t state_to_bits[] = {0, 2, 1};

    for (int i = 0; i < LOW_CUR_DRIVER_COUNT; i++) {
        uint8_t chip_index = 4 - (i / 4);
        uint8_t bit_indx = (i % 4) * 2;
        LOW_CUR_DRIVER_STATE state = msg->low_cur_driver_channels[i];

        if (state < 3) {
            tx_data[chip_index] |= (state_to_bits[state] << bit_indx);
        }
    }
}

/* ------------------------------------------------------------------ */
/* 3. ЛИСТЕНЕР ПРИКЛАДНОГО КАНАЛА УПРАВЛЕНИЯ                           */
/* ------------------------------------------------------------------ */

static void app_control_listener_fn(const struct zbus_channel *chan)
{
    if (chan == &app_control_chan) {
        struct hc595_channels_msg msg;

        /* Читаем желаемые уровни */
        int err = zbus_chan_read(&app_control_chan, &msg, K_NO_WAIT);
        if (err != 0) {
            return;
        }

        /* Рассчитываем маску для SPI в глобальный буфер отправки */
        hc595_calculate_mask(&msg, spi_tx_buf, sizeof(spi_tx_buf));

        /* Сохраняем копию сообщения для последующего подтверждения */
        memcpy(&last_submitted_msg, &msg, sizeof(struct hc595_channels_msg));

        /* Получаем свободный элемент очереди отправки RTIO */
        struct rtio_sqe *sqe = rtio_sqe_acquire(&spi_rtio);
        if (sqe != NULL) {
            /* 
             * Подготавливаем асинхронную операцию записи по SPI.
             * В качестве userdata передаем указатель на копию отправленного пакета, 
             * чтобы извлечь его в коллбэке завершения.
             */
            rtio_sqe_prep_write(sqe, &hc595_iodev, RTIO_PRIO_NORM, 
                                spi_tx_buf, sizeof(spi_tx_buf), 
                                (void *)&last_submitted_msg);

            /* Запускаем асинхронную отправку в аппаратную очередь драйвера */
            rtio_submit(&spi_rtio, 1);
        } else {
            LOG_ERR("RTIO Submission Queue Overflow! Request dropped.");
        }
    }
}

/* Регистрируем листенер в Zbus */
ZBUS_LISTENER_DEFINE(app_control_lis, app_control_listener_fn);

/* ------------------------------------------------------------------ */
/* 4. ОБРАБОТЧИК ЗАВЕРШЕНИЯ ТРАНЗАКЦИЙ (CQE PROCESSOR)                */
/* ------------------------------------------------------------------ */

static void cqe_processor_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    const struct device *hc595_dev = DEVICE_DT_GET(DT_NODELABEL(hc595_chain));

    if (!device_is_ready(hc595_dev)) {
        LOG_ERR("74HC595 hardware device not ready");
        return;
    }

    /* На старте аппаратно включаем выходы (разрешение OE) */
    hc595_chain_output_enable(hc595_dev, true);

    LOG_INF("RTIO Completion Queue processor started.");

    while (1) {
        /* 
         * Поток спит без потребления ресурсов процессора, пока транзакция выполняется.
         * Как только DMA закончит отправку и сработает прерывание, rtio_cqe_consume 
         * мгновенно разблокирует этот поток.
         */
        struct rtio_cqe *cqe = rtio_cqe_consume(&spi_rtio);
        if (cqe != NULL) {
            if (cqe->result == 0) {
                /* Извлекаем пакет примененного состояния из контекста userdata */
                struct hc595_channels_msg *applied_msg = (struct hc595_channels_msg *)cqe->userdata;

                /* 
                 * Фиксируем факт успешной записи в системный канал Zbus.
                 * Используем K_NO_WAIT, так как находимся в контексте асинхронного обработчика [2].
                 */
                zbus_chan_pub(&sys_applied_chan, applied_msg, K_NO_WAIT);
                LOG_INF("SPI transfer finished, system applied state updated.");
            } else {
                LOG_ERR("Asynchronous SPI write failed with code: %d", cqe->result);
            }

            /* Возвращаем обработанный элемент в пул очереди */
            rtio_cqe_release(&spi_rtio, cqe);
        }
    }
}

/* Регистрируем поток обработки прерываний RTIO */
K_THREAD_DEFINE(cqe_proc_thread_id, 1024, cqe_processor_thread, NULL, NULL, NULL,
                K_PRIO_PREEMPT(7), 0, 0);
