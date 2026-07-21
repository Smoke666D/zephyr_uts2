#include <zephyr/kernel.h>
#include "adc_thread.h"
#include <zephyr/logging/log.h>
#include "system_data_bus.h"


/* Подключаем публичный заголовочный файл нашего драйвера */
#include <seq_mux_adc.h>


/* 
 * ОПРЕДЕЛЯЕМ КАНАЛ.
 * Поскольку adc_monitor_sub объявлен extern в adc_zbus.h, 
 * макрос ZBUS_OBSERVERS скомпилируется успешно.
 */
ZBUS_CHAN_DEFINE(adc_data_chan,
                 struct adc_data_msg,
                 NULL, /* Валидатор */
                 NULL, /* Пользовательские данные */
                 ZBUS_OBSERVERS(), /* Список получателей */
                 ZBUS_MSG_INIT(0) /* Инициализация нулями */
);

LOG_MODULE_REGISTER(seq_mux_adc_drv, LOG_LEVEL_INF);

/* Получаем указатель на наш прибор из дерева устройств */
static const struct device *const seq_dev = DEVICE_DT_GET(DT_NODELABEL(my_sequencer));

static uint8_t __attribute__((__section__("DTCM"))) stack3[AIN_TASK_STACK_SIZE];


static struct k_thread thread_data;

static void func(void *arg1, void *arg2, void *arg3)
{
    
  // Проверяем готовность драйвера перед началом работы
    if (!device_is_ready(seq_dev)) {
         LOG_ERR("Sequencer driver is not ready!");
        return;
    }

    /* Извлекаем интерфейс высокоуровневого API нашего драйвера */
    struct seq_mux_adc_api *api = (struct seq_mux_adc_api *)seq_dev->api;
    struct adc_data_msg msg; 
    
    LOG_INF("Starting ADC3 Real-Time Publisher Thread...");
    k_sleep(K_MSEC(2000));
    
    while (1) 
    {
         int ret = api->wait_for_data(seq_dev, K_FOREVER);
        if (ret < 0) {
            LOG_ERR("Failed to wait for data! Error: %d", ret);
            continue;
        }

        // Заполняем структуру физическими величинами
        for (int step = 0; step < 8; step++) {
            msg.vdda_mv = api->get_vdda_mv(seq_dev, step);
            msg.core_temp = api->get_core_temp(seq_dev, step);
            
            api->get_channel_voltage(seq_dev, step * 2 + 0, &msg.channels_mv[step * 2 + 0]);
            api->get_channel_voltage(seq_dev, step * 2 + 1, &msg.channels_mv[step * 2 + 1]);
        }

        // Публикуем готовые данные в Zbus-канал
         zbus_chan_pub(&adc_data_chan, &msg,K_NO_WAIT);
        
        
    }
    
}

int ain_thread_start(void)
{
   

    
    // Создаем поток для работы с меню
    k_thread_create(&thread_data, (k_thread_stack_t *)stack3, AIN_TASK_STACK_SIZE,
                    func, NULL, NULL, NULL, AIN_TASK_PRIORITY, 0, K_NO_WAIT);


 
	   
    return 0;
}