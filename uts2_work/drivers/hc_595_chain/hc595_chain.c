#define DT_DRV_COMPAT custom_hc595_chain

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include "hc595_chain.h"

LOG_MODULE_REGISTER(hc595_chain, LOG_LEVEL_INF);

struct hc595_chain_config {
    struct spi_dt_spec spi;
    struct gpio_dt_spec oe_gpio;
    uint8_t chain_length;
};

struct hc595_chain_data {
    struct k_mutex lock;
};

static int hc595_chain_write_impl(const struct device *dev, const uint8_t *data, size_t len)
{
    const struct hc595_chain_config *config = dev->config;
    struct hc595_chain_data *drv_data = dev->data;
    int err;

    if (len != config->chain_length) {
        LOG_ERR("Data length (%d) does not match chain length (%d)", len, config->chain_length);
        return -EINVAL;
    }

    struct spi_buf tx_buf = {
        .buf = (void *)data,
        .len = len,
    };
    struct spi_buf_set tx_bufs = {
        .buffers = &tx_buf,
        .count = 1,
    };

    k_mutex_lock(&drv_data->lock, K_FOREVER);
    err = spi_write_dt(&config->spi, &tx_bufs);
    k_mutex_unlock(&drv_data->lock);

    if (err) {
        LOG_ERR("SPI write failed: %d", err);
        return err;
    }

    return 0;
}

static int hc595_chain_output_enable_impl(const struct device *dev, bool enable)
{
    const struct hc595_chain_config *config = dev->config;

    if (config->oe_gpio.port == NULL) {
        return -ENOTSUP;
    }

    /* Направление/полярность учитывается библиотекой GPIO автоматически.
     * enable = true  -> Логическая 1 -> Выставит физический 0 (Включено)
     * enable = false -> Логический 0 -> Выставит физическую 1 (Выключено, Hi-Z) */
    return gpio_pin_set_dt(&config->oe_gpio, enable ? 1 : 0);
}

static const struct hc595_chain_api hc595_api = {
    .write = hc595_chain_write_impl,
    .output_enable = hc595_chain_output_enable_impl,
};

static int hc595_chain_init(const struct device *dev)
{
    const struct hc595_chain_config *config = dev->config;
    struct hc595_chain_data *drv_data = dev->data;
    int err;

    k_mutex_init(&drv_data->lock);

    if (!spi_is_ready_dt(&config->spi)) {
        LOG_ERR("SPI device not ready");
        return -ENODEV;
    }

    /* Проверяем, задан ли пин OE в Devicetree */
    if (config->oe_gpio.port != NULL) {
        if (!gpio_is_ready_dt(&config->oe_gpio)) {
            LOG_ERR("OE GPIO device not ready");
            return -ENODEV;
        }

        /* Настраиваем пин как выход в неактивное логическое состояние.
         * Это гарантирует, что выходы 595 будут в Hi-Z до явной команды от приложения */
        err = gpio_pin_configure_dt(&config->oe_gpio, GPIO_OUTPUT_INACTIVE);
        if (err) {
            LOG_ERR("Failed to configure OE GPIO: %d", err);
            return err;
        }
    }

    LOG_INF("74HC595 Chain initialized. Length: %d", config->chain_length);
    return 0;
}

#define HC595_CHAIN_INIT(inst)                                              \
    static struct hc595_chain_data hc595_chain_data_##inst;                 \
                                                                            \
    static const struct hc595_chain_config hc595_chain_config_##inst = {    \
        .spi = SPI_DT_SPEC_INST_GET(inst,                                   \
                                    SPI_OP_MODE_MASTER |                    \
                                    SPI_WORD_SET(8) |                       \
                                    SPI_TRANSFER_MSB),                      \
        .oe_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, oe_gpios, {0}),           \
        .chain_length = DT_INST_PROP(inst, chain_length),                   \
    };                                                                      \
                                                                            \
    DEVICE_DT_INST_DEFINE(inst,                                             \
                          hc595_chain_init,                                 \
                          NULL,                                             \
                          &hc595_chain_data_##inst,                         \
                          &hc595_chain_config_##inst,                       \
                          POST_KERNEL,                                      \
                          CONFIG_APPLICATION_INIT_PRIORITY,                 \
                          &hc595_api);

DT_INST_FOREACH_STATUS_OKAY(HC595_CHAIN_INIT)