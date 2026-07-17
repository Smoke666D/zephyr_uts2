#include <zephyr/kernel.h>

#include <zephyr/drivers/gpio.h>
#include <stm32h7xx_ll_tim.h>
#include <stm32h7xx_ll_dma.h>
#include <stm32h7xx_ll_adc.h>
#include <stm32h7xx_ll_gpio.h>
#include <stm32h7xx_ll_rcc.h>
#include <stm32h7xx_ll_bus.h>
#include <zephyr/cache.h>
#include <zephyr/drivers/adc.h>
#include "adc_thread.h"
#include <zephyr/cache.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/counter.h>
#include "board_define.h"
#include <zephyr/drivers/pinctrl.h>

#include <stm32_ll_tim.h>
#include <stm32_ll_adc.h>
#include <stm32_ll_bus.h>

#define ADC_CHANNELS_COUNT 2 /* Опрашиваем 2 внутренних канала: Температура и VREFINT */

// Макросы для получения адреса регистра BSSR
#define MUX_AIN_GPIO_BSRR_ADDR (DT_REG_ADDR(DT_NODELABEL(MUX_AIN_GPIO_PORT_NODELABEL)) + 0x18)


#define SEQ_TIMER_NODE     DT_NODELABEL(SEQ_TIMER_NODELABEL)
#define SEQ_TIMER_BASE     DT_REG_ADDR(SEQ_TIMER_NODE)
#define SEQ_TIMER_DIER     (SEQ_TIMER_BASE + 0x0C)
#define TIM_DIER_UDE       (1 << 8)
/* 
 * Избавляемся от DT_CHILD. 
 * Прямой расчет адресов и узла счетчика по ярлыкам.
 */
#define SEQ_TIMER_NODE     DT_NODELABEL(SEQ_TIMER_NODELABEL)
#define SEQ_TIMER_BASE     DT_REG_ADDR(SEQ_TIMER_NODE)
#define SEQ_TIMER_DIER     (SEQ_TIMER_BASE + 0x0C)
#define TIM_DIER_UDE       (1 << 8)

PINCTRL_DT_DEFINE(DT_NODELABEL(seq_pins));

static void dma_callback(const struct device *dev, void *user_data, uint32_t channel, int status)
{
    // Заглушка, прерывание завершения передачи DMA
}

static uint32_t gpio_dma_buffer[COMBINATIONS_COUNT] __attribute__((aligned(32)));
/* 
 * Буфер результатов АЦП3.
 * Атрибут __nocache принудительно размещает массив в некэшируемой RAM.
 * Выравнивание по-прежнему оставляем для порядка, но для MPU это уже не критично.
 */
static uint32_t adc_raw_buffer[COMBINATIONS_COUNT * ADC_CHANNELS_COUNT] __nocache __attribute__((aligned(32)));

/** 
* @brief Функция подготовки буфера DMA
* @details Для управления пинами по DMA используется регистр bssr. Половина регистра  отвечает
* за установку порта в 1, вторая половина за сброс в ноль. Поэтому для перебора всех значений
* на портах, на нужно сформировать соответствующие маски, где нужные биты сбрасываются и устанавливаются.
*/
static void fill_combinations_buffer(void)
{
    const uint32_t pins[3] = {MUX_AO_PIN, MUX_A1_PIN, MUX_A2_PIN}; 

    for (int i = 0; i < COMBINATIONS_COUNT; i++) 
    {
        uint32_t bsrr_val = 0;
        for (int bit = 0; bit < 3; bit++) {
            if (i & (1 << bit)) {
                bsrr_val |= (1 << pins[bit]);
            } else {
                bsrr_val |= (1 << (pins[bit] + 16));
            }
        }
        gpio_dma_buffer[i] = bsrr_val;
    }
}

