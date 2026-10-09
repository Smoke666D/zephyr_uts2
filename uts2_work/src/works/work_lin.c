#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include "app_worker.h"
#include "system_bus_model.h"
#include <zephyr/zbus/zbus.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/iterable_sections.h>

LOG_MODULE_REGISTER(uart_4ch_driver, LOG_LEVEL_INF);

/* Размер очередей сообщений (в байтах) для каждого канала */
#define UART_Q_SIZE 128

/* Структура контекста для каждого из 4 UART каналов */
struct uart_channel_context {
    const struct device *dev;
    const char *name;
    
    /* Очередь на передачу (TX) */
    struct k_msgq tx_msgq;
    uint8_t tx_q_buffer[UART_Q_SIZE];

    /* Очередь на прием (RX) */
    struct k_msgq rx_msgq;
    uint8_t rx_q_buffer[UART_Q_SIZE];
};

/* Инициализация структуры для 4 каналов под твои ноды DTS */
static struct uart_channel_context uart_contexts[] = {
    { .dev = DEVICE_DT_GET(DT_NODELABEL(uart8)), .name = "UART8 (LIN1)" },
    { .dev = DEVICE_DT_GET(DT_NODELABEL(uart9)), .name = "UART9 (LIN2)" },
    { .dev = DEVICE_DT_GET(DT_NODELABEL(uart7)), .name = "UART7 (LIN3)" },
    { .dev = DEVICE_DT_GET(DT_NODELABEL(uart5)), .name = "UART5 (LIN4)" },
};

typedef enum 
{
    LIN1 = 0x01,
    LIN2 = 0x02,
    LIN3 = 0x03,
    LIN4 = 0x04,
} LIN_SYSTEM_BUS_ID;

typedef struct
{
   uint32_t data[8];
} lin_rx_ch_t;

static struct k_work uart_rx_task; 

static void _lin_tx_listener_cb(const struct zbus_channel *_chan);

ZBUS_LISTENER_DEFINE(lin_tx_listener, _lin_tx_listener_cb);

ZBUS_CHAN_DEFINE(lin1_tx_chan,
                 uint8_t,
                 NULL, 
                 (void*)LIN1, 
                 ZBUS_OBSERVERS(lin_tx_listener), 
                 ZBUS_MSG_INIT(0) 
);

ZBUS_CHAN_DEFINE(lin2_tx_chan,
                 uint8_t,
                 NULL, 
                 (void*)LIN2, 
                 ZBUS_OBSERVERS(lin_tx_listener), 
                 ZBUS_MSG_INIT(0) 
);

ZBUS_CHAN_DEFINE(lin3_tx_chan,
                 uint8_t,
                 NULL, 
                 (void*)LIN3, 
                 ZBUS_OBSERVERS(lin_tx_listener), 
                 ZBUS_MSG_INIT(0) 
);

ZBUS_CHAN_DEFINE(lin4_tx_chan,
                 uint8_t,
                 NULL, 
                 (void*)LIN4, 
                 ZBUS_OBSERVERS(lin_tx_listener), 
                 ZBUS_MSG_INIT(0) 
);

ZBUS_CHAN_DEFINE(lin1_rx_chan,
                 lin_rx_ch_t,
                 NULL, 
                 (void*)LIN1, 
                 ZBUS_OBSERVERS(), 
                 ZBUS_MSG_INIT(0) 
);

ZBUS_CHAN_DEFINE(lin2_rx_chan,
                 lin_rx_ch_t,
                 NULL, 
                 (void*)LIN2, 
                 ZBUS_OBSERVERS(), 
                 ZBUS_MSG_INIT(0) 
);

ZBUS_CHAN_DEFINE(lin3_rx_chan,
                lin_rx_ch_t,
                 NULL, 
                 (void*)LIN3, 
                 ZBUS_OBSERVERS(), 
                 ZBUS_MSG_INIT(0) 
);

ZBUS_CHAN_DEFINE(lin4_rx_chan,
                 lin_rx_ch_t,
                 NULL, 
                 (void*)LIN4, 
                 ZBUS_OBSERVERS(), 
                 ZBUS_MSG_INIT(0) 
);

