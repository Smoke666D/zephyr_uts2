/**
 *  @file       seq_mux_adc.c
 *  @headerfile seq_mux_adc.h
 *
 *  @date       2026.07.23
 *  @author     Dymov Igor
 *
 *  @brief      Драйвер последовательного АЦП с мультиплексированием каналов
 *  @details    Обеспечивает управление внешним аналоговым мультиплексором через GPIO 
 *              и циклическое чтение данных АЦП с двойной буферизацией DMA.
 */

/***************************************************************************************************
 *                                          INCLUDED FILES
 **************************************************************************************************/
#include <stdint.h>
#include <stdbool.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/cache.h>
#include <zephyr/dt-bindings/dma/stm32_dma.h>

#include <stm32_ll_tim.h>
#include <stm32_ll_adc.h>
#include <stm32_ll_bus.h>
#include <stm32_ll_gpio.h>
#include <stm32_ll_dma.h>

#include "seq_mux_adc.h"

/***************************************************************************************************
 *                                           DEFINITIONS
 **************************************************************************************************/

/***************************************************************************************************
 *                                   PRIVATE FUNCTION PROTOTYPES
 **************************************************************************************************/
static void        _dma_callback(const struct device *_dev, void *_user_data, 
                                 uint32_t _channel, int _status);
static int         _seq_mux_adc_get_channel_value_impl(const struct device *_dev, 
                                                       uint8_t _channel_idx, 
                                                       uint32_t *_val);
static int         _seq_mux_adc_wait_for_data_impl(const struct device *_dev, 
                                                   k_timeout_t _timeout);
static void        _fill_gpio_buffer(uint32_t *_buf);
static inline int  _dma_init(const struct seq_mux_adc_config *_config, 
                             ADC_TypeDef *_adc_inst, 
                             const struct device *_dev);
static inline void _gpio_init(void);
static inline int  _tmr_init(TIM_TypeDef *_tmr_inst);
static inline void _adc_init(ADC_TypeDef *_adc_inst);
static int         _seq_mux_adc_init(const struct device *_dev);

/***************************************************************************************************
 *                                           PRIVATE DATA
 **************************************************************************************************/
static uint32_t gpio_raw_buf[COMBINATIONS_CNT] __attribute__((section("SRAM4")));
static uint32_t adc_buf_a[TOTAL_CHANNELS_CNT] __nocache;
static uint32_t adc_buf_b[TOTAL_CHANNELS_CNT] __nocache;
static uint32_t adc_shadow_buf[TOTAL_CHANNELS_CNT];

static struct k_mutex mutex_lock;
static struct k_sem   sem_data_ready;

/***************************************************************************************************
 *                                        PRIVATE FUNCTIONS
 **************************************************************************************************/

/**
 *  @brief      Обработчик завершения DMA-передачи АЦП
 *  @details    Выполняется в контексте прерывания (ISR). Копирует данные 
 *              из активного буфера в теневой буфер и выдает семафор готовности.
 *
 *  @param      _dev       - Указатель на устройство DMA
 *  @param      _user_data - Пользовательские данные (указатель на устройство)
 *  @param      _channel   - Номер канала DMA
 *  @param      _status    - Статус передачи DMA
 */
static void _dma_callback(const struct device *_dev, void *_user_data, 
                          uint32_t _channel, int _status)
{
    ARG_UNUSED(_dev);
    ARG_UNUSED(_user_data);
    ARG_UNUSED(_channel);

    if (_status >= 0) 
    {
        // Копирование данных из заполненного буфера в теневой
        if (LL_DMA_GetCurrentTargetMem(DMA1, LL_DMA_STREAM_1) == 1)
        {
            memcpy(adc_shadow_buf, adc_buf_a, sizeof(adc_shadow_buf));
        } 
        else 
        {
            memcpy(adc_shadow_buf, adc_buf_b, sizeof(adc_shadow_buf));
        }
        k_sem_give(&sem_data_ready);
    }
}

/**
 *  @brief      Получение значения АЦП для указанного канала
 *  @details    Блокирует доступ с помощью мьютекса и считывает 
 *              сохраненное значение канала.
 *
 *  @param      _dev         - Указатель на устройство
 *  @param      _channel_idx - Индекс канала
 *  @param      _val         - Указатель для записи значения
 *
 *  @return     int - Ноль в случае успеха, код ошибки в противном случае
 */
