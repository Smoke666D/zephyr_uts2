


#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/cache.h>

#include <stm32_ll_tim.h>
#include <stm32_ll_adc.h>
#include <stm32_ll_bus.h>
#include <stm32_ll_gpio.h>
#include <stm32_ll_dma.h> 
#include "seq_mux_adc.h" 
#include <zephyr/dt-bindings/dma/stm32_dma.h>

static const uint32_t r1_resistors[] = DT_PROP(SEQ_NODE, resistors_r1);
static const uint32_t r2_resistors[] = DT_PROP(SEQ_NODE, resistors_r2);
static uint32_t gpio_raw_buffer[COMBINATIONS_COUNT] __attribute__((section("SRAM4")));
static uint32_t adc_buffer_a[TOTAL_CHANNELS_COUNT] __nocache;
static uint32_t adc_buffer_b[TOTAL_CHANNELS_COUNT] __nocache;
static uint32_t adc_shadow_buffer[TOTAL_CHANNELS_COUNT];
static struct k_mutex lock;
static struct k_sem data_ready_sem;
static float coefficients[TOTAL_CHANNELS_COUNT];




/* Обработчик завершения DMA-передачи АЦП (Выполняется в контексте прерывания ISR) */
static void dma_callback(const struct device *dev, void *user_data, uint32_t channel, int status)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(user_data);
    ARG_UNUSED(channel);

    if (status >= 0) 
    {
        if (LL_DMA_GetCurrentTargetMem(DMA1, LL_DMA_STREAM_1) == 1)
        {
            memcpy(adc_shadow_buffer, adc_buffer_a, sizeof(adc_shadow_buffer));
        } 
        else 
        {
            memcpy(adc_shadow_buffer, adc_buffer_b, sizeof(adc_shadow_buffer));
        }
        k_sem_give(&data_ready_sem);
    }
}


static int seq_mux_adc_get_channel_value_impl(const struct device *dev, uint8_t channel_idx, uint32_t *val)
{
    ARG_UNUSED(dev);
    
    if (false 
        || channel_idx >= TOTAL_CHANNELS_COUNT 
        || val == NULL
    ) 
    {
        return -EINVAL;
    }

    /* Захватываем мьютекс для CPU-to-CPU потокобезопасности */
    if (k_mutex_lock(&lock, K_MSEC(100)) < 0)
    {
        return -EAGAIN; 
    }

    // Читаем значение из стабильного теневого буфера
    *val = adc_shadow_buffer[channel_idx];

    k_mutex_unlock(&lock);

    return 0;
}

/* Реализация API */
static uint32_t seq_mux_adc_get_raw_value_impl(const struct device *dev, uint8_t step, uint8_t channel)
{
    uint32_t raw_data;
    if (false 
        || step >= COMBINATIONS_COUNT 
        || channel >= ADC_CHANNELS_COUNT
        || seq_mux_adc_get_channel_value_impl(dev,step * ADC_CHANNELS_COUNT + channel,&raw_data) < 0
    )
    {
        return 0;
    }        
    return raw_data;
}

static uint32_t seq_mux_adc_get_vdda_mv_impl(const struct device *dev, uint8_t step)
{    
    uint32_t raw_vref;
    if (false 
        || step >= COMBINATIONS_COUNT
        || seq_mux_adc_get_channel_value_impl(dev,step * ADC_CHANNELS_COUNT + 1,&raw_vref) < 0 
        || raw_vref == 0
    ) 
    {
        return 0; 
    }
    return __LL_ADC_CALC_VREFANALOG_VOLTAGE(raw_vref, LL_ADC_RESOLUTION_12B);
}

static int32_t seq_mux_adc_get_core_temp_impl(const struct device *dev, uint8_t step)
{
    uint32_t raw_temp; 
    uint32_t raw_vref;
    uint32_t vdda_mv;
    if (false 
        || step >= COMBINATIONS_COUNT
        || seq_mux_adc_get_channel_value_impl(dev,step * ADC_CHANNELS_COUNT + 1,&raw_vref) < 0
        || seq_mux_adc_get_channel_value_impl(dev,step * ADC_CHANNELS_COUNT + 0,&raw_temp) < 0
        || raw_vref == 0 
        || raw_temp == 0
    ) 
    {
        return 0; 
    }
    vdda_mv = __LL_ADC_CALC_VREFANALOG_VOLTAGE(raw_vref, LL_ADC_RESOLUTION_12B);
    return __LL_ADC_CALC_TEMPERATURE(vdda_mv, raw_temp, LL_ADC_RESOLUTION_12B);
}


