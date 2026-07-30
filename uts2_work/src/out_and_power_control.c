#include <stdint.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>      /* Для SYS_INIT */
#include <zephyr/rtio/rtio.h>
#include <zephyr/drivers/spi.h>
#include "system_data_bus.h"


LOG_MODULE_REGISTER(out_and_power, LOG_LEVEL_INF);

/* ------------------------------------------------------------------ */
/* 1. ОБЪЯВЛЕНИЕ РЕСУРСОВ И ПИНОВ                                     */
/* ------------------------------------------------------------------ */

/* Пин разрешения выходов OE из узла zephyr,user [1] */
static const struct gpio_dt_spec oe_gpio = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), oe_gpios);

/* Спецификация SPI для синхронной записи при старте системы [1.2.2] */
static const struct spi_dt_spec init_spi_spec = 
    SPI_DT_SPEC_GET(DT_NODELABEL(hc595_chain),
                    SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB);

/* ------------------------------------------------------------------ */
/* 2. ОБЪЯВЛЕНИЕ ПРИКЛАДНЫХ И СИСТЕМНЫХ КАНАЛОВ ZBUS                  */
/* ------------------------------------------------------------------ */

/* Прикладной канал команд */
ZBUS_CHAN_DEFINE(app_control_chan,
                 struct hc595_channels_msg,
                 NULL, NULL,
                 ZBUS_OBSERVERS_EMPTY,
                 {0}
);

/* Системный канал примененных состояний */
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
/* 3. НАСТРОЙКА ПОДСИСТЕМЫ RTIO ДЛЯ SPI                               */
/* ------------------------------------------------------------------ */

/* Размер очереди отправки 4, очереди завершения 4 (не используется из-за NO_RESPONSE) */
RTIO_DEFINE(spi_rtio, 4, 4);

SPI_DT_IODEV_DEFINE(hc595_iodev, 
                    DT_NODELABEL(hc595_chain), 
                    SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB, 
                    0);

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
/* 4. НАТИВНЫЙ ОБРАБОТЧИК ЗАВЕРШЕНИЯ ТРАНЗАКЦИИ (RTIO CALLBACK)       */
/* ------------------------------------------------------------------ */

/**
 * @brief Асинхронный коллбэк. Вызывается из ISR-контекста прерывания SPI [1.2.6]
 */
static void spi_completion_callback(struct rtio *r, const struct rtio_sqe *sqe, int result, void *arg0)
{
    ARG_UNUSED(r);
    ARG_UNUSED(sqe);

    if (result == 0) {
        struct hc595_channels_msg *applied_msg = (struct hc595_channels_msg *)arg0;

        /* Фиксируем отправку в системный Zbus. Используем K_NO_WAIT для безопасного вызова из ISR [2] */
        zbus_chan_pub(&sys_applied_chan, applied_msg, K_NO_WAIT);
    } else {
        /* В реальной системе здесь можно инкрементировать счетчик аппаратных сбоев SPI */
    }
}

/* ------------------------------------------------------------------ */
/* 5. ЛИСТЕНЕР ПРИКЛАДНОГО КАНАЛА УПРАВЛЕНИЯ                           */
/* ------------------------------------------------------------------ */

static void app_control_listener_fn(const struct zbus_channel *chan)
{
    if (chan == &app_control_chan) {
        struct hc595_channels_msg msg;

        int err = zbus_chan_read(&app_control_chan, &msg, K_NO_WAIT);
        if (err != 0) {
            return;
        }

        hc595_calculate_mask(&msg, spi_tx_buf, sizeof(spi_tx_buf));
        memcpy(&last_submitted_msg, &msg, sizeof(struct hc595_channels_msg));

        /* Запрашиваем два элемента из очереди для организации цепочки */
        struct rtio_sqe *sqe_write = rtio_sqe_acquire(&spi_rtio);
        struct rtio_sqe *sqe_callback = rtio_sqe_acquire(&spi_rtio);

        if (sqe_write != NULL && sqe_callback != NULL) {
            /* 1. Подготавливаем асинхронную отправку. 
             * Флаг CHAINED связывает эту операцию со следующей [1.2.4].
             * Флаг NO_RESPONSE отключает генерацию CQE, разгружая память линкера [1.2.3]. */
            rtio_sqe_prep_write(sqe_write, &hc595_iodev, RTIO_PRIO_NORM, 
                                spi_tx_buf, sizeof(spi_tx_buf), NULL);
            sqe_write->flags |= RTIO_SQE_CHAINED | RTIO_SQE_NO_RESPONSE;

            /* 2. Подготавливаем коллбэк завершения, передавая адрес сообщения */
            rtio_sqe_prep_callback(sqe_callback, spi_completion_callback, 
                                   (void *)&last_submitted_msg, NULL);
            sqe_callback->flags |= RTIO_SQE_NO_RESPONSE;

            /* Отправляем цепочку из 2-х задач на исполнение */
            rtio_submit(&spi_rtio, 2);
        } else {
            LOG_ERR("RTIO queue overflow! Commands dropped.");
        }
    }
}

ZBUS_LISTENER_DEFINE(app_control_lis, app_control_listener_fn);

/* ------------------------------------------------------------------ */
/* 6. ФУНКЦИЯ АВТОИНИЦИАЛИЗАЦИИ И СБРОСА ПРИ СТАРТЕ                   */
/* ------------------------------------------------------------------ */

static int out_and_power_init(void)
{
    int err;

    /* 1. Инициализируем пин Output Enable */
    if (!gpio_is_ready_dt(&oe_gpio)) {
        LOG_ERR("OE GPIO device is not ready!");
        return -ENODEV;
    }

    /* Аппаратно отключаем выходы (перевод в Hi-Z), чтобы избежать бросков */
    err = gpio_pin_configure_dt(&oe_gpio, GPIO_OUTPUT_INACTIVE);
    if (err) {
        LOG_ERR("Failed to configure OE pin: %d", err);
        return err;
    }

    /* 2. Инициализируем аппаратную шину SPI */
    if (!spi_is_ready_dt(&init_spi_spec)) {
        LOG_ERR("SPI device not ready during system init!");
        return -ENODEV;
    }

    /* 3. Готовим безопасную стартовую маску (все нули) */
    uint8_t init_tx[5] = {0};
    struct spi_buf tx_buf = { .buf = init_tx, .len = sizeof(init_tx) };
    struct spi_buf_set tx_bufs = { .buffers = &tx_buf, .count = 1 };

    /* Синхронная (блокирующая) отправка нулей при старте системы [1.2.2] */
    err = spi_write_dt(&init_spi_spec, &tx_bufs);
    if (err) {
        LOG_ERR("Failed to write initial safe state: %d", err);
        return err;
    }

    /* 4. Физически разрешаем работу выходов (прижимаем OE к земле) [1] */
    err = gpio_pin_set_dt(&oe_gpio, 1);
    if (err) {
        LOG_ERR("Failed to enable OE: %d", err);
        return err;
    }

    LOG_INF("Out & Power module successfully initialized (Outputs enabled with safe state).");
    return 0;
}

/* 
 * Автозапуск на этапе POST_KERNEL.
 * Приоритет 85 гарантирует, что модуль запустится строго ПОСЛЕ инициализации 
 * драйверов SPI и GPIO (приоритеты 70-80), но ДО старта прикладных потоков (90) [1.1.1, 1.1.3].
 */
SYS_INIT(out_and_power_init, POST_KERNEL, 85);