static int _seq_mux_adc_get_channel_value_impl(const struct device *_dev, 
                                               uint8_t _channel_idx, 
                                               uint32_t *_val)
{
    int ret;

    if (false
        || _channel_idx >= TOTAL_CHANNELS_CNT
        || _val == NULL
       ) 
    {
        return -EINVAL;
    }

    ret = k_mutex_lock(&mutex_lock, K_MSEC(100));
    if (ret < 0) 
    {
        return -EAGAIN;
    }

    *_val = adc_shadow_buf[_channel_idx];

    k_mutex_unlock(&mutex_lock);
    return 0;
}

/**
 *  @brief      Ожидание готовности новых данных АЦП
 *  @details    Ожидает освобождение семафора готовности данных 
 *              в течение заданного таймаута.
 *
 *  @param      _dev     - Указатель на устройство
 *  @param      _timeout - Время ожидания
 *
 *  @return     int - Ноль при успехе, код ошибки при таймауте
 */
static int _seq_mux_adc_wait_for_data_impl(const struct device *_dev, 
                                           k_timeout_t _timeout)
{
    ARG_UNUSED(_dev);
    return k_sem_take(&sem_data_ready, _timeout);
}

/**
 *  @brief      Заполнение буфера значений для BSRR GPIO
 *  @details    Формирует маску переключения выводов мультиплексора 
 *              для каждого состояния.
 *
 *  @param      _buf - Указатель на буфер для заполнения
 */
static void _fill_gpio_buffer(uint32_t *_buf)
{
    const uint32_t pins[3] = {MUX_PIN_0_NUM, MUX_PIN_1_NUM, MUX_PIN_2_NUM};

    for (int_fast32_t i = 0; i < COMBINATIONS_CNT; i++) 
    {
        uint_fast8_t state = (i + 1) % COMBINATIONS_CNT;
        uint32_t     bsrr_val = 0;
        
        for (int_fast32_t bit = 0; bit < 3; bit++) 
        {
            if (state & (1 << bit)) 
            {
                bsrr_val |= (1 << pins[bit]);
            } 
            else 
            {
                bsrr_val |= (1 << (pins[bit] + 16));
            }
        }
        _buf[i] = bsrr_val;
    }
}

/**
 *  @brief      Инициализация каналов DMA для GPIO и АЦП
 *  @details    Конфигурирует каналы DMA для передачи паттернов в BSRR 
 *              и чтения данных из АЦП в режиме двойного буфера.
 *
 *  @param      _config   - Указатель на конфигурацию устройства
 *  @param      _adc_inst - Указатель на регистры АЦП
 *  @param      _dev      - Указатель на устройство
 *
 *  @return     int - Ноль при успехе, код ошибки при сбое
 */
static inline int _dma_init(const struct seq_mux_adc_config *_config, 
                            ADC_TypeDef *_adc_inst, 
                            const struct device *_dev)
{
    int ret;

    if (!device_is_ready(_config->dma_dev)) 
    {
        return -ENODEV;
    }

    struct dma_block_config gpio_block_cfg = 
    {
        .source_address   = (uint32_t)gpio_raw_buf,
        .dest_address     = SEQ_GPIO_BSRR_ADDR,
        .block_size       = COMBINATIONS_CNT * sizeof(uint32_t),
        .source_addr_adj  = DMA_ADDR_ADJ_INCREMENT,
        .dest_addr_adj    = DMA_ADDR_ADJ_NO_CHANGE,
        .source_reload_en = 1,
        .dest_reload_en   = 1,
    };

    struct dma_config gpio_dma_cfg = 
    {
        .dma_slot            = _config->gpio_dma_slot,
        .channel_direction   = MEMORY_TO_PERIPHERAL,
        .source_data_size    = 4,
        .dest_data_size      = 4,
        .source_burst_length = 4,
        .dest_burst_length   = 4,
        .channel_priority    = 2, // Высокий приоритет DMA
        .block_count         = 1,
        .head_block          = &gpio_block_cfg,
    };

    ret = dma_config(_config->dma_dev, _config->gpio_dma_channel, &gpio_dma_cfg);
    if (ret < 0) 
    {
        return ret;
    }

    struct dma_block_config adc_block_cfg = 
    {
        .source_address   = (uint32_t)&(_adc_inst->DR),
        .dest_address     = (uint32_t)adc_buf_a,
        .block_size       = TOTAL_CHANNELS_CNT * sizeof(uint32_t),
        .source_addr_adj  = DMA_ADDR_ADJ_NO_CHANGE,
        .dest_addr_adj    = DMA_ADDR_ADJ_INCREMENT,
        .source_reload_en = 1,
        .dest_reload_en   = 1,
    };

    struct dma_config adc_dma_cfg = 
    {
        .dma_slot             = _config->adc_dma_slot,
        .channel_direction    = PERIPHERAL_TO_MEMORY,
        .source_data_size     = 4,
        .dest_data_size       = 4,
        .source_burst_length  = 4,
        .dest_burst_length    = 4,
        .channel_priority     = 3, // Очень высокий приоритет DMA
        .block_count          = 1,
        .head_block           = &adc_block_cfg,
        .dma_callback         = _dma_callback,
        .user_data            = (void *)_dev,
        .complete_callback_en = true,
    };

    ret = dma_config(_config->dma_dev, _config->adc_dma_channel, &adc_dma_cfg);
    if (ret < 0) 
    {
        return ret;
    }

    LL_DMA_SetMemory1Address(DMA1, LL_DMA_STREAM_1, (uint32_t)adc_buf_b);
    LL_DMA_EnableDoubleBufferMode(DMA1, LL_DMA_STREAM_1);

    return 0;
}

