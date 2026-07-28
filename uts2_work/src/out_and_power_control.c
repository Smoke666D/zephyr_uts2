#include <stdint.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>
#include "system_data_bus.h"
#include <hc595_chain.h>
#include "global_params.h"
#include "out_and_power_control.h"

LOG_MODULE_REGISTER(out_and_power, LOG_LEVEL_INF);



/* 
 * 1. Выделяем стек потока и принудительно переносим его в быструю память DTCM [1.1.4, 2.3.1].
 * Используем __dtcm_bss_section, так как неинициализированный стек относится к сегменту BSS [1.1.4].
 */
static uint8_t __attribute__((section("DTCM"), aligned(32))) 
    hc595_dispatcher_stack[DISPATCHER_STACK_SIZE];


/* 
 * 2. Управляющую структуру потока оставляем в стандартной RAM (SRAM) [1.1.4].
 * Это гарантирует отсутствие конфликтов с защитой ядра и MPU [1.1.4].
 */
static struct k_thread hc595_dispatcher_thread_data;

/* 1. Объявляем подписчика Zbus для управления выходами */
ZBUS_SUBSCRIBER_DEFINE(hc595_sub, 8);

/* 2. Объявляем канал Zbus для управления выходами */
ZBUS_CHAN_DEFINE(hc595_chan,
                 struct hc595_channels_msg,
                 NULL,
                 NULL,
                 ZBUS_OBSERVERS(hc595_sub),
                 {0}
);


/**
 * @brief Функция расчета маски для сдвигового регистра.
 */
static void hc595_calculate_mask(const struct hc595_channels_msg *msg, uint8_t *tx_data, size_t len)
{
    if (len < 5) {
        return;
    }

    memset(tx_data, 0, len);

    /* Заполняем управляющие сигналы LIN (старшая тетрада первого байта) */
    tx_data[0] = ((uint8_t)msg->lin_pb[0] << 4) |
                 ((uint8_t)msg->lin_pb[1] << 5) |
                 ((uint8_t)msg->lin_pb[2] << 6) |
                 ((uint8_t)msg->lin_pb[3] << 7);

    static const uint8_t state_to_bits[] = {0, 2, 1};

    /* Заполняем каналы слаботочных драйверов */
    for (int i = 0; i < LOW_CUR_DRIVER_COUNT; i++) {
        uint8_t chip_index = 4 - (i / 4);
        uint8_t bit_indx = (i % 4) * 2;
        LOW_CUR_DRIVER_STATE state = msg->low_cur_driver_channels[i];

        if (state < 3) {
            tx_data[chip_index] |= (state_to_bits[state] << bit_indx);
        }
    }
}


/**
 * @brief Потокобезопасная атомарная установка состояния силового ключа (Zero-Copy)
 */
int out_and_power_set_channel(uint8_t channel_idx, LOW_CUR_DRIVER_STATE state)
{
    if (channel_idx >= LOW_CUR_DRIVER_COUNT) {
        return -EINVAL;
    }

    /* 1. Блокируем внутренний семафор (мьютекс) канала в Zbus */
    int err = zbus_chan_claim(&hc595_chan, K_FOREVER);
    if (err == 0) {
        /* 2. Получаем прямой указатель на структуру сообщения в памяти Zbus */
        struct hc595_channels_msg *msg = (struct hc595_channels_msg *)zbus_chan_msg(&hc595_chan);
        
        /* 3. Модифицируем нужное поле прямо на месте (Zero-Copy) */
        msg->low_cur_driver_channels[channel_idx] = state;
        
        /* 4. Принудительно запускаем рассылку уведомлений подписчикам */
        zbus_chan_notify(&hc595_chan, K_MSEC(10));
        
        /* 5. Освобождаем встроенный семафор канала */
        zbus_chan_finish(&hc595_chan);
    }

    return err;
}

/**
 * @brief Потокобезопасная атомарная установка состояния линии LIN (Zero-Copy)
 */
