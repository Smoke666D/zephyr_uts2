#ifndef ZEPHYR_DRIVERS_SEQ_MUX_ADC_H_
#define ZEPHYR_DRIVERS_SEQ_MUX_ADC_H_

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>  
#include <stm32_ll_gpio.h>
#include <stm32_ll_bus.h>

#define SEQ_NODE DT_NODELABEL(my_sequencer)
#define SEQ_GPIO_PORT_NODE DT_GPIO_CTLR_BY_IDX(SEQ_NODE, mux_gpios, 0)

/* Извлекаем базовые адреса портов для каждого из 3 управляющих пинов */
#define SEQ_NODE DT_NODELABEL(my_sequencer)

#define MUX_PIN_0_PORT_BASE DT_REG_ADDR(DT_GPIO_CTLR_BY_IDX(SEQ_NODE, mux_gpios, 0))
#define MUX_PIN_0_NUM       DT_GPIO_PIN_BY_IDX(SEQ_NODE, mux_gpios, 0)

#define MUX_PIN_1_PORT_BASE DT_REG_ADDR(DT_GPIO_CTLR_BY_IDX(SEQ_NODE, mux_gpios, 1))
#define MUX_PIN_1_NUM       DT_GPIO_PIN_BY_IDX(SEQ_NODE, mux_gpios, 1)

#define MUX_PIN_2_PORT_BASE DT_REG_ADDR(DT_GPIO_CTLR_BY_IDX(SEQ_NODE, mux_gpios, 2))
#define MUX_PIN_2_NUM       DT_GPIO_PIN_BY_IDX(SEQ_NODE, mux_gpios, 2)

#define SEQ_GPIO_BSRR_ADDR (MUX_PIN_0_PORT_BASE + 0x18)

#define COMBINATIONS_COUNT 8
#define ADC_CHANNELS_COUNT 2

#define TOTAL_CHANNELS_COUNT (COMBINATIONS_COUNT * ADC_CHANNELS_COUNT)

/* Извлекаем временные параметры из DTS */
#define SEQ_ADC_DURATION_US    DT_PROP(SEQ_NODE, adc_duration_us)
#define SEQ_SWITCH_DURATION_US DT_PROP(SEQ_NODE, switch_duration_us)

/* ================== ДИНАМИЧЕСКИЙ РАСЧЕТ ИЗ DTS ================== */
/* Извлекаем узлы, указанные в свойствах adc-dev и timer-dev */
#define SEQ_ADC_NODE  DT_PHANDLE(DT_NODELABEL(my_sequencer), adc_dev)
#define SEQ_TIM_NODE  DT_PHANDLE(DT_NODELABEL(my_sequencer), timer_dev)

/* Вычисляем физические адреса регистров */
#define SEQ_ADC_BASE  DT_REG_ADDR(SEQ_ADC_NODE)
#define SEQ_TIM_BASE  DT_REG_ADDR(SEQ_TIM_NODE)

#define SEQ_TIMER_DIER (SEQ_TIM_BASE + 0x0C)
#define TIM_DIER_UDE    (1 << 8)



/* Описание публичного API нашего драйвера секвенсора АЦП */
struct seq_mux_adc_api {
    /* Получить сырые данные АЦП для определенного шага и канала */
    uint32_t (*get_raw_value)(const struct device *dev, uint8_t step, uint8_t channel);
    
    /* Получить вычисленное напряжение питания VDDA в милливольтах */
    uint32_t (*get_vdda_mv)(const struct device *dev, uint8_t step);
    
    /* Получить вычисленную температуру ядра кристалла в градусах Цельсия */
    int32_t (*get_core_temp)(const struct device *dev, uint8_t step);

    /* 
     * Потокобезопасная функция получения значения любого из каналов.
     * Принимает индекс канала (0..15) и записывает результат в val.
     * Возвращает 0 при успехе, или отрицательный код ошибки.
     */
    int (*get_channel_value)(const struct device *dev, uint8_t channel_idx, uint32_t *val);
};


/* 2. ДИНАМИЧЕСКИЙ РАСЧЕТ ТАКТИРОВАНИЯ ДЛЯ GPIO ПОРТА (Смещение 8 портов -> 14 скобок) */
#define LL_AHB4_GRP1_PERIPH_GPIO \
    COND_CODE_1(DT_SAME_NODE(SEQ_GPIO_PORT_NODE, DT_NODELABEL(gpioa)), (LL_AHB4_GRP1_PERIPH_GPIOA), ( \
    COND_CODE_1(DT_SAME_NODE(SEQ_GPIO_PORT_NODE, DT_NODELABEL(gpiob)), (LL_AHB4_GRP1_PERIPH_GPIOB), ( \
    COND_CODE_1(DT_SAME_NODE(SEQ_GPIO_PORT_NODE, DT_NODELABEL(gpioc)), (LL_AHB4_GRP1_PERIPH_GPIOC), ( \
    COND_CODE_1(DT_SAME_NODE(SEQ_GPIO_PORT_NODE, DT_NODELABEL(gpiod)), (LL_AHB4_GRP1_PERIPH_GPIOD), ( \
    COND_CODE_1(DT_SAME_NODE(SEQ_GPIO_PORT_NODE, DT_NODELABEL(gpioe)), (LL_AHB4_GRP1_PERIPH_GPIOE), ( \
    COND_CODE_1(DT_SAME_NODE(SEQ_GPIO_PORT_NODE, DT_NODELABEL(gpiof)), (LL_AHB4_GRP1_PERIPH_GPIOF), ( \
    COND_CODE_1(DT_SAME_NODE(SEQ_GPIO_PORT_NODE, DT_NODELABEL(gpiog)), (LL_AHB4_GRP1_PERIPH_GPIOG), ( \
    COND_CODE_1(DT_SAME_NODE(SEQ_GPIO_PORT_NODE, DT_NODELABEL(gpioh)), (LL_AHB4_GRP1_PERIPH_GPIOH), (0) \
    ))))))))))))))


/* 3. ДИНАМИЧЕСКИЙ РАСЧЕТ ТАКТИРОВАНИЯ ДЛЯ ТАЙМЕРА (Смещение 4 таймеров -> 6 скобок) */
#define LL_APB1_GRP1_PERIPH_TIM \
    COND_CODE_1(DT_SAME_NODE(SEQ_TIM_NODE, DT_NODELABEL(timers2)), (LL_APB1_GRP1_PERIPH_TIM2), ( \
    COND_CODE_1(DT_SAME_NODE(SEQ_TIM_NODE, DT_NODELABEL(timers3)), (LL_APB1_GRP1_PERIPH_TIM3), ( \
    COND_CODE_1(DT_SAME_NODE(SEQ_TIM_NODE, DT_NODELABEL(timers4)), (LL_APB1_GRP1_PERIPH_TIM4), ( \
    COND_CODE_1(DT_SAME_NODE(SEQ_TIM_NODE, DT_NODELABEL(timers5)), (LL_APB1_GRP1_PERIPH_TIM5), (0) \
    ))))))


/* 4. ДИНАМИЧЕСКИЙ СТАРТ ТАКТИРОВАНИЯ ДЛЯ АЦП */
#define SEQ_ADC_CLOCK_ENABLE() \
    COND_CODE_1(DT_SAME_NODE(SEQ_ADC_NODE, DT_NODELABEL(adc3)), \
        (LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_ADC3);), \
        (LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_ADC12);))

#endif /* ZEPHYR_DRIVERS_SEQ_MUX_ADC_H_ */