/* 
 * РЕАЛИЗАЦИЯ ПОТОКОБЕЗОПАСНОЙ ФУНКЦИИ ВЫЧИСЛЕНИЯ НАПРЯЖЕНИЯ.
 * Рассчитывает напряжение на входе делителя в милливольтах (mV).
 */
static int seq_mux_adc_get_channel_voltage_impl(const struct device *dev, uint8_t channel_idx, uint32_t *voltage_mv)
{
    ARG_UNUSED(dev);
    uint32_t raw_val = 0;
    

    if (false
        || channel_idx >= TOTAL_CHANNELS_COUNT
        || voltage_mv == NULL
        || seq_mux_adc_get_channel_value_impl(dev, channel_idx, &raw_val) < 0
    ) {
        return -EINVAL;
    }

    // Находим индекс шага мультиплексора (0..7) и индекс аналогового входа (0..4)
    uint8_t step = channel_idx / ADC_CHANNELS_COUNT;
    uint8_t adc_chan = channel_idx % ADC_CHANNELS_COUNT;

    // Вычисляем точное напряжение питания АЦП VDDA на текущем шаге
    uint32_t vdda_mv = seq_mux_adc_get_vdda_mv_impl(dev, step);
    if (vdda_mv == 0) {
        return -EAGAIN;
    }

    // 1. Вычисляем напряжение на физической ножке АЦП (в милливольтах)
    float v_pin_mv = (float)(raw_val * vdda_mv) / 4095.0f;

    // 2. Умножаем его на рассчитанный при загрузке коэффициент делителя
    *voltage_mv = (uint32_t)(v_pin_mv * coefficients[adc_chan]);

    return 0;
}



static void fill_gpio_buffer(uint32_t *buf)
{
    const uint32_t pins[3] = {MUX_PIN_0_NUM, MUX_PIN_1_NUM, MUX_PIN_2_NUM};

    for (int i = 0; i < COMBINATIONS_COUNT; i++) {
        uint8_t state = (i + 1) % COMBINATIONS_COUNT;
        uint32_t bsrr_val = 0;
        for (int bit = 0; bit < 3; bit++) {
            if (state & (1 << bit)) {
                bsrr_val |= (1 << pins[bit]);
            } else {
                bsrr_val |= (1 << (pins[bit] + 16));
            }
        }
        buf[i] = bsrr_val;
    }
}

static inline int _dma_init(const struct seq_mux_adc_config *config, ADC_TypeDef *adc_inst,const struct device *dev)
{
    int ret;

    if (!device_is_ready(config->dma_dev)) return -ENODEV;

    struct dma_block_config gpio_block_cfg = {0}; 
    gpio_block_cfg.source_address = (uint32_t)gpio_raw_buffer;
    gpio_block_cfg.dest_address = SEQ_GPIO_BSRR_ADDR;
    gpio_block_cfg.block_size = COMBINATIONS_COUNT * sizeof(uint32_t);
    gpio_block_cfg.source_addr_adj = DMA_ADDR_ADJ_INCREMENT;
    gpio_block_cfg.dest_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;
    gpio_block_cfg.source_reload_en = 1; 
    gpio_block_cfg.dest_reload_en = 1; 

    struct dma_config gpio_dma_cfg = {0}; 
    gpio_dma_cfg.dma_slot = config->gpio_dma_slot;
    gpio_dma_cfg.channel_direction = MEMORY_TO_PERIPHERAL;
    gpio_dma_cfg.source_data_size = 4;
    gpio_dma_cfg.dest_data_size = 4;
    gpio_dma_cfg.source_burst_length = 4;
    gpio_dma_cfg.dest_burst_length = 4;
    gpio_dma_cfg.channel_priority = 2; // LL_DMA_PRIORITY_HIGH
    gpio_dma_cfg.block_count = 1;
    gpio_dma_cfg.head_block = &gpio_block_cfg;

    ret = dma_config(config->dma_dev, config->gpio_dma_channel, &gpio_dma_cfg);
    if (ret < 0) return ret;

    struct dma_block_config adc_block_cfg = {0};
    adc_block_cfg.source_address = (uint32_t)&(adc_inst->DR);
    adc_block_cfg.dest_address = (uint32_t)adc_buffer_a;
    adc_block_cfg.block_size = TOTAL_CHANNELS_COUNT * sizeof(uint32_t);
    adc_block_cfg.source_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;
    adc_block_cfg.dest_addr_adj = DMA_ADDR_ADJ_INCREMENT;
    adc_block_cfg.source_reload_en = 1;
    adc_block_cfg.dest_reload_en = 1;

    struct dma_config adc_dma_cfg = {0};
    adc_dma_cfg.dma_slot = config->adc_dma_slot;
    adc_dma_cfg.channel_direction = PERIPHERAL_TO_MEMORY;
    adc_dma_cfg.source_data_size = 4;
    adc_dma_cfg.dest_data_size = 4;
    adc_dma_cfg.source_burst_length = 4;
    adc_dma_cfg.dest_burst_length = 4;
    adc_dma_cfg.channel_priority = 3; // LL_DMA_PRIORITY_VERYHIGH
    adc_dma_cfg.block_count = 1;
    adc_dma_cfg.head_block = &adc_block_cfg;

    adc_dma_cfg.dma_callback = dma_callback;
    adc_dma_cfg.user_data = (void *)dev; 
    adc_dma_cfg.complete_callback_en = true;

    ret = dma_config(config->dma_dev, config->adc_dma_channel, &adc_dma_cfg);
    if (ret < 0) return ret;

    LL_DMA_SetMemory1Address(DMA1, LL_DMA_STREAM_1, (uint32_t)adc_buffer_b);
    LL_DMA_EnableDoubleBufferMode(DMA1, LL_DMA_STREAM_1);

    return 0;
}