/* Zbus Листенер: срабатывает при публикации в любой из CAN каналов */
static void _lin_tx_listener_cb(const struct zbus_channel *_chan)
{
    LIN_SYSTEM_BUS_ID bus_id = (LIN_SYSTEM_BUS_ID)zbus_chan_user_data(_chan); 
    struct uart_channel_context *ctx = &uart_contexts[bus_id - 1];

    const uint8_t *msg =  (uint8_t *)zbus_chan_msg(_chan);
    if (k_msgq_num_used_get(&ctx->tx_msgq) == 0)
    {
        uart_fifo_fill(ctx->dev, msg, 1);
        uart_irq_tx_enable(ctx->dev);
    }
    else 
    {
      k_msgq_put(&ctx->tx_msgq, msg, K_NO_WAIT);
    }  
}



#define NUM_UART_CHANNELS ARRAY_SIZE(uart_contexts)

/* 
 * Общий обработчик прерываний (ISR) для UART 
 * Работает в контексте аппаратного прерывания (минимальное время выполнения)
 */
static void uart_isr_callback(const struct device *dev, void *user_data)
{
    struct uart_channel_context *ctx = (struct uart_channel_context *)user_data;

    uart_irq_update(dev);

    /* --- ПЕРЕДАЧА (TX): железо готово принять следующий байт --- */
    if (uart_irq_tx_ready(dev)) {
        uint8_t byte_to_send;

        if (k_msgq_get(&ctx->tx_msgq, &byte_to_send, K_NO_WAIT) == 0) {
            // Заливаем байт в аппаратный FIFO передатчика
            uart_fifo_fill(dev, &byte_to_send, 1);
        } else {
            // Очередь пуста — отключаем прерывание TX, чтобы не грузить CPU
            uart_irq_tx_disable(dev);
        }
    }

    /* --- ПРИЕМ (RX): в аппаратном FIFO появились принятые байты --- */
    if (uart_irq_rx_ready(dev))
     {
        uint8_t received_byte;

        int read_bytes = uart_fifo_read(dev, &received_byte, 1);
        if (read_bytes > 0) {
            // Кладываем принятый байт в RX-очередь (безопасно для ISR благодаря K_NO_WAIT)
            k_msgq_put(&ctx->rx_msgq, &received_byte, K_NO_WAIT);
        }
        app_worker_submit(&uart_rx_task, REAL_TIME_WORKER);
    }
}

/**
 * @brief Функция отправки массива байт в выбранный UART канал
 * @param channel_idx Индекс канала от 0 до 3
 * @param data Указатель на данные
 * @param size Размер данных
 */
int uart_channel_send(int channel_idx, const uint8_t *data, size_t size)
{
    if (channel_idx < 0 || channel_idx >= NUM_UART_CHANNELS) {
        return -EINVAL;
    }

    struct uart_channel_context *ctx = &uart_contexts[channel_idx];

    for (size_t i = 0; i < size; i++) {
        // Побайтово пушим в программную очередь TX (с таймаутом, если очередь забита)
        while (k_msgq_put(&ctx->tx_msgq, &data[i], K_MSEC(10)) != 0) {
            // Ждем освобождения места в очереди
        }
    }

    // Принудительно разрешаем прерывание TX, чтобы железо начало отправку
    uart_irq_tx_enable(ctx->dev);
    return 0;
}

/**
 * @brief Функция чтения байта из RX-очереди выбранного канала (неблокирующая / с таймаутом)
 */
int uart_channel_receive_byte(int channel_idx, uint8_t *byte, k_timeout_t timeout)
{
    if (channel_idx < 0 || channel_idx >= NUM_UART_CHANNELS) {
        return -EINVAL;
    }

    struct uart_channel_context *ctx = &uart_contexts[channel_idx];
    return k_msgq_get(&ctx->rx_msgq, byte, timeout);
}






