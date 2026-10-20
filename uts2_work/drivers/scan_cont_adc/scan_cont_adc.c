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
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/stm32_clock_control.h>
#include <stm32_ll_tim.h>
#include <stm32_ll_adc.h>
#include <stm32_ll_bus.h>
#include <stm32_ll_gpio.h>
#include <stm32_ll_dma.h>

#include "scan_cont_adc.h"

/***************************************************************************************************
 *                                           DEFINITIONS
 **************************************************************************************************/

/***************************************************************************************************
 *                                   PRIVATE FUNCTION PROTOTYPES
 **************************************************************************************************/
static void        _dma_callback(const struct device *_dev, void *_user_data, 
                                 uint32_t _channel, int _status);
static inline int  _dma_init(const struct scan_cont_adc_config *_config, 
                             ADC_TypeDef *_adc_inst, 
                             const struct device *_dev);
static inline int  _tmr_init(TIM_TypeDef *_tmr_inst);
static inline void _adc_init(ADC_TypeDef *_adc_inst, uint32_t trg_source);
static int         _seq_mux_adc_init(const struct device *_dev);

/***************************************************************************************************
 *                                           PRIVATE DATA
 **************************************************************************************************/
static uint16_t adc_buf_a[ADC_CHANNELS_CNT] __nocache;
static uint16_t adc_buf_b[ADC_CHANNELS_CNT] __nocache;
static volatile uint16_t *active_adc_buf;

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
        if (LL_DMA_GetCurrentTargetMem(DMA1, LL_DMA_STREAM_2) == 1)
        {
            active_adc_buf = adc_buf_a;
        } 
        else 
        {
            active_adc_buf = adc_buf_b;
        }
        k_sem_give(&sem_data_ready);
    }
}


static int _scan_adc_get_buf_p(const struct device *_dev,  uint16_t *_user_buf)
{
    ARG_UNUSED(_dev);
    memcpy(_user_buf, (const void *)active_adc_buf, ADC_CHANNELS_CNT * sizeof(uint16_t));
    return 0;

}



static struct k_sem * _seq_mux_adc_get_sem_impl(const struct device *_dev)
{
    ARG_UNUSED(_dev);

    /* Возвращаем адрес нашего системного семафора, который выдается в dma_callback */
    return &sem_data_ready;
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
static inline int _dma_init(const struct scan_cont_adc_config *_config, 
                            ADC_TypeDef *_adc_inst, 
                            const struct device *_dev)
{
    int ret;

    if (!device_is_ready(_config->dma_dev)) 
    {
        return -ENODEV;
    }

    struct dma_block_config adc_block_cfg = 
    {
        .source_address   = (uint32_t)&(_adc_inst->DR),
        .dest_address     = (uint32_t)adc_buf_a,
        .block_size       = ADC_CHANNELS_CNT  * sizeof(uint16_t),
        .source_addr_adj  = DMA_ADDR_ADJ_NO_CHANGE,
        .dest_addr_adj    = DMA_ADDR_ADJ_INCREMENT,
        .source_reload_en = 1,
        .dest_reload_en   = 1,
    };

    struct dma_config adc_dma_cfg = 
    {
        .dma_slot             = _config->adc_dma_slot,
        .channel_direction    = PERIPHERAL_TO_MEMORY,
        .source_data_size     = 2,
        .dest_data_size       = 2,
        .source_burst_length  = 1,
        .dest_burst_length    = 1,
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

    LL_DMA_SetMemory1Address(DMA1, LL_DMA_STREAM_2, (uint32_t)adc_buf_b);
    LL_DMA_EnableDoubleBufferMode(DMA1, LL_DMA_STREAM_2);
    LL_DMA_EnableIT_TC(DMA1, LL_DMA_STREAM_2);

    // Убедитесь, что HT выключено:
    LL_DMA_DisableIT_HT(DMA1, LL_DMA_STREAM_2);
    return 0;
}



/**
 *  @brief      Инициализация выводов мультиплексора GPIO
 *  @details    Настраивает порты вывода для сигналов управления 
 *              и сбрасывает их в нулевое состояние.
 */
static inline void _gpio_init(void)
{

    LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_GPIOA);    
    LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_GPIOB);
    LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_GPIOC);
    LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_6, LL_GPIO_MODE_ANALOG);
    LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_7, LL_GPIO_MODE_ANALOG);
    LL_GPIO_SetPinMode(GPIOB, LL_GPIO_PIN_0, LL_GPIO_MODE_ANALOG);
    LL_GPIO_SetPinMode(GPIOB, LL_GPIO_PIN_1, LL_GPIO_MODE_ANALOG);
    LL_GPIO_SetPinMode(GPIOC, LL_GPIO_PIN_0, LL_GPIO_MODE_ANALOG);
    LL_GPIO_SetPinMode(GPIOC, LL_GPIO_PIN_1, LL_GPIO_MODE_ANALOG);
    LL_GPIO_SetPinMode(GPIOC, LL_GPIO_PIN_4, LL_GPIO_MODE_ANALOG);
    LL_GPIO_SetPinMode(GPIOC, LL_GPIO_PIN_5, LL_GPIO_MODE_ANALOG);

    
    // Отключаем подтяжки (Pull-up / Pull-down), чтобы не портить аналоговый сигнал
    LL_GPIO_SetPinPull(GPIOA, LL_GPIO_PIN_6, LL_GPIO_PULL_NO);
    LL_GPIO_SetPinPull(GPIOA, LL_GPIO_PIN_7, LL_GPIO_PULL_NO);
    LL_GPIO_SetPinPull(GPIOB, LL_GPIO_PIN_0, LL_GPIO_PULL_NO);
    LL_GPIO_SetPinPull(GPIOB, LL_GPIO_PIN_1, LL_GPIO_PULL_NO);
    LL_GPIO_SetPinPull(GPIOC, LL_GPIO_PIN_0, LL_GPIO_PULL_NO);
    LL_GPIO_SetPinPull(GPIOC, LL_GPIO_PIN_1, LL_GPIO_PULL_NO);
    LL_GPIO_SetPinPull(GPIOC, LL_GPIO_PIN_4, LL_GPIO_PULL_NO);
    LL_GPIO_SetPinPull(GPIOC, LL_GPIO_PIN_5, LL_GPIO_PULL_NO);

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

   LL_TIM_DisableCounter(_tmr_inst);