/* ИСПРАВЛЕНИЕ: Перевели возвращаемый тип функции в void */
static inline void _gpio_init(void)
{

 
    // Настраиваем пин MUX_A0
    GPIO_TypeDef *port0 = (GPIO_TypeDef *)MUX_PIN_0_PORT_BASE;
    LL_GPIO_SetPinMode(port0, 1 << MUX_PIN_0_NUM, LL_GPIO_MODE_OUTPUT);
    LL_GPIO_SetPinOutputType(port0, 1 << MUX_PIN_0_NUM, LL_GPIO_OUTPUT_PUSHPULL);
    LL_GPIO_SetPinSpeed(port0, 1 << MUX_PIN_0_NUM, LL_GPIO_SPEED_FREQ_VERY_HIGH);
    LL_GPIO_SetPinPull(port0, 1 << MUX_PIN_0_NUM, LL_GPIO_PULL_NO);
    LL_GPIO_ResetOutputPin(port0, 1 << MUX_PIN_0_NUM);

    // Настраиваем пин MUX_A1
    GPIO_TypeDef *port1 = (GPIO_TypeDef *)MUX_PIN_1_PORT_BASE;
    LL_GPIO_SetPinMode(port1, 1 << MUX_PIN_1_NUM, LL_GPIO_MODE_OUTPUT);
    LL_GPIO_SetPinOutputType(port1, 1 << MUX_PIN_1_NUM, LL_GPIO_OUTPUT_PUSHPULL);
    LL_GPIO_SetPinSpeed(port1, 1 << MUX_PIN_1_NUM, LL_GPIO_SPEED_FREQ_VERY_HIGH);
    LL_GPIO_SetPinPull(port1, 1 << MUX_PIN_1_NUM, LL_GPIO_PULL_NO);
    LL_GPIO_ResetOutputPin(port1, 1 << MUX_PIN_1_NUM);

    // Настраиваем пин MUX_A2
    GPIO_TypeDef *port2 = (GPIO_TypeDef *)MUX_PIN_2_PORT_BASE;
    LL_GPIO_SetPinMode(port2, 1 << MUX_PIN_2_NUM, LL_GPIO_MODE_OUTPUT);
    LL_GPIO_SetPinOutputType(port2, 1 << MUX_PIN_2_NUM, LL_GPIO_OUTPUT_PUSHPULL);
    LL_GPIO_SetPinSpeed(port2, 1 << MUX_PIN_2_NUM, LL_GPIO_SPEED_FREQ_VERY_HIGH);
    LL_GPIO_SetPinPull(port2, 1 << MUX_PIN_2_NUM, LL_GPIO_PULL_NO);
    LL_GPIO_ResetOutputPin(port2, 1 << MUX_PIN_2_NUM);

    /* Перед первым стартом аппаратно гарантируем 0 на всех управляющих ножках */
    LL_GPIO_ResetOutputPin(port0, 1 << MUX_PIN_0_NUM);
    LL_GPIO_ResetOutputPin(port1, 1 << MUX_PIN_1_NUM);
    LL_GPIO_ResetOutputPin(port2, 1 << MUX_PIN_2_NUM);
}