/**
 *  @brief      Инициализация выводов мультиплексора GPIO
 *  @details    Настраивает порты вывода для сигналов управления 
 *              и сбрасывает их в нулевое состояние.
 */
static inline void _gpio_init(void)
{
    // Настройка вывода MUX_A0
    GPIO_TypeDef *port0 = (GPIO_TypeDef *)MUX_PIN_0_PORT_BASE;
    LL_GPIO_SetPinMode(port0, 1 << MUX_PIN_0_NUM, LL_GPIO_MODE_OUTPUT);
    LL_GPIO_SetPinOutputType(port0, 1 << MUX_PIN_0_NUM, LL_GPIO_OUTPUT_PUSHPULL);
    LL_GPIO_SetPinSpeed(port0, 1 << MUX_PIN_0_NUM, LL_GPIO_SPEED_FREQ_VERY_HIGH);
    LL_GPIO_SetPinPull(port0, 1 << MUX_PIN_0_NUM, LL_GPIO_PULL_NO);
    LL_GPIO_ResetOutputPin(port0, 1 << MUX_PIN_0_NUM);

    // Настройка вывода MUX_A1
    GPIO_TypeDef *port1 = (GPIO_TypeDef *)MUX_PIN_1_PORT_BASE;
    LL_GPIO_SetPinMode(port1, 1 << MUX_PIN_1_NUM, LL_GPIO_MODE_OUTPUT);
    LL_GPIO_SetPinOutputType(port1, 1 << MUX_PIN_1_NUM, LL_GPIO_OUTPUT_PUSHPULL);
    LL_GPIO_SetPinSpeed(port1, 1 << MUX_PIN_1_NUM, LL_GPIO_SPEED_FREQ_VERY_HIGH);
    LL_GPIO_SetPinPull(port1, 1 << MUX_PIN_1_NUM, LL_GPIO_PULL_NO);
    LL_GPIO_ResetOutputPin(port1, 1 << MUX_PIN_1_NUM);

    // Настройка вывода MUX_A2
    GPIO_TypeDef *port2 = (GPIO_TypeDef *)MUX_PIN_2_PORT_BASE;
    LL_GPIO_SetPinMode(port2, 1 << MUX_PIN_2_NUM, LL_GPIO_MODE_OUTPUT);
    LL_GPIO_SetPinOutputType(port2, 1 << MUX_PIN_2_NUM, LL_GPIO_OUTPUT_PUSHPULL);
    LL_GPIO_SetPinSpeed(port2, 1 << MUX_PIN_2_NUM, LL_GPIO_SPEED_FREQ_VERY_HIGH);
    LL_GPIO_SetPinPull(port2, 1 << MUX_PIN_2_NUM, LL_GPIO_PULL_NO);
    LL_GPIO_ResetOutputPin(port2, 1 << MUX_PIN_2_NUM);

    // Гарантированный сброс всех управляющих ножек перед стартом
    LL_GPIO_ResetOutputPin(port0, 1 << MUX_PIN_0_NUM);
    LL_GPIO_ResetOutputPin(port1, 1 << MUX_PIN_1_NUM);
    LL_GPIO_ResetOutputPin(port2, 1 << MUX_PIN_2_NUM);
}

/**
 *  @brief      Инициализация аппаратного таймера
 *  @details    Настраивает частоту, период ARR и значение CCR1 на основе DTS.
 *              Включает триггер TRGO для АЦП и запрос DMA для GPIO.
 *
 *  @param      _tmr_inst - Указатель на регистры таймера
 *
 *  @return     int - Ноль при успехе, код ошибки при сбое
 */