// 2. Явно задаем режим счета ВВЕРХ (Up-counting), чтобы исключить удвоение частоты TRGO
LL_TIM_SetCounterMode(_tmr_inst, LL_TIM_COUNTERMODE_UP);

uint32_t freq = counter_get_frequency(counter_dev);

// Период таймера задается одной настройкой SEQ_ADC_DURATION_US
uint32_t total_ticks = (uint32_t)((uint64_t)SEQ_ADC_DURATION_US 
                               * freq / 1000000ULL);

uint32_t arr_val = total_ticks > 0 ? total_ticks - 1 : 0;


//uint32_t psc_val = (freq / 1000000UL) > 0 ? (freq / 1000000UL) - 1 : 0;

//LL_TIM_SetPrescaler(_tmr_inst, psc_val);
LL_TIM_SetAutoReload(_tmr_inst, arr_val);

LL_TIM_CC_DisableChannel(_tmr_inst, LL_TIM_CHANNEL_CH1);

// Событие UPDATE генерирует триггер TRGO для старта АЦП3
LL_TIM_SetTriggerOutput(_tmr_inst, LL_TIM_TRGO_UPDATE);

  LL_TIM_GenerateEvent_UPDATE(_tmr_inst);

// Очищаем случайный флаг обновления, если он взвелся программно
LL_TIM_ClearFlag_UPDATE(_tmr_inst);

// 5. Запускаем таймер
LL_TIM_EnableCounter(_tmr_inst);

return 0;
}

/**
 *  @brief      Инициализация выбранного модуля АЦП
 *  @details    Настраивает тактирование, триггеры, секвенсор, калибровку 
 *              и запускает модуль АЦП.
 *
 *  @param      _adc_inst - Указатель на регистры АЦП
 */
