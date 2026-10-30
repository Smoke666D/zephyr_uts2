

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/drivers/uart.h>
#include "system_bus_model.h"

LOG_MODULE_REGISTER(uart_detect, LOG_LEVEL_INF);
ZBUS_CHAN_DECLARE(uart_detect_cfg_chan); /* Канал настроек порта */
ZBUS_CHAN_DECLARE(uart_detect_rx_chan);  /* Канал побайтового приема */
#define RING_BUF_SIZE 512

/* Промежуточный буфер между ISR и k_work */
static struct ring_buf rx_rb;
static uint8_t rx_rb_buf[RING_BUF_SIZE];

static const struct device *const uart_dev = DEVICE_DT_GET(DT_NODELABEL(uart_detect));
static struct k_work rx_work;

/* ====================================================================
 * 1. Воркер приема: достает байты по одному и публикует uint32_t
 * ==================================================================== */
static void rx_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);
    uint8_t byte;

    /* Выгребаем кольцевой буфер и побайтно пушим в ZBUS */
    while (ring_buf_get(&rx_rb, &byte, 1) > 0) {
        uint32_t val = (uint32_t)byte;
        
        /* Таймаут K_NO_WAIT или небольшой таймаут */
        zbus_chan_pub(&uart_detect_rx_chan, &val, K_MSEC(10));
    }
}

/* Обработчик прерывания UART */
static void uart_detect_isr(const struct device *dev, void *user_data)
{
    ARG_UNUSED(user_data);

    while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
        if (uart_irq_rx_ready(dev)) {
            uint8_t temp_buf[16];
            int recv_len;

            /* Вычитываем аппаратный FIFO в промежуточный ring_buf */
            while ((recv_len = uart_fifo_read(dev, temp_buf, sizeof(temp_buf))) > 0) {
                ring_buf_put(&rx_rb, temp_buf, recv_len);
            }

            /* Запускаем воркер для публикации */
            k_work_submit(&rx_work);
        }
    }
}

/* ====================================================================
 * 2. Слушатель настроек: применяет uint32_t к регистрам USART2
 * ==================================================================== */
static void uart_detect_cfg_listener_cb(const struct zbus_channel *chan)
{
    const uint32_t *packed_cfg = zbus_chan_const_msg(chan);
    uint32_t cfg = *packed_cfg;

    struct uart_config ucfg = {
        .baudrate  = uart_detect_unpack_baud(cfg),
        .parity    = uart_detect_unpack_parity(cfg),
        .stop_bits = uart_detect_unpack_stop(cfg),
        .data_bits = uart_detect_unpack_data(cfg),
        .flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
    };

    LOG_INF("UART_DETECT: смена настроек (Baud: %u, Parity: %d, Stop: %d, Data: %d)",
            ucfg.baudrate, ucfg.parity, ucfg.stop_bits, ucfg.data_bits);

    uart_irq_rx_disable(uart_dev);
    
    int ret = uart_configure(uart_dev, &ucfg);
    if (ret != 0) {
        LOG_ERR("Ошибка конфигурирования UART: %d", ret);
        return;
    }

    uart_irq_rx_enable(uart_dev);
}

ZBUS_LISTENER_DEFINE(uart_detect_cfg_listener, uart_detect_cfg_listener_cb);

/* ====================================================================
 * 3. Объявление каналов Zbus (оба типа uint32_t)
 * ==================================================================== */

/* Канал конфигурации: слушатель uart_detect_cfg_listener привязан к нему */
ZBUS_CHAN_DEFINE(uart_detect_cfg_chan,
                 uint32_t,
                 NULL,
                 NULL,
                 ZBUS_OBSERVERS(uart_detect_cfg_listener),
                 ZBUS_MSG_INIT(0)
);

/* Приёмный канал: слушатели подключаются приложением */
ZBUS_CHAN_DEFINE(uart_detect_rx_chan,
                 uint32_t,
                 NULL,
                 NULL,
                 ZBUS_OBSERVERS_EMPTY,
                 ZBUS_MSG_INIT(0)
);

/* ====================================================================
 * 4. Инициализация (SYS_INIT)
 * ==================================================================== */
static int uart_detect_init(void)
{
    if (!device_is_ready(uart_dev)) {
        LOG_ERR("Устройство UART_DETECT не готово!");
        return -ENODEV;
    }

    ring_buf_init(&rx_rb, sizeof(rx_rb_buf), rx_rb_buf);
    k_work_init(&rx_work, rx_work_handler);

    uart_irq_callback_user_data_set(uart_dev, uart_detect_isr, NULL);

    /* 
     * Публикуем дефолтную конфигурацию: 115200 8N1.
     * Слушатель тут же перехватит её, вызовет uart_configure() и включит RX!
     */
    uint32_t default_cfg = uart_detect_pack_cfg(
        115200,
        UART_CFG_PARITY_NONE,
        UART_CFG_STOP_BITS_1,
        UART_CFG_DATA_BITS_8
    );

    LOG_INF("UART_DETECT init: публикация дефолтных настроек...");
    zbus_chan_pub(&uart_detect_cfg_chan, &default_cfg, K_FOREVER);

    return 0;
}

SYS_INIT(uart_detect_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);