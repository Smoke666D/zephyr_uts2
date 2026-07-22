#ifndef DRIVERS_HC595_CHAIN_H_
#define DRIVERS_HC595_CHAIN_H_

#include <zephyr/device.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

__subsystem struct hc595_chain_api {
    int (*write)(const struct device *dev, const uint8_t *data, size_t len);
    int (*output_enable)(const struct device *dev, bool enable);
};

/**
 * @brief Отправить данные в цепочку 74HC595.
 */
static inline int hc595_chain_write(const struct device *dev, const uint8_t *data, size_t len)
{
    const struct hc595_chain_api *api = (const struct hc595_chain_api *)dev->api;
    return api->write(dev, data, len);
}

/**
 * @brief Включить или выключить физические выходы цепочки.
 * @param dev Указатель на устройство.
 * @param enable true для включения выходов, false — для перевода в Hi-Z.
 */
static inline int hc595_chain_output_enable(const struct device *dev, bool enable)
{
    const struct hc595_chain_api *api = (const struct hc595_chain_api *)dev->api;
    if (!api->output_enable) {
        return -ENOTSUP;
    }
    return api->output_enable(dev, enable);
}

#ifdef __cplusplus
}
#endif

#endif /* DRIVERS_HC595_CHAIN_H_ */