int StartDmaGPIO(void)
{
    int ret;

 // 2. ВРУЧНУЮ ПРИМЕНЯЕМ НАСТРОЙКИ PINCTRL В СИ-КОДЕ!
    // Получаем ссылку на pinctrl-структуру и записываем конфигурацию в регистры STM32.
    // Это настроит пины на MODER (Выход), OTYPER (Push-Pull), OSPEEDR (Very High Speed) и ODR (Output Low)


    const struct pinctrl_dev_config *pcfg = PINCTRL_DT_DEV_CONFIG_GET(DT_NODELABEL(seq_pins));
    ret = pinctrl_apply_state(pcfg, PINCTRL_STATE_DEFAULT);
    if (ret < 0) {
        printk("[ERR] Failed to apply pinctrl state! Code: %d\n", ret);
        return ret;
    }
     
    // Подготовка DMA буффера и запись его из D-Cache в SRAM
    fill_combinations_buffer();
    sys_cache_data_flush_range(gpio_dma_buffer, sizeof(gpio_dma_buffer));

    // 2. Инициализация DMA
    const struct device *dma_dev = DEVICE_DT_GET(DT_DMAS_CTLR(DT_PATH(zephyr_user)));
    if (!device_is_ready(dma_dev)) 
   if (!device_is_ready(dma_dev)) {
        printk("[ERR] DMA controller not ready!\n");
        return -ENODEV;
    }

   uint32_t gpio_dma_channel = DT_DMAS_CELL_BY_NAME(DT_PATH(zephyr_user), seq_dma, channel);
uint32_t gpio_dma_slot = DT_DMAS_CELL_BY_NAME(DT_PATH(zephyr_user), seq_dma, slot);

uint32_t adc_dma_channel = DT_DMAS_CELL_BY_NAME(DT_PATH(zephyr_user), adc_dma, channel);
uint32_t adc_dma_slot = DT_DMAS_CELL_BY_NAME(DT_PATH(zephyr_user), adc_dma, slot);

   

     struct dma_block_config gpio_block_cfg = {0}; 
     
        gpio_block_cfg.source_address     = (uint32_t)gpio_dma_buffer;
        gpio_block_cfg.dest_address       = MUX_AIN_GPIO_BSRR_ADDR;
        gpio_block_cfg.block_size         = sizeof(gpio_dma_buffer);
        gpio_block_cfg.source_addr_adj    = DMA_ADDR_ADJ_INCREMENT;
        gpio_block_cfg.dest_addr_adj      = DMA_ADDR_ADJ_NO_CHANGE;
        gpio_block_cfg.source_reload_en = 1;
        gpio_block_cfg.dest_reload_en = 1;
    

     struct dma_config gpio_dma_cfg = {0}; 
     
        gpio_dma_cfg .dma_slot           = gpio_dma_slot;
        gpio_dma_cfg.channel_direction  = MEMORY_TO_PERIPHERAL;
        gpio_dma_cfg.source_data_size   = 4;
        gpio_dma_cfg.dest_data_size     = 4;
        gpio_dma_cfg.source_burst_length = 4; 
        gpio_dma_cfg.dest_burst_length = 4;   
        gpio_dma_cfg.block_count        = 1;
        gpio_dma_cfg.head_block         = &gpio_block_cfg;
    gpio_dma_cfg.dma_callback = dma_callback;
    gpio_dma_cfg.complete_callback_en = true;
   
    

 ret = dma_config(dma_dev, gpio_dma_channel, &gpio_dma_cfg);
    if (ret < 0) {
        printk("[ERR] Failed to configure GPIO DMA! Code: %d\n", ret);
        return ret;
    }
       // ---- Конфигурация DMA 2: ADC3 Scan Buffer ----
    struct dma_block_config adc_block_cfg = {0};
    adc_block_cfg.source_address = (uint32_t)&(ADC3->DR); // Регистр данных ADC3
    adc_block_cfg.dest_address = (uint32_t)adc_raw_buffer;
    adc_block_cfg.block_size = sizeof(adc_raw_buffer);
    adc_block_cfg.source_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;
    adc_block_cfg.dest_addr_adj = DMA_ADDR_ADJ_INCREMENT;
    adc_block_cfg.source_reload_en = 1;
    adc_block_cfg.dest_reload_en = 1;

    struct dma_config adc_dma_cfg = {0};
    adc_dma_cfg.dma_slot = adc_dma_slot;
    adc_dma_cfg.channel_direction = PERIPHERAL_TO_MEMORY;
    adc_dma_cfg.source_data_size = 4;
    adc_dma_cfg.dest_data_size = 4;
    adc_dma_cfg.block_count = 1;
    adc_dma_cfg.head_block = &adc_block_cfg;
    adc_dma_cfg.dma_callback = dma_callback;
    adc_dma_cfg.complete_callback_en = true;

    ret = dma_config(dma_dev, adc_dma_channel, &adc_dma_cfg);
    if (ret < 0) {
        printk("[ERR] Failed to configure ADC DMA! Code: %d\n", ret);
        return ret;
    }
    printk("[DMA] Both DMA channels configured for TIM3 and ADC3 successfully.\n");


   // 3. Тонкая настройка Таймера (TIM3)
    TIM_TypeDef *tim3 = (TIM_TypeDef *)SEQ_TIMER_BASE;
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM3);

    LL_TIM_SetPrescaler(tim3, 10000 - 1);
    LL_TIM_SetAutoReload(tim3, 27500 - 1); // 1 секунда переполнения

    // PWM Mode 1 и Settling time (20% задержка перед АЦП)
    LL_TIM_OC_SetMode(tim3, LL_TIM_CHANNEL_CH1, LL_TIM_OCMODE_PWM1);
    LL_TIM_OC_SetCompareCH1(tim3, 5500);
    LL_TIM_CC_EnableChannel(tim3, LL_TIM_CHANNEL_CH1);

    // Событие Compare Match 1 выводим на TRGO
    LL_TIM_SetTriggerOutput(tim3, LL_TIM_TRGO_OC1REF);
    LL_TIM_EnableDMAReq_UPDATE(tim3);
    printk("[TIMER] TIM3 hardware sync configured successfully.\n");

    // 4. Настройка АЦП3 (ADC3) через LL API
    // Включаем тактирование шины AHB4 для ADC3 (АЦП3 находится в домене D3)
    LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_ADC3);

    // Выводим АЦП3 из режима глубокого энергосбережения
    if (LL_ADC_IsDeepPowerDownEnabled(ADC3) != 0UL) {
        LL_ADC_DisableDeepPowerDown(ADC3);
    }

    // Включаем внутренний стабилизатор напряжения АЦП3 и ждем его стабилизации
    if (LL_ADC_IsInternalRegulatorEnabled(ADC3) == 0UL) {
        LL_ADC_EnableInternalRegulator(ADC3);
        k_busy_wait(20); // Пауза ~20 мкс для стабилизации LDO
    }

    // Настраиваем тактовую частоту для блока ADC3
    LL_ADC_SetCommonClock(ADC3_COMMON, LL_ADC_CLOCK_ASYNC_DIV2);

    // Активируем внутренние каналы (Температурный датчик и ИОН VREFINT) в общем блоке АЦП3
    LL_ADC_SetCommonPathInternalCh(ADC3_COMMON, LL_ADC_PATH_INTERNAL_TEMPSENSOR | LL_ADC_PATH_INTERNAL_VREFINT);

    // Настраиваем АЦП3 на внешний триггер от TIM3_TRGO
    LL_ADC_REG_SetTriggerSource(ADC3, LL_ADC_REG_TRIG_EXT_TIM3_TRGO);
    LL_ADC_REG_SetTriggerEdge(ADC3, LL_ADC_REG_TRIG_EXT_RISING);

    // Разрешаем циклическую передачу результатов АЦП3 через DMA
    LL_ADC_REG_SetDataTransferMode(ADC3, LL_ADC_REG_DMA_TRANSFER_UNLIMITED);

    // Настраиваем сканирующий режим (Scan Mode) для последовательного опроса 2 каналов
    LL_ADC_REG_SetSequencerLength(ADC3, LL_ADC_REG_SEQ_SCAN_ENABLE_2RANKS);
    
    // Назначаем каналы в последовательность опроса: Rank 1 - Temp, Rank 2 - VREFINT
    LL_ADC_REG_SetSequencerRanks(ADC3, LL_ADC_REG_RANK_1, LL_ADC_CHANNEL_TEMPSENSOR);
    LL_ADC_REG_SetSequencerRanks(ADC3, LL_ADC_REG_RANK_2, LL_ADC_CHANNEL_VREFINT);

    // Предвыбор каналов (Preselection) для переключения аналоговых ключей STM32H7
    LL_ADC_SetChannelPreSelection(ADC3, LL_ADC_CHANNEL_TEMPSENSOR);
    LL_ADC_SetChannelPreSelection(ADC3, LL_ADC_CHANNEL_VREFINT);

    // Задаем длинное время выборки (810.5 циклов), необходимое для точной работы внутренних датчиков
    LL_ADC_SetChannelSamplingTime(ADC3, LL_ADC_CHANNEL_TEMPSENSOR, LL_ADC_SAMPLINGTIME_810CYCLES_5);
    LL_ADC_SetChannelSamplingTime(ADC3, LL_ADC_CHANNEL_VREFINT, LL_ADC_SAMPLINGTIME_810CYCLES_5);

    LL_ADC_SetChannelSingleDiff(ADC3, LL_ADC_CHANNEL_TEMPSENSOR, LL_ADC_SINGLE_ENDED);
    LL_ADC_SetChannelSingleDiff(ADC3, LL_ADC_CHANNEL_VREFINT, LL_ADC_SINGLE_ENDED);

    // Калибровка АЦП3
    LL_ADC_StartCalibration(ADC3, LL_ADC_CALIB_OFFSET, LL_ADC_SINGLE_ENDED);
    while (LL_ADC_IsCalibrationOnGoing(ADC3)) {
        k_busy_wait(1);
    }

    // Включение АЦП3
    LL_ADC_Enable(ADC3);
    while (!LL_ADC_IsActiveFlag_ADRDY(ADC3)) {
        k_busy_wait(1);
    }
    printk("[ADC] ADC3 internal channels calibrated and ready.\n");

    // 5. Запуск
    ret = dma_start(dma_dev, gpio_dma_channel);
    if (ret < 0) return ret;

    ret = dma_start(dma_dev, adc_dma_channel);
    if (ret < 0) return ret;

    // Включаем регулярную конверсию АЦП в режиме готовности к внешнему триггеру
    LL_ADC_REG_StartConversion(ADC3);
    printk("[SYSTEM] DMA and ADC3 waiting for TIM3 triggers...\n");

    // Запускаем таймер
    LL_TIM_EnableCounter(tim3);
    printk("[SYSTEM] TIM3-ADC3 test loop started successfully!\n");

    return 0;
}

