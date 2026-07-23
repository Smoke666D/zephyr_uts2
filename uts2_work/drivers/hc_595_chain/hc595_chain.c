#define DT_DRV_COMPAT custom_hc595_chain

/**
 *  @file       hc595_chain.c
 *  @headerfile hc595_chain.h
 *
 *  @date       2026.07.23
 *  @author     Dymov Igor
 *
 *  @brief      Драйвер каскада сдвиговых регистров 74HC595
 *  @details    Реализует передачу данных по интерфейсу SPI для каскада регистров
 *              и управление выводом разрешения выхода (OE) через GPIO.
 */

/***************************************************************************************************
 *                                          INCLUDED FILES
 **************************************************************************************************/
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "hc595_chain.h"

/***************************************************************************************************
 *                                           DEFINITIONS
 **************************************************************************************************/
LOG_MODULE_REGISTER(hc595_chain, LOG_LEVEL_INF);

/***************************************************************************************************
 *                                          PRIVATE TYPES
 **************************************************************************************************/
typedef struct hc595_chain_config
{
    struct spi_dt_spec  spi;
    struct gpio_dt_spec oe_gpio;
    uint8_t             chain_length;
} hc595_chain_config_t;

typedef struct hc595_chain_data
{
    struct k_mutex lock;
} hc595_chain_data_t;

/***************************************************************************************************
 *                                   PRIVATE FUNCTION PROTOTYPES
 **************************************************************************************************/
static int _hc595_chain_write_impl(const struct device *_dev, 
                                   const uint8_t *_data, 
                                   size_t _len);
static int _hc595_chain_output_enable_impl(const struct device *_dev, 
                                           bool _enable);
static int _hc595_chain_init(const struct device *_dev);

/***************************************************************************************************
 *                                        PRIVATE FUNCTIONS
 **************************************************************************************************/

/**
 *  @brief      Запись данных в цепочку сдвиговых регистров
 *  @details    Проверяет соответствие длины данных длине цепочки, блокирует 
 *              интерфейс с помощью мьютекса и осуществляет запись через SPI.
 *
 *  @param      _dev  - Указатель на устройство драйвера
 *  @param      _data - Указатель на буфер с данными для отправки
 *  @param      _len  - Длина отправляемых данных в байтах
 *
 *  @return     int - Ноль при успехе, отрицательный код ошибки при сбое
 */
static int _hc595_chain_write_impl(const struct device *_dev, 
                                   const uint8_t *_data, 
                                   size_t _len)
{
    const struct hc595_chain_config *config = _dev->config;
    struct hc595_chain_data *drv_data = _dev->data;
    int err;

    if (_len != config->chain_length) 
    {
        LOG_ERR("Длина данных (%zu) не совпадает с длиной цепочки (%u)", 
                _len, config->chain_length);
        return -EINVAL;
    }

    struct spi_buf tx_buf = 
    {
        .buf = (void *)_data,
        .len = _len,
    };

    struct spi_buf_set tx_bufs = 
    {
        .buffers = &tx_buf,
        .count   = 1,
    };

    k_mutex_lock(&drv_data->lock, K_FOREVER);
    err = spi_write_dt(&config->spi, &tx_bufs);
    k_mutex_unlock(&drv_data->lock);

    if (err) 
    {
        LOG_ERR("Сбой записи SPI: %d", err);
        return err;
    }

    return 0;
}

/**
 *  @brief      Управление разрешением выходов сдвиговых регистров
 *  @details    Устанавливает логический уровень на выводе OE (Output Enable), 
 *              если он настроен в дереве устройств.
 *
 *  @param      _dev    - Указатель на устройство драйвера
 *  @param      _enable - Флаг включения выходов (true - включены, false - Hi-Z)
 *
 *  @return     int - Ноль при успехе, отрицательный код ошибки при сбое
 */
static int _hc595_chain_output_enable_impl(const struct device *_dev, 
                                           bool _enable)
{
    const struct hc595_chain_config *config = _dev->config;

    if (config->oe_gpio.port == NULL) 
    {
        return -ENOTSUP;
    }

    // Направление и полярность учитываются библиотекой GPIO автоматически
    // Значение true выставит физический 0 (включено), false — физическую 1 (Hi-Z)
    return gpio_pin_set_dt(&config->oe_gpio, _enable ? 1 : 0);
}

/**
 *  @brief      Инициализация драйвера каскада сдвиговых регистров
 *  @details    Инициализирует мьютекс, проверяет готовность SPI и настраивает 
 *              вывод OE (если задан) в неактивное состояние (Hi-Z).
 *
 *  @param      _dev - Указатель на устройство драйвера
 *
 *  @return     int - Ноль при успехе, отрицательный код ошибки при сбое
 */
static int _hc595_chain_init(const struct device *_dev)
{
    const struct hc595_chain_config *config = _dev->config;
    struct hc595_chain_data *drv_data = _dev->data;
    int err;

    k_mutex_init(&drv_data->lock);

    if (!spi_is_ready_dt(&config->spi)) 
    {
        LOG_ERR("Устройство SPI не готово");
        return -ENODEV;
    }

    // Проверка наличия вывода OE в дереве устройств
    if (config->oe_gpio.port != NULL) 
    {
        if (!gpio_is_ready_dt(&config->oe_gpio)) 
        {
            LOG_ERR("Устройство GPIO для OE не готово");
            return -ENODEV;
        }

        // Настройка вывода как выход в неактивное логическое состояние (Hi-Z)
        err = gpio_pin_configure_dt(&config->oe_gpio, GPIO_OUTPUT_INACTIVE);
        if (err) 
        {
            LOG_ERR("Не удалось настроить GPIO для OE: %d", err);
            return err;
        }
    }

    LOG_INF("Цепочка 74HC595 инициализирована. Длина: %d", 
            config->chain_length);
    return 0;
}

/***************************************************************************************************
 *                                        PUBLIC FUNCTIONS
 **************************************************************************************************/

static const struct hc595_chain_api hc595_api = 
{
    .write         = _hc595_chain_write_impl,
    .output_enable = _hc595_chain_output_enable_impl,
};

#define HC595_CHAIN_INIT(inst)                                             \
    static struct hc595_chain_data hc595_chain_data_##inst;                \
                                                                           \
    static const struct hc595_chain_config hc595_chain_config_##inst =     \
    {                                                                      \
        .spi          = SPI_DT_SPEC_INST_GET(inst,                         \
                                             SPI_OP_MODE_MASTER |          \
                                             SPI_WORD_SET(8)    |          \
                                             SPI_TRANSFER_MSB),            \
        .oe_gpio      = GPIO_DT_SPEC_INST_GET_OR(inst, oe_gpios, {0}),     \
        .chain_length = DT_INST_PROP(inst, chain_length),                  \
    };                                                                     \
                                                                           \
    DEVICE_DT_INST_DEFINE(inst,                                            \
                          _hc595_chain_init,                               \
                          NULL,                                            \
                          &hc595_chain_data_##inst,                        \
                          &hc595_chain_config_##inst,                      \
                          POST_KERNEL,                                     \
                          CONFIG_APPLICATION_INIT_PRIORITY,                \
                          &hc595_api);

DT_INST_FOREACH_STATUS_OKAY(HC595_CHAIN_INIT)

/***************************************************************************************************
 *                                           END OF FILE
 **************************************************************************************************/