static inline void _adc_init(ADC_TypeDef *_adc_inst, uint32_t trg_source)
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

    
    LL_ADC_REG_SetTriggerSource(_adc_inst, trg_source);
    LL_ADC_REG_SetTriggerEdge(_adc_inst, LL_ADC_REG_TRIG_EXT_RISING);
    
    LL_ADC_REG_SetDataTransferMode(_adc_inst, LL_ADC_REG_DMA_TRANSFER_UNLIMITED);

    LL_ADC_SetChannelPreSelection(_adc_inst, LL_ADC_CHANNEL_3);
    LL_ADC_SetChannelPreSelection(_adc_inst, LL_ADC_CHANNEL_7);
    LL_ADC_SetChannelPreSelection(_adc_inst, LL_ADC_CHANNEL_9);  
    LL_ADC_SetChannelPreSelection(_adc_inst, LL_ADC_CHANNEL_5);
    LL_ADC_SetChannelPreSelection(_adc_inst, LL_ADC_CHANNEL_10);
    LL_ADC_SetChannelPreSelection(_adc_inst, LL_ADC_CHANNEL_11);
    LL_ADC_SetChannelPreSelection(_adc_inst, LL_ADC_CHANNEL_4);
    LL_ADC_SetChannelPreSelection(_adc_inst, LL_ADC_CHANNEL_8);


    LL_ADC_REG_SetSequencerLength(_adc_inst, LL_ADC_REG_SEQ_SCAN_ENABLE_8RANKS);

    LL_ADC_REG_SetSequencerRanks(_adc_inst, LL_ADC_REG_RANK_1, LL_ADC_CHANNEL_3);
    LL_ADC_REG_SetSequencerRanks(_adc_inst, LL_ADC_REG_RANK_2, LL_ADC_CHANNEL_7);
    LL_ADC_REG_SetSequencerRanks(_adc_inst, LL_ADC_REG_RANK_3, LL_ADC_CHANNEL_9);
    LL_ADC_REG_SetSequencerRanks(_adc_inst, LL_ADC_REG_RANK_4, LL_ADC_CHANNEL_5);
    LL_ADC_REG_SetSequencerRanks(_adc_inst, LL_ADC_REG_RANK_5, LL_ADC_CHANNEL_10);
    LL_ADC_REG_SetSequencerRanks(_adc_inst, LL_ADC_REG_RANK_6, LL_ADC_CHANNEL_11);
    LL_ADC_REG_SetSequencerRanks(_adc_inst, LL_ADC_REG_RANK_7, LL_ADC_CHANNEL_4);
    LL_ADC_REG_SetSequencerRanks(_adc_inst, LL_ADC_REG_RANK_8, LL_ADC_CHANNEL_8);


    // 7. Настройка времени выборки (Sampling Time) уменьшена до 64.5 циклов для баланса со скоростью 1 кГц
    LL_ADC_SetChannelSamplingTime(_adc_inst, LL_ADC_CHANNEL_3, LL_ADC_SAMPLINGTIME_64CYCLES_5);
    LL_ADC_SetChannelSamplingTime(_adc_inst, LL_ADC_CHANNEL_7, LL_ADC_SAMPLINGTIME_64CYCLES_5);
    LL_ADC_SetChannelSamplingTime(_adc_inst, LL_ADC_CHANNEL_9, LL_ADC_SAMPLINGTIME_64CYCLES_5);
    LL_ADC_SetChannelSamplingTime(_adc_inst, LL_ADC_CHANNEL_5, LL_ADC_SAMPLINGTIME_64CYCLES_5);
    LL_ADC_SetChannelSamplingTime(_adc_inst, LL_ADC_CHANNEL_10, LL_ADC_SAMPLINGTIME_64CYCLES_5);
    LL_ADC_SetChannelSamplingTime(_adc_inst, LL_ADC_CHANNEL_11, LL_ADC_SAMPLINGTIME_64CYCLES_5);
    LL_ADC_SetChannelSamplingTime(_adc_inst, LL_ADC_CHANNEL_4, LL_ADC_SAMPLINGTIME_64CYCLES_5);
    LL_ADC_SetChannelSamplingTime(_adc_inst, LL_ADC_CHANNEL_8, LL_ADC_SAMPLINGTIME_64CYCLES_5);

    // 8. Установка режима Single-ended для всех каналов
    LL_ADC_SetChannelSingleDiff(_adc_inst, LL_ADC_CHANNEL_3, LL_ADC_SINGLE_ENDED);
    LL_ADC_SetChannelSingleDiff(_adc_inst, LL_ADC_CHANNEL_7, LL_ADC_SINGLE_ENDED);
    LL_ADC_SetChannelSingleDiff(_adc_inst, LL_ADC_CHANNEL_9, LL_ADC_SINGLE_ENDED);
    LL_ADC_SetChannelSingleDiff(_adc_inst, LL_ADC_CHANNEL_5, LL_ADC_SINGLE_ENDED);
    LL_ADC_SetChannelSingleDiff(_adc_inst, LL_ADC_CHANNEL_10, LL_ADC_SINGLE_ENDED);
    LL_ADC_SetChannelSingleDiff(_adc_inst, LL_ADC_CHANNEL_11, LL_ADC_SINGLE_ENDED);
    LL_ADC_SetChannelSingleDiff(_adc_inst, LL_ADC_CHANNEL_4, LL_ADC_SINGLE_ENDED);
    LL_ADC_SetChannelSingleDiff(_adc_inst, LL_ADC_CHANNEL_8, LL_ADC_SINGLE_ENDED);


    // --- 9. НАСТРОЙКА АППАРАТНОГО ОВЕРСЭМПЛИНГА (x16) ---
    if (LL_ADC_IsEnabled(_adc_inst) == 1)
    {
        LL_ADC_Disable(_adc_inst);
        while (LL_ADC_IsEnabled(_adc_inst) == 1);
    }

    // Включаем оверсэмплинг для регулярной группы с непрерывным режимом
    LL_ADC_SetOverSamplingDiscont(_adc_inst, LL_ADC_OVS_REG_CONT);
    
    // Настраиваем коэффициент 16 и сдвиг вправо на 4 бита (2^4 = 16)
    // Используем правильное имя функции для STM32H7: LL_ADC_ConfigOverSamplingRatioShift
    LL_ADC_ConfigOverSamplingRatioShift(_adc_inst, 16, LL_ADC_OVS_SHIFT_RIGHT_4);
    
    // Активируем оверсэмплинг в scope регулярных каналов
    LL_ADC_SetOverSamplingScope(_adc_inst, LL_ADC_OVS_GRP_REGULAR_CONTINUED);
    // ----------------------------------------------------


    // 10. Калибровка АЦП
    LL_ADC_StartCalibration(_adc_inst, LL_ADC_CALIB_OFFSET, 
                            LL_ADC_SINGLE_ENDED);
    while (LL_ADC_IsCalibrationOnGoing(_adc_inst)) 
    {
        k_busy_wait(1);
    }

    LL_ADC_REG_SetContinuousMode(_adc_inst, LL_ADC_REG_CONV_SINGLE);
  

    // 11. Включение АЦП
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
static int _scan_cont_adc_init(const struct device *_dev)
{
    const struct scan_cont_adc_config  *config = _dev->config;
    int ret;

    ADC_TypeDef *adc_inst = (ADC_TypeDef *)SEQ_ADC_BASE;
    TIM_TypeDef *tmr_inst = (TIM_TypeDef *)SEQ_TIM_BASE;

    k_sem_init(&sem_data_ready, 0, 1);

    _gpio_init();


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

     uint32_t auto_trigger_source = LL_ADC_REG_TRIG_EXT_TIM3_TRGO; // Дефолт на случай неизвестного таймера

    if      (tmr_inst == TIM1)  auto_trigger_source = LL_ADC_REG_TRIG_EXT_TIM1_TRGO;
    else if (tmr_inst == TIM2)  auto_trigger_source = LL_ADC_REG_TRIG_EXT_TIM2_TRGO;
    else if (tmr_inst == TIM3)  auto_trigger_source = LL_ADC_REG_TRIG_EXT_TIM3_TRGO;
    else if (tmr_inst == TIM4)  auto_trigger_source = LL_ADC_REG_TRIG_EXT_TIM4_TRGO;
    else if (tmr_inst == TIM8)  auto_trigger_source = LL_ADC_REG_TRIG_EXT_TIM8_TRGO;
    else if (tmr_inst == TIM15) auto_trigger_source = LL_ADC_REG_TRIG_EXT_TIM15_TRGO;
#ifdef LPTIM1
    else if (tmr_inst == LPTIM1) auto_trigger_source = LL_ADC_REG_TRIG_EXT_LPTIM1_OUT;
#endif
#ifdef LPTIM2
    else if (tmr_inst == LPTIM2) auto_trigger_source = LL_ADC_REG_TRIG_EXT_LPTIM2_OUT;
#endif
    else 
    {
        // Если таймер не попал ни в один список, можно вернуть ошибку или оставить софтовый триггер
        return -ENOTSUP; 
    }

    // Инициализация модуля АЦП
     _adc_init(adc_inst, auto_trigger_source);

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

static const struct scan_cont_adc_api driver_api = 
{
    .get_sem           = _seq_mux_adc_get_sem_impl,
    .get_buf_pointer  = _scan_adc_get_buf_p,
};

static const struct scan_cont_adc_config scan_config = 
{
    .dma_dev          = DEVICE_DT_GET(DT_DMAS_CTLR(DT_NODELABEL(scan_adc))),

    .adc_dma_channel  = DT_DMAS_CELL_BY_NAME(DT_NODELABEL(scan_adc), 
                                              adc_dma, channel),
    .adc_dma_slot     = DT_DMAS_CELL_BY_NAME(DT_NODELABEL(scan_adc), 
                                              adc_dma, slot),
};

DEVICE_DT_DEFINE(DT_NODELABEL(scan_adc),
                 _scan_cont_adc_init,
                 NULL,
                 NULL,
                 &scan_config,
                 POST_KERNEL,
                 CONFIG_APPLICATION_INIT_PRIORITY,
                 &driver_api);

/***************************************************************************************************
 *                                           END OF FILE
 **************************************************************************************************/