/* Реализация функции ожидания готовности данных */
static int seq_mux_adc_wait_for_data_impl(const struct device *dev, k_timeout_t timeout)
{
    ARG_UNUSED(dev);
    
    /* 
     * Переводим вызывающий поток в спящий режим на семафоре.
     * Когда сработает прерывание dma_callback, поток мгновенно проснется!
     */
    return k_sem_take(&data_ready_sem, timeout);
}


static int seq_mux_adc_init(const struct device *dev)
{
    const struct seq_mux_adc_config *config = dev->config;
    int ret;

    ADC_TypeDef *adc_inst = (ADC_TypeDef *)SEQ_ADC_BASE;
    TIM_TypeDef *tim_inst = (TIM_TypeDef *)SEQ_TIM_BASE;

    k_mutex_init(&lock);
    k_sem_init(&data_ready_sem, 0, 1);

    for (int i = 0; i < ADC_CHANNELS_COUNT; i++)
    {
        // Формула: K = (R1 + R2) / R2
        coefficients[i] = (float)(r1_resistors[i] + r2_resistors[i]) / (float)r2_resistors[i];
    }

    _gpio_init();

    fill_gpio_buffer(gpio_raw_buffer);
    sys_cache_data_flush_range(gpio_raw_buffer, sizeof(gpio_raw_buffer));

    // Конфигурация DMA
    ret = _dma_init(config, adc_inst, dev);
    if (ret < 0) return ret;

    const struct device *counter_dev = DEVICE_DT_GET(DT_CHILD(SEQ_TIM_NODE, counter));
    if (!device_is_ready(counter_dev)) {
        return -ENODEV;
    }

    uint32_t freq = counter_get_frequency(counter_dev);
    
    uint32_t t1_ticks = (uint32_t)((uint64_t)SEQ_ADC_DURATION_US * freq / 1000000ULL);
    uint32_t t2_ticks = (uint32_t)((uint64_t)SEQ_SWITCH_DURATION_US * freq / 1000000ULL);

    uint32_t arr_value = t1_ticks + t2_ticks - 1;
    uint32_t ccr1_value = t1_ticks;

    LL_TIM_SetPrescaler(tim_inst, 10000 - 1);
    LL_TIM_SetAutoReload(tim_inst, arr_value);
    
    LL_TIM_OC_SetMode(tim_inst, LL_TIM_CHANNEL_CH1, LL_TIM_OCMODE_PWM1);
    LL_TIM_OC_SetCompareCH1(tim_inst, ccr1_value);
    LL_TIM_CC_EnableChannel(tim_inst, LL_TIM_CHANNEL_CH1);

    // Событие UPDATE генерирует триггер TRGO для старта АЦП3
    LL_TIM_SetTriggerOutput(tim_inst, LL_TIM_TRGO_UPDATE);

    // Событие CC1 генерирует запрос DMA 1 для переключения ножек
    LL_TIM_EnableDMAReq_CC1(tim_inst);

    // ---- Включение тактирования выбранного АЦП ----
    if (adc_inst == ADC3) {
        LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_ADC3);
    } else if (adc_inst == ADC1 || adc_inst == ADC2) {
        LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_ADC12);
    }

    if (LL_ADC_IsDeepPowerDownEnabled(adc_inst) != 0UL) {
        LL_ADC_DisableDeepPowerDown(adc_inst);
    }
    if (LL_ADC_IsInternalRegulatorEnabled(adc_inst) == 0UL) {
        LL_ADC_EnableInternalRegulator(adc_inst);
        k_busy_wait(20);
    }



#if DT_SAME_NODE(SEQ_ADC_NODE, DT_NODELABEL(adc3))
    ADC_Common_TypeDef *adc_common = ADC3_COMMON;
#else
    ADC_Common_TypeDef *adc_common = ADC12_COMMON;