static void _uart_rx_handler(struct k_work *_work)
{
    uint8_t _data;
    lin_rx_ch_t send;
    
    while (k_msgq_get(&uart_contexts[0].rx_msgq, &_data, K_NO_WAIT) == 0) 
    {         
        send.data[0] = _data;
        zbus_chan_pub(&lin1_rx_chan, &send, K_NO_WAIT);
    }
    while (k_msgq_get(&uart_contexts[1].rx_msgq, &_data, K_NO_WAIT) == 0) 
    {         
        send.data[0] = _data;
        zbus_chan_pub(&lin2_rx_chan, &send, K_NO_WAIT);
    }
    while (k_msgq_get(&uart_contexts[2].rx_msgq, &_data, K_NO_WAIT) == 0) 
    {         
        send.data[0] = _data;
        zbus_chan_pub(&lin3_rx_chan, &send, K_NO_WAIT);
    }
    while (k_msgq_get(&uart_contexts[3].rx_msgq, &_data, K_NO_WAIT) == 0) 
    {         
        send.data[0] = _data;
        zbus_chan_pub(&lin4_rx_chan, &send, K_NO_WAIT);
    }
}



/**
 * @brief Общая инициализация подсистемы UART и пина SLP (PC3)
 */
static int uart_subsystem_init(void)
{
    int ret;

    /* 1. Инициализация общего пина SLP (PC3) в HIGH (выводим трансиверы из сна) */
    const struct device *gpioc_dev = DEVICE_DT_GET(DT_NODELABEL(gpioc));
    if (!device_is_ready(gpioc_dev)) {
        LOG_ERR("GPIOC device not ready!");
        return -ENODEV;
    }

    ret = gpio_pin_configure(gpioc_dev, 3, GPIO_OUTPUT_ACTIVE);
    if (ret < 0) {
        LOG_ERR("Failed to configure PC3 (SLP) pin, err: %d", ret);
        return ret;
    }
    LOG_INF("All transceivers awakened via shared SLP pin PC3 (HIGH)");

    /* 2. Инициализация всех 4 UART каналов */
    for (int i = 0; i < NUM_UART_CHANNELS; i++) {
        struct uart_channel_context *ctx = &uart_contexts[i];

        if (!device_is_ready(ctx->dev)) {
            LOG_ERR("UART device %s is not ready!", ctx->name);
            continue;
        }

        /* Инициализируем очереди k_msgq */
        k_msgq_init(&ctx->tx_msgq, ctx->tx_q_buffer, sizeof(uint8_t), UART_Q_SIZE);
        k_msgq_init(&ctx->rx_msgq, ctx->rx_q_buffer, sizeof(uint8_t), UART_Q_SIZE);

        /* Конфигурируем параметры UART (19200 бод, 8n1) */
        struct uart_config uart_cfg = {
            .baudrate = 19200,
            .parity = UART_CFG_PARITY_NONE,
            .stop_bits = UART_CFG_STOP_BITS_1,
            .data_bits = UART_CFG_DATA_BITS_8,
            .flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
        };

        ret = uart_configure(ctx->dev, &uart_cfg);
        if (ret < 0) {
            LOG_ERR("Failed to configure %s (err %d)", ctx->name, ret);
            continue;
        }

        /* Регистрируем прерывания и callback */
        uart_irq_callback_user_data_set(ctx->dev, uart_isr_callback, (void *)ctx);

        /* Включаем прерывание по приему (RX), чтобы слушать шину в фоне */
        uart_irq_rx_enable(ctx->dev);

        LOG_INF("UART channel %s initialized with TX/RX queues and interrupts.", ctx->name);
    }

    k_work_init(&uart_rx_task, _uart_rx_handler);

    return 0;
}

SYS_INIT(uart_subsystem_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

PARAM_ROUTE_DEFINE(LIN1_TX, &lin1_tx_chan, 0, ARRAY_DATA);
PARAM_ROUTE_DEFINE(LIN2_TX, &lin2_tx_chan, 0, ARRAY_DATA);
PARAM_ROUTE_DEFINE(LIN3_TX, &lin3_tx_chan, 0, ARRAY_DATA);
PARAM_ROUTE_DEFINE(LIN4_TX, &lin4_tx_chan, 0, ARRAY_DATA);
PARAM_ROUTE_DEFINE(LIN1_RX, &lin1_rx_chan, 0, ARRAY_DATA);
PARAM_ROUTE_DEFINE(LIN2_RX, &lin2_rx_chan, 0, ARRAY_DATA);
PARAM_ROUTE_DEFINE(LIN3_RX, &lin3_rx_chan, 0, ARRAY_DATA);
PARAM_ROUTE_DEFINE(LIN4_RX, &lin4_rx_chan, 0, ARRAY_DATA);
