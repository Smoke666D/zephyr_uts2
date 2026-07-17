#ifndef ZEPHYR_DRIVERS_SEQ_MUX_ADC_H_
#define ZEPHYR_DRIVERS_SEQ_MUX_ADC_H_

#include <zephyr/device.h>

/* Описание публичного API нашего драйвера секвенсора АЦП */
struct seq_mux_adc_api {
    /* Получить сырые данные АЦП для определенного шага и канала */
    uint32_t (*get_raw_value)(const struct device *dev, uint8_t step, uint8_t channel);
    
    /* Получить вычисленное напряжение питания VDDA в милливольтах */
    uint32_t (*get_vdda_mv)(const struct device *dev, uint8_t step);
    
    /* Получить вычисленную температуру ядра кристалла в градусах Цельсия */
    int32_t (*get_core_temp)(const struct device *dev, uint8_t step);
};

#endif /* ZEPHYR_DRIVERS_SEQ_MUX_ADC_H_ */