#endif

    LL_ADC_SetCommonClock(adc_common, LL_ADC_CLOCK_ASYNC_DIV2);
    LL_ADC_SetCommonPathInternalCh(adc_common, LL_ADC_PATH_INTERNAL_TEMPSENSOR | LL_ADC_PATH_INTERNAL_VREFINT);
    
    LL_ADC_REG_SetTriggerSource(adc_inst, LL_ADC_REG_TRIG_EXT_TIM3_TRGO);
    LL_ADC_REG_SetTriggerEdge(adc_inst, LL_ADC_REG_TRIG_EXT_RISING);
    
    LL_ADC_REG_SetDataTransferMode(adc_inst, LL_ADC_REG_DMA_TRANSFER_UNLIMITED);
    LL_ADC_REG_SetSequencerLength(adc_inst, LL_ADC_REG_SEQ_SCAN_ENABLE_2RANKS);
    LL_ADC_REG_SetSequencerRanks(adc_inst, LL_ADC_REG_RANK_1, LL_ADC_CHANNEL_TEMPSENSOR);
    LL_ADC_REG_SetSequencerRanks(adc_inst, LL_ADC_REG_RANK_2, LL_ADC_CHANNEL_VREFINT);
    LL_ADC_SetChannelPreSelection(adc_inst, LL_ADC_CHANNEL_TEMPSENSOR);
    LL_ADC_SetChannelPreSelection(adc_inst, LL_ADC_CHANNEL_VREFINT);
    LL_ADC_SetChannelSamplingTime(adc_inst, LL_ADC_CHANNEL_TEMPSENSOR, LL_ADC_SAMPLINGTIME_810CYCLES_5);
    LL_ADC_SetChannelSamplingTime(adc_inst, LL_ADC_CHANNEL_VREFINT, LL_ADC_SAMPLINGTIME_810CYCLES_5);
    LL_ADC_SetChannelSingleDiff(adc_inst, LL_ADC_CHANNEL_TEMPSENSOR, LL_ADC_SINGLE_ENDED);
    LL_ADC_SetChannelSingleDiff(adc_inst, LL_ADC_CHANNEL_VREFINT, LL_ADC_SINGLE_ENDED);

    LL_ADC_StartCalibration(adc_inst, LL_ADC_CALIB_OFFSET, LL_ADC_SINGLE_ENDED);
    while (LL_ADC_IsCalibrationOnGoing(adc_inst)) {
        k_busy_wait(1);
    }

    LL_ADC_Enable(adc_inst);
    while (!LL_ADC_IsActiveFlag_ADRDY(adc_inst)) {
        k_busy_wait(1);
    }

    // ---- 7. Запуск ----
    ret = dma_start(config->dma_dev, config->gpio_dma_channel);
    if (ret < 0) return ret;

    ret = dma_start(config->dma_dev, config->adc_dma_channel);
    if (ret < 0) return ret;

    LL_ADC_REG_StartConversion(adc_inst);
    LL_TIM_EnableCounter(tim_inst);

    return 0;
}

/* УДАЛЕНО: Макрос PINCTRL_DT_DEFINE полностью удален */

static const struct seq_mux_adc_config seq_config = {
    .dma_dev = DEVICE_DT_GET(DT_DMAS_CTLR(DT_NODELABEL(my_sequencer))),
    .gpio_dma_channel = DT_DMAS_CELL_BY_NAME(DT_NODELABEL(my_sequencer), seq_dma, channel),
    .gpio_dma_slot = DT_DMAS_CELL_BY_NAME(DT_NODELABEL(my_sequencer), seq_dma, slot),
    .adc_dma_channel = DT_DMAS_CELL_BY_NAME(DT_NODELABEL(my_sequencer), adc_dma, channel),
    .adc_dma_slot = DT_DMAS_CELL_BY_NAME(DT_NODELABEL(my_sequencer), adc_dma, slot),
    /* УДАЛЕНО: Инициализация pcfg полностью удалена */
};

static const struct seq_mux_adc_api driver_api = {
    .get_raw_value = seq_mux_adc_get_raw_value_impl,
    .get_vdda_mv = seq_mux_adc_get_vdda_mv_impl,
    .get_core_temp = seq_mux_adc_get_core_temp_impl,
    .get_channel_value = seq_mux_adc_get_channel_value_impl,
    .get_channel_voltage = seq_mux_adc_get_channel_voltage_impl,
    .wait_for_data = seq_mux_adc_wait_for_data_impl,
};

DEVICE_DT_DEFINE(DT_NODELABEL(my_sequencer),
                 seq_mux_adc_init,
                 NULL,
                 NULL,
                 &seq_config,
                 POST_KERNEL,
                 CONFIG_APPLICATION_INIT_PRIORITY,
                 &driver_api);