static inline int _tmr_init(TIM_TypeDef *_tmr_inst)
{
    const struct device *counter_dev = 
        DEVICE_DT_GET(DT_CHILD(SEQ_TIM_NODE, counter));
    
    if (!device_is_ready(counter_dev)) 
    {
        return -ENODEV;
    }

    uint32_t freq = counter_get_frequency(counter_dev);
    
    uint32_t t1_ticks = (uint32_t)((uint64_t)SEQ_ADC_DURATION_US 
                                   * freq / 1000000ULL);
    uint32_t t2_ticks = (uint32_t)((uint64_t)SEQ_SWITCH_DURATION_US 
                                   * freq / 1000000ULL);

    uint32_t arr_val = t1_ticks + t2_ticks - 1;
    uint32_t ccr1_val = t1_ticks;

    LL_TIM_SetPrescaler(_tmr_inst, 10000 - 1);
    LL_TIM_SetAutoReload(_tmr_inst, arr_val);
    
    LL_TIM_OC_SetMode(_tmr_inst, LL_TIM_CHANNEL_CH1, LL_TIM_OCMODE_PWM1);
    LL_TIM_OC_SetCompareCH1(_tmr_inst, ccr1_val);
    LL_TIM_CC_EnableChannel(_tmr_inst, LL_TIM_CHANNEL_CH1);

    // Событие UPDATE генерирует триггер TRGO для старта АЦП3
    LL_TIM_SetTriggerOutput(_tmr_inst, LL_TIM_TRGO_UPDATE);

    // Событие CC1 генерирует запрос DMA 1 для переключения ножек
    LL_TIM_EnableDMAReq_CC1(_tmr_inst);

    return 0;
}

/**
 *  @brief      Инициализация выбранного модуля АЦП
 *  @details    Настраивает тактирование, триггеры, секвенсор, калибровку 
 *              и запускает модуль АЦП.
 *
 *  @param      _adc_inst - Указатель на регистры АЦП
 */
static inline void _adc_init(ADC_TypeDef *_adc_inst)
{
    // Включение тактирования выбранного АЦП
    if (_adc_inst == ADC3) 
    {
        LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_ADC3);
    } 
    else if (false
             || _adc_inst == ADC1 
             || _adc_inst == ADC2
            ) 
    {
        LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_ADC12);
    }

    if (LL_ADC_IsDeepPowerDownEnabled(_adc_inst) != 0UL) 
    {
        LL_ADC_DisableDeepPowerDown(_adc_inst);
    }
    if (LL_ADC_IsInternalRegulatorEnabled(_adc_inst) == 0UL) 
    {
        LL_ADC_EnableInternalRegulator(_adc_inst);
        k_busy_wait(20);
    }

#if DT_SAME_NODE(SEQ_ADC_NODE, DT_NODELABEL(adc3))
    ADC_Common_TypeDef *adc_common = ADC3_COMMON;
#else
    ADC_Common_TypeDef *adc_common = ADC12_COMMON;
#endif

    LL_ADC_SetCommonClock(adc_common, LL_ADC_CLOCK_ASYNC_DIV2);
    LL_ADC_SetCommonPathInternalCh(
        adc_common, 
        LL_ADC_PATH_INTERNAL_TEMPSENSOR | LL_ADC_PATH_INTERNAL_VREFINT
    );
    
    LL_ADC_REG_SetTriggerSource(_adc_inst, LL_ADC_REG_TRIG_EXT_TIM3_TRGO);
    LL_ADC_REG_SetTriggerEdge(_adc_inst, LL_ADC_REG_TRIG_EXT_RISING);
    
    LL_ADC_REG_SetDataTransferMode(_adc_inst, LL_ADC_REG_DMA_TRANSFER_UNLIMITED);
    LL_ADC_REG_SetSequencerLength(_adc_inst, LL_ADC_REG_SEQ_SCAN_ENABLE_2RANKS);
    LL_ADC_REG_SetSequencerRanks(_adc_inst, LL_ADC_REG_RANK_1, 
                                 LL_ADC_CHANNEL_TEMPSENSOR);
    LL_ADC_REG_SetSequencerRanks(_adc_inst, LL_ADC_REG_RANK_2, 
                                 LL_ADC_CHANNEL_VREFINT);
    LL_ADC_REG_SetSequencerRanks(_adc_inst, LL_ADC_REG_RANK_3, 
                                 LL_ADC_CHANNEL_TEMPSENSOR);
    LL_ADC_REG_SetSequencerRanks(_adc_inst, LL_ADC_REG_RANK_4, 
                                 LL_ADC_CHANNEL_VREFINT);

    LL_ADC_SetChannelPreSelection(_adc_inst, LL_ADC_CHANNEL_TEMPSENSOR);
    LL_ADC_SetChannelPreSelection(_adc_inst, LL_ADC_CHANNEL_VREFINT);
    LL_ADC_SetChannelSamplingTime(_adc_inst, LL_ADC_CHANNEL_TEMPSENSOR, 
                                  LL_ADC_SAMPLINGTIME_810CYCLES_5);
    LL_ADC_SetChannelSamplingTime(_adc_inst, LL_ADC_CHANNEL_VREFINT, 
                                  LL_ADC_SAMPLINGTIME_810CYCLES_5);
    LL_ADC_SetChannelSingleDiff(_adc_inst, LL_ADC_CHANNEL_TEMPSENSOR, 
                                LL_ADC_SINGLE_ENDED);
    LL_ADC_SetChannelSingleDiff(_adc_inst, LL_ADC_CHANNEL_VREFINT, 
                                LL_ADC_SINGLE_ENDED);

    LL_ADC_StartCalibration(_adc_inst, LL_ADC_CALIB_OFFSET, 
                            LL_ADC_SINGLE_ENDED);
    while (LL_ADC_IsCalibrationOnGoing(_adc_inst)) 
    {
        k_busy_wait(1);
    }

    LL_ADC_Enable(_adc_inst);
    while (!LL_ADC_IsActiveFlag_ADRDY(_adc_inst)) 
    {
        k_busy_wait(1);
    }
}

