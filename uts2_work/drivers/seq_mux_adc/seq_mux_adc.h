/***************************************************************************************************
 *   Project:       UTC2
 *   Author:        Dymov Igor
 ***************************************************************************************************
 *   Distribution:  
 *
 ***************************************************************************************************
 *   MCU Family:    STM32H7 / STM32F
 *   Compiler:      GCC (Zephyr toolchain)
 ***************************************************************************************************
 *   File:          seq_mux_adc.h
 *   Description:   Интерфейс драйвера последовательного АЦП с мультиплексором
 *
 ***************************************************************************************************
 *   History:       2026.07.23 - файл создан и оформлен по стандарту
 *
 **************************************************************************************************/
#pragma once

/***************************************************************************************************
 *                                      INCLUDED FILES
 **************************************************************************************************/
#include <zephyr/device.h>
#include <zephyr/devicetree.h>

/***************************************************************************************************
 *                                       DEFINITIONS
 **************************************************************************************************/
/// @brief Базовый узел драйвера в дереве устройств (DTS)
#define SEQ_NODE DT_NODELABEL(my_sequencer)

/* Базовые адреса портов и номера выводов для 3 управляющих пинов мультиплексора */
#define MUX_PIN_0_PORT_BASE    DT_REG_ADDR(DT_GPIO_CTLR_BY_IDX(SEQ_NODE, mux_gpios, 0))
#define MUX_PIN_0_NUM          DT_GPIO_PIN_BY_IDX(SEQ_NODE, mux_gpios, 0)

#define MUX_PIN_1_PORT_BASE    DT_REG_ADDR(DT_GPIO_CTLR_BY_IDX(SEQ_NODE, mux_gpios, 1))
#define MUX_PIN_1_NUM          DT_GPIO_PIN_BY_IDX(SEQ_NODE, mux_gpios, 1)

#define MUX_PIN_2_PORT_BASE    DT_REG_ADDR(DT_GPIO_CTLR_BY_IDX(SEQ_NODE, mux_gpios, 2))
#define MUX_PIN_2_NUM          DT_GPIO_PIN_BY_IDX(SEQ_NODE, mux_gpios, 2)

/// @brief Физический адрес регистра BSRR порта GPIO управляющего пина 0
#define SEQ_GPIO_BSRR_ADDR     (MUX_PIN_0_PORT_BASE + 0x18)

#define COMBINATIONS_CNT       8
#define ADC_CHANNELS_CNT       4
#define TOTAL_CHANNELS_CNT     (COMBINATIONS_CNT * ADC_CHANNELS_CNT)

/* Временные параметры из DTS */
#define SEQ_ADC_DURATION_US    DT_PROP(SEQ_NODE, adc_duration_us)
#define SEQ_SWITCH_DURATION_US DT_PROP(SEQ_NODE, switch_duration_us)

/* Узлы периферии, указанные в свойствах adc-dev и timer-dev в DTS */
#define SEQ_ADC_NODE           DT_PHANDLE(SEQ_NODE, adc_dev)
#define SEQ_TIM_NODE           DT_PHANDLE(SEQ_NODE, timer_dev)

/* Физические базовые адреса регистров АЦП и таймера */
#define SEQ_ADC_BASE           DT_REG_ADDR(SEQ_ADC_NODE)
#define SEQ_TIM_BASE           DT_REG_ADDR(SEQ_TIM_NODE)

/***************************************************************************************************
 *                                      PUBLIC TYPES
 **************************************************************************************************/

/**
 *  @brief Структура API для управления драйвером seq_mux_adc
 */
typedef struct seq_mux_adc_api 
{
    /// @brief Получение значения АЦП для указанного канала (0..31)
    int (*get_channel_value)(const struct device *_dev, uint8_t _channel_idx, uint32_t *_val);
    
    /// @brief Ожидание готовности новых данных АЦП с учетом таймаута
    int (*wait_for_data)(const struct device *_dev, k_timeout_t _timeout);
} seq_mux_adc_api_t;

/**
 *  @brief Структура конфигурации аппаратных ресурсов драйвера
 */
typedef struct seq_mux_adc_config 
{
    const struct device *dma_dev;          ///< Устройство DMA
    uint32_t             gpio_dma_channel; ///< Номер канала DMA для управления GPIO
    uint32_t             gpio_dma_slot;    ///< Слот аппаратного триггера DMA для GPIO
    uint32_t             adc_dma_channel;  ///< Номер канала DMA для передачи данных АЦП
    uint32_t             adc_dma_slot;     ///< Слот аппаратного триггера DMA для АЦП
} seq_mux_adc_config_t;

/***************************************************************************************************
 *                                       END OF FILE
 **************************************************************************************************/