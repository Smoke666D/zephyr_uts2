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

/* Подключаем публичный заголовочный файл нашего драйвера */
#include <seq_mux_adc.h>


/* Получаем указатель на наш прибор из дерева устройств */
static const struct device *const seq_dev = DEVICE_DT_GET(DT_NODELABEL(my_sequencer));

K_THREAD_STACK_DEFINE(stack3, AIN_TASK_STACK_SIZE);

static struct k_thread thread_data;

static void func(void *arg1, void *arg2, void *arg3)
{
    
  // Проверяем готовность драйвера перед началом работы
    if (!device_is_ready(seq_dev)) {
        printk("Sequencer driver is not ready!");
        return;
    }

    /* Извлекаем интерфейс высокоуровневого API нашего драйвера */
    struct seq_mux_adc_api *api = (struct seq_mux_adc_api *)seq_dev->api;
    
    /* Запрашиваем у драйвера некэшируемый буфер данных АЦП */
    uint32_t *adc_raw_buffer = api->get_raw_value ? NULL : NULL; // Для информации (внутри API есть функции прямого доступа)

    
    while (1) 
    {
        		
      
        for (int step = 0; step < 8; step++) {
            // Запрашиваем у драйвера готовые физические величины
            uint32_t vdda_mv = api->get_vdda_mv(seq_dev, step);
            int32_t temp_c = api->get_core_temp(seq_dev, step);

            /* 
             * Защита от деления на ноль. 
             * Если данные АЦП еще не готовы, драйвер вернет 0.
             * Мы мягко пишем предупреждение без зависания и падения.
             */
            if (vdda_mv == 0) {
                printk("Step %d | Data not ready yet\r\n", step);
                continue;
            }

            // Выводим физические величины
            printk("Step %d | VDDA: %u mV | Core Temp: %d C\r\n", step, vdda_mv, temp_c);
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