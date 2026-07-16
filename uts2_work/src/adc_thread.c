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

static uint32_t dma_buffer[COMBINATIONS_COUNT] __attribute__((aligned(32)));
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
        dma_buffer[i] = bsrr_val;
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
    sys_cache_data_flush_range(dma_buffer, sizeof(dma_buffer));

    // 2. Инициализация DMA
    const struct device *dma_dev = DEVICE_DT_GET(DT_DMAS_CTLR(DT_PATH(zephyr_user)));
    if (!device_is_ready(dma_dev)) 
   if (!device_is_ready(dma_dev)) {
        printk("[ERR] DMA controller not ready!\n");
        return -ENODEV;
    }

    uint32_t dma_channel = DT_DMAS_CELL_BY_NAME(DT_PATH(zephyr_user), seq_dma, channel);
    uint32_t dma_slot = DT_DMAS_CELL_BY_NAME(DT_PATH(zephyr_user), seq_dma, slot);

    uint32_t adc_dma_channel = DT_DMAS_CELL_BY_NAME(DT_PATH(zephyr_user), adc_dma, channel);
    uint32_t adc_dma_slot = DT_DMAS_CELL_BY_NAME(DT_PATH(zephyr_user), adc_dma, slot);

    printk("[DMA] Channel: %d, Slot: %d\n", dma_channel, dma_slot);

     struct dma_block_config dma_block_cfg = {0}; 
     
        dma_block_cfg.source_address     = (uint32_t)dma_buffer;
        dma_block_cfg.dest_address       = MUX_AIN_GPIO_BSRR_ADDR;
        dma_block_cfg.block_size         = sizeof(dma_buffer);
        dma_block_cfg.source_addr_adj    = DMA_ADDR_ADJ_INCREMENT;
        dma_block_cfg.dest_addr_adj      = DMA_ADDR_ADJ_NO_CHANGE;
        dma_block_cfg.source_reload_en = 1;
        dma_block_cfg.dest_reload_en = 1;
    

     struct dma_config dma_cfg = {0}; 
     
        dma_cfg .dma_slot           = dma_slot,
        dma_cfg .channel_direction  = MEMORY_TO_PERIPHERAL,
        dma_cfg .source_data_size   = 4,
        dma_cfg .dest_data_size     = 4,
        dma_cfg.source_burst_length = 4; 
        dma_cfg.dest_burst_length = 4;   
        dma_cfg .block_count        = 1,
        dma_cfg .head_block         = &dma_block_cfg,
    dma_cfg.dma_callback = dma_callback;
    dma_cfg.complete_callback_en = true;
   
    

    ret = dma_config(dma_dev, dma_channel , &dma_cfg);
      if (ret < 0) {
        printk("[ERR] Failed to configure DMA! Error code: %d\n", ret);
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


// 3. Настройка таймера через Counter API
 // 5. Настройка таймера через Counter API
    const struct device *counter_dev = DEVICE_DT_GET(DT_CHILD(SEQ_TIMER_NODE, counter));
    if (!device_is_ready(counter_dev)) {
        printk("[ERR] Timer device not ready!\n");
        return -ENODEV;
    }

    uint32_t freq = counter_get_frequency(counter_dev);
    printk("[TIMER] Timer frequency: %u Hz\n", freq);
    
    struct counter_top_cfg top_cfg = {
        .ticks = freq, // Шаг перебора - 1 секунда
        .callback = NULL,
        .user_data = NULL,
        .flags = 0,
    };

    ret = counter_set_top_value(counter_dev, &top_cfg);
    if (ret < 0) {
        printk("[ERR] Failed to set timer top value! Code: %d\n", ret);
        return ret;
    }

   // 6. Разрешаем таймеру генерировать DMA-запросы (Update DMA Request Enable)
    volatile uint32_t *tim_dier = (volatile uint32_t *)SEQ_TIMER_DIER;
    *tim_dier |= TIM_DIER_UDE;
    printk("[TIMER] DMA trigger activated (DIER: 0x%08X)\n", *tim_dier);

    // 7. Запуск DMA
    ret = dma_start(dma_dev, dma_channel);
    if (ret < 0) {
        printk("[ERR] Failed to start DMA! Code: %d\n", ret);
        return ret;
    }
    printk("[DMA] Transfer started, waiting for trigger.\n");

    // 8. Запуск таймера
    ret = counter_start(counter_dev);
    if (ret < 0) {
        printk("[ERR] Failed to start timer! Code: %d\n", ret);
        return ret;
    }
    printk("[TIMER] Timer started.\n");

    return 0;
}