int out_and_power_set_lin(uint8_t lin_idx, bool active)
{
    if (lin_idx >= LIN_COUNT) {
        return -EINVAL;
    }

    /* 1. Блокируем внутренний семафор канала в Zbus */
    int err = zbus_chan_claim(&hc595_chan, K_FOREVER);
    if (err == 0) {
        /* 2. Получаем прямой указатель на структуру сообщения в Zbus */
        struct hc595_channels_msg *msg = (struct hc595_channels_msg *)zbus_chan_msg(&hc595_chan);
        
        /* 3. Модифицируем линию LIN прямо на месте */
        msg->lin_pb[lin_idx] = active;
        
        /* 4. Принудительно запускаем рассылку уведомлений подписчикам */
        zbus_chan_notify(&hc595_chan, K_MSEC(10));
        
        /* 5. Освобождаем встроенный семафор канала */
        zbus_chan_finish(&hc595_chan);
    }

    return err;
}

/* Функция потока диспетчера (коллбэк) */
static void hc595_dispatcher_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    const struct device *hc595_dev = DEVICE_DT_GET(DT_NODELABEL(hc595_chain));

    if (!device_is_ready(hc595_dev)) {
        LOG_ERR("74HC595 device not ready in dispatcher thread");
        return;
    }

    /* На старте выходы аппаратно выключены */
    hc595_chain_output_enable(hc595_dev, false);

    struct hc595_channels_msg current_msg = {0};  /* Желаемое состояние из Zbus */
    uint8_t last_tx_data[5] = {0};
    bool first_run = true;
    const struct zbus_channel *chan;

    while (1) {
        /* Поток засыпает и ждет публикации в канале [2, 3] */
        int err = zbus_sub_wait(&hc595_sub, &chan, K_FOREVER);
        if (err != 0) {
            continue;
        }

        if (chan == &hc595_chan) {
            err = zbus_chan_read(&hc595_chan, &current_msg, K_NO_WAIT);
            if (err != 0) {
                continue;
            }

            uint8_t tx_data[5] = {0};

            hc595_calculate_mask(&current_msg, tx_data, sizeof(tx_data));

            /* Отправляем по SPI только при изменении битовой маски */
            if (first_run || memcmp(tx_data, last_tx_data, sizeof(tx_data)) != 0) {
                err = hc595_chain_write(hc595_dev, tx_data, sizeof(tx_data));
                if (err) {
                    LOG_ERR("Failed to write registers: %d", err);
                } else {
                    memcpy(last_tx_data, tx_data, sizeof(tx_data));
                    
                    if (first_run) {
                        /* После первой успешной инициализации включаем выходы */
                        hc595_chain_output_enable(hc595_dev, true);
                        first_run = false;
                    }
                }
            }
        }
    }
}


static int _power_drv_param_set(PARAM_ID id, const PARAM_VAL *val)
{
    // 1. Проверяем диапазон параметров управления силовыми ключами (1 - 18)
    if (id >= POWER_DRV_CTR_CHANNEL1 && id <= POWER_DRV_CTR_CHANNEL18)
    {
        uint32_t channel_idx = id - POWER_DRV_CTR_CHANNEL1;
        return out_and_power_set_channel(channel_idx, (LOW_CUR_DRIVER_STATE)val->value.integer);
    }
    // 2. Проверяем диапазон параметров управления линиями LIN (1 - 4)
    else if (id >= LIN1_PD && id <= LIN4_PD)
    {
        uint32_t lin_idx = id - LIN1_PD;
        return out_and_power_set_lin(lin_idx, val->value.boolean);
    }

    return -ENOTSUP;
}



/**
 * @brief Функция ручной инициализации и запуска потока управления
 */
int out_and_power_control_init(void)
{
    /* Создаем и запускаем поток на вытесняющем приоритете 8 [2.3.1] */
    k_thread_create(&hc595_dispatcher_thread_data,
                    (k_thread_stack_t *)hc595_dispatcher_stack,
                    DISPATCHER_STACK_SIZE,
                    hc595_dispatcher_thread,
                    NULL, NULL, NULL,
                    K_PRIO_PREEMPT(8), 0, K_NO_WAIT);
                    
    LOG_INF("HC595 Dispatcher thread initialized (Stack in DTCM).");
    return 0;
}