/**
 *  @brief      Основная функция инициализации драйвера seq_mux_adc
 *  @details    Конфигурирует таймер, GPIO, DMA и АЦП для совместной работы.
 *
 *  @param      _dev - Указатель на устройство
 *
 *  @return     int - Ноль при успехе, код ошибки при сбое
 */
static int _seq_mux_adc_init(const struct device *_dev)
{
    const struct seq_mux_adc_config *config = _dev->config;
    int ret;

    ADC_TypeDef *adc_inst = (ADC_TypeDef *)SEQ_ADC_BASE;
    TIM_TypeDef *tmr_inst = (TIM_TypeDef *)SEQ_TIM_BASE;

    k_mutex_init(&mutex_lock);
    k_sem_init(&sem_data_ready, 0, 1);

    _gpio_init();

    _fill_gpio_buffer(gpio_raw_buf);
    
    // Конфигурация DMA
    ret = _dma_init(config, adc_inst, _dev);
    if (ret < 0) 
    {
        return ret;
    }

    // Инициализация таймера
    ret = _tmr_init(tmr_inst);
    if (ret < 0) 
    {
        return ret;
    }

    // Инициализация модуля АЦП
    _adc_init(adc_inst);

    // Запуск DMA и таймера
    ret = dma_start(config->dma_dev, config->gpio_dma_channel);
    if (ret < 0) 
    {
        return ret;
    }

    ret = dma_start(config->dma_dev, config->adc_dma_channel);
    if (ret < 0) 
    {
        return ret;
    }

    LL_ADC_REG_StartConversion(adc_inst);
    LL_TIM_EnableCounter(tmr_inst);

    return 0;
}

/***************************************************************************************************
 *                                        PUBLIC FUNCTIONS
 **************************************************************************************************/

static const struct seq_mux_adc_api driver_api = 
{
    .get_channel_value = _seq_mux_adc_get_channel_value_impl,
    .wait_for_data     = _seq_mux_adc_wait_for_data_impl,
};

static const struct seq_mux_adc_config seq_config = 
{
    .dma_dev          = DEVICE_DT_GET(DT_DMAS_CTLR(DT_NODELABEL(my_sequencer))),
    .gpio_dma_channel = DT_DMAS_CELL_BY_NAME(DT_NODELABEL(my_sequencer), 
                                              seq_dma, channel),
    .gpio_dma_slot    = DT_DMAS_CELL_BY_NAME(DT_NODELABEL(my_sequencer), 
                                              seq_dma, slot),
    .adc_dma_channel  = DT_DMAS_CELL_BY_NAME(DT_NODELABEL(my_sequencer), 
                                              adc_dma, channel),
    .adc_dma_slot     = DT_DMAS_CELL_BY_NAME(DT_NODELABEL(my_sequencer), 
                                              adc_dma, slot),
};

DEVICE_DT_DEFINE(DT_NODELABEL(my_sequencer),
                 _seq_mux_adc_init,
                 NULL,
                 NULL,
                 &seq_config,
                 POST_KERNEL,
                 CONFIG_APPLICATION_INIT_PRIORITY,
                 &driver_api);

/***************************************************************************************************
 *                                           END OF FILE
 **************************************************************************************************/