K_THREAD_STACK_DEFINE(stack3, AIN_TASK_STACK_SIZE);

static struct k_thread thread_data;

static void func(void *arg1, void *arg2, void *arg3)
{
    
    printk("Starting ADC3 Real-Time Monitor (No-Cache Mode)...\n");
    k_msleep(2000);
    
    while (1) 
    {
        		
      
        /* 
         * БОЛЬШЕ НЕ НУЖНО вызывать sys_cache_data_invd_range!
         * CPU гарантированно читает данные напрямую из RAM, куда их записал DMA2.
         */

        printk("\n--- New Multiplexer Cycle Measurements ---\n");
        for (int step = 0; step < 8; step++) {
            uint32_t raw_temp = adc_raw_buffer[step * 2 + 0];
            uint32_t raw_vref = adc_raw_buffer[step * 2 + 1];
            if (raw_vref == 0 || raw_temp == 0) {
                printk("Step %d | Data not ready yet (0x0)\n", step);
                continue;
            }
           // Теперь деление на ноль физически невозможно и вызов безопасен
            uint32_t vdda_mv = __LL_ADC_CALC_VREFANALOG_VOLTAGE(raw_vref, LL_ADC_RESOLUTION_12B);
            int32_t temp_c = __LL_ADC_CALC_TEMPERATURE(vdda_mv, raw_temp, LL_ADC_RESOLUTION_12B);

            printk("Step %d | VDDA: %u mV | Core Temp: %d C\n", step, vdda_mv, temp_c);
        }

        k_msleep(3000);

        /* Мигание обрабатывается всегда */
       // handle_blinking();


      
    
}
}

int ain_thread_start(void)
{
   

    
    // Создаем поток для работы с меню
    k_thread_create(&thread_data, stack3, AIN_TASK_STACK_SIZE,
                    func, NULL, NULL, NULL, AIN_TASK_PRIORITY, 0, K_NO_WAIT);


 
	   
    return 0;
}