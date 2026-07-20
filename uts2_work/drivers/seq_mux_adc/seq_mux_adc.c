#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/cache.h>

#include <stm32_ll_tim.h>
#include <stm32_ll_adc.h>
#include <stm32_ll_bus.h>
#include <stm32_ll_gpio.h>

#include "seq_mux_adc.h" 
#include <zephyr/dt-bindings/dma/stm32_dma.h>

struct seq_mux_adc_config {
    const struct device *dma_dev;
    uint32_t gpio_dma_channel;
    uint32_t gpio_dma_slot;
    uint32_t adc_dma_channel;
    uint32_t adc_dma_slot;
    /* УДАЛЕНО: pcfg больше не требуется */
};

#define SEQ_SRAM4_GPIO_BUFFER_ADDR 0x38000000


/* Буфер результатов АЦП — в SRAM1/2/3 (домен D2) */
static uint32_t adc_raw_buffer[TOTAL_CHANNELS_COUNT]  __nocache;
static uint32_t gpio_raw_buffer[COMBINATIONS_COUNT] __attribute__((section("SRAM4")));

/* ШАГ 2: В структуре храним указатели на эти два внешних массива */
struct seq_mux_adc_data {
    struct k_mutex lock;             /* Мьютекс для CPU-to-CPU потокобезопасности */
    uint32_t *gpio_buffer;
    uint32_t *adc_buffer;
    uint32_t adc_shadow_buffer[TOTAL_CHANNELS_COUNT]; /* Теневой буфер (отсюда читает процессор) */
};;


/* Обработчик завершения DMA-передачи АЦП (Выполняется в контексте прерывания ISR) */
static void dma_callback(const struct device *dev, void *user_data, uint32_t channel, int status)
{
    // Восстанавливаем указатель на наше устройство из параметров пользователя
    const struct device *seq_dev = user_data;
    if (seq_dev == NULL) {
        return;
    }

    struct seq_mux_adc_data *data = seq_dev->data;

    if (status >= 0) {
        /* 
         * Мгновенно и атомарно копируем свежие данные из активного буфера АЦП 
         * в стабильный теневой буфер. Поскольку прерывание прерывает потоки,
         * гонка за данные с DMA полностью исключена.
         */
        memcpy(data->adc_shadow_buffer, data->adc_buffer, TOTAL_CHANNELS_COUNT * sizeof(uint32_t));
    }
}


/* Реализация API */
static uint32_t seq_mux_adc_get_raw_value_impl(const struct device *dev, uint8_t step, uint8_t channel)
{
    struct seq_mux_adc_data *data = dev->data;
    if (step >= COMBINATIONS_COUNT || channel >= ADC_CHANNELS_COUNT) {
        return 0;
    }
    return data->adc_buffer[step * ADC_CHANNELS_COUNT + channel];
}

static uint32_t seq_mux_adc_get_vdda_mv_impl(const struct device *dev, uint8_t step)
{
    struct seq_mux_adc_data *data = dev->data;
    if (step >= COMBINATIONS_COUNT) return 0;
    uint32_t raw_vref = data->adc_buffer[step * ADC_CHANNELS_COUNT + 1];
    if (raw_vref == 0) return 0; 
    return __LL_ADC_CALC_VREFANALOG_VOLTAGE(raw_vref, LL_ADC_RESOLUTION_12B);
}

static int32_t seq_mux_adc_get_core_temp_impl(const struct device *dev, uint8_t step)
{
    struct seq_mux_adc_data *data = dev->data;
    if (step >= COMBINATIONS_COUNT) return 0;
    uint32_t raw_temp = data->adc_buffer[step * ADC_CHANNELS_COUNT + 0];
    uint32_t raw_vref = data->adc_buffer[step * ADC_CHANNELS_COUNT + 1];
    if (raw_vref == 0 || raw_temp == 0) return 0; 
    uint32_t vdda_mv = __LL_ADC_CALC_VREFANALOG_VOLTAGE(raw_vref, LL_ADC_RESOLUTION_12B);
    return __LL_ADC_CALC_TEMPERATURE(vdda_mv, raw_temp, LL_ADC_RESOLUTION_12B);
}

static const struct seq_mux_adc_api driver_api = {
    .get_raw_value = seq_mux_adc_get_raw_value_impl,
    .get_vdda_mv = seq_mux_adc_get_vdda_mv_impl,
    .get_core_temp = seq_mux_adc_get_core_temp_impl,
    .get_channel_value = seq_mux_adc_get_channel_value_impl, /* Регистрируем новую функцию в API */
};

/* 
 * РЕАЛИЗАЦИЯ ПОТОКОБЕЗОПАСНОЙ ФУНКЦИИ ИНТЕРФЕЙСА API.
 * Функция принимает индекс канала от 0 до 15 и защищает доступ мьютексом.
 */
static int seq_mux_adc_get_channel_value_impl(const struct device *dev, uint8_t channel_idx, uint32_t *val)
{
    struct seq_mux_adc_data *data = dev->data;
    int ret;

    if (channel_idx >= TOTAL_CHANNELS_COUNT || val == NULL) {
        return -EINVAL;
    }

    /* 
     * Захватываем мьютекс. Если другой поток сейчас читает данные,
     * текущий поток безопасно засыпает, уступая процессор ОС.
     * Ставим таймаут ожидания 100 мс для защиты от зависаний.
     */
    ret = k_mutex_lock(&data->lock, K_MSEC(100));
    if (ret < 0) {
        return -EAGAIN; // Ошибка таймаута блокировки
    }

    // Читаем значение из стабильного теневого буфера
    *val = data->adc_shadow_buffer[channel_idx];

    // Освобождаем мьютекс, давая дорогу другим потокам
    k_mutex_unlock(&data->lock);

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

static inline int _dma_init(const struct seq_mux_adc_config *config, struct seq_mux_adc_data *data, ADC_TypeDef *adc_inst)
{
    int ret;

    if (!device_is_ready(config->dma_dev)) return -ENODEV;

    struct dma_block_config gpio_block_cfg = {0}; 
    gpio_block_cfg.source_address = (uint32_t)data->gpio_buffer;
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
    adc_block_cfg.dest_address = (uint32_t)data->adc_buffer;
    adc_block_cfg.block_size = COMBINATIONS_COUNT * ADC_CHANNELS_COUNT * sizeof(uint32_t);
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
    adc_dma_cfg.error_callback_en = true;

    ret = dma_config(config->dma_dev, config->adc_dma_channel, &adc_dma_cfg);
    if (ret < 0) return ret;

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

static int seq_mux_adc_init(const struct device *dev)
{
    const struct seq_mux_adc_config *config = dev->config;
    struct seq_mux_adc_data *data = dev->data;
    int ret;

    ADC_TypeDef *adc_inst = (ADC_TypeDef *)SEQ_ADC_BASE;
    TIM_TypeDef *tim_inst = (TIM_TypeDef *)SEQ_TIM_BASE;

  
    /* ИСПРАВЛЕНИЕ: Обязательно вызываем инициализацию пинов мультиплексора */
    _gpio_init();

    fill_gpio_buffer(data->gpio_buffer);
   

    // Конфигурация DMA
    ret = _dma_init(config, data, adc_inst);
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


static struct seq_mux_adc_data seq_data = {
    .gpio_buffer = gpio_raw_buffer,
    .adc_buffer = adc_raw_buffer,
};

DEVICE_DT_DEFINE(DT_NODELABEL(my_sequencer),
                 seq_mux_adc_init,
                 NULL,
                 &seq_data,
                 &seq_config,
                 POST_KERNEL,
                 CONFIG_APPLICATION_INIT_PRIORITY,
                 &driver_api);