#include <zephyr/kernel.h>
#include "adc_thread.h"
#include <zephyr/logging/log.h>
#include "system_data_bus.h"
#include "global_params.h"

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

static const AIN_MUX_CHANNEL_NUMBER param_to_adc_map[] = {
    [AIN_AO1]        = AO1,
    [AIN_AO7]        = AO7,
    [AIN_AO13]       = AO13,
    [AIN_AVsense1]   = AVsense1,
    [AIN_AO2]        = AO2,
    [AIN_AO8]        = AO8,
    [AIN_AO14]       = AO14,
    [AIN_AVsense2]   = AVsense2,
    [AIN_AO3]        = AO3,
    [AIN_AO9]        = AO9,
    [AIN_AO15]       = AO15,
    [AIN_AVsense3]   = AVsense3,
    [AIN_AO4]        = AO4,
    [AIN_AO10]       = AO10,
    [AIN_AO16]       = AO16,
    [AIN_AVsense4]   = AVsense4,
    [AIN_AO5]        = AO5,
    [AIN_AO11]       = AO11,
    [AIN_AO17]       = AO17,
    [AIN_AVsense5]   = AVsense5,
    [AIN_AO6]        = AO6,
    [AIN_AO12]       = AO12,
    [AIN_AO18]       = AO18,
    [AIN_AVsense6]   = AVsense6,
    [AIN_DA11_test1] = DA11_test1,
    [AIN_DA20_test1] = DA20_test1,
    [AIN_DA33_test1] = DA33_test1,
    [AIN_DA44_test1] = DA44_test1,
    [AIN_DA11_test2] = DA11_test2,
    [AIN_DA20_test2] = DA20_test2,
    [AIN_DA33_test2] = DA33_test2,
    [AIN_DA44_test2] = DA44_test2,
};

static int adc_param_get(PARAM_ID id, PARAM_VAL *val)
{
    /* Проверяем, что запрашиваемый ID входит в диапазон наших аналоговых каналов */
    if (id >= AIN_AO1 && id <= AIN_DA44_test2) {
        struct adc_data_msg adc_msg;
        
        /* Читаем последние кэшированные данные из канала Zbus */
        int err = zbus_chan_read(&adc_data_chan, &adc_msg, K_NO_WAIT);
        if (err == 0) {
            /* Находим физический канал АЦП по логическому ID */
            AIN_MUX_CHANNEL_NUMBER adc_chan = param_to_adc_map[id];
            
            /* Преобразуем милливольты в Вольты и сохраняем в вещественное поле */
            val->value.real = (double)adc_msg.channels_mv[adc_chan];
        }
        return err;
    }
    
    return -ENOTSUP;
}

PARAM_ROUTE_RO(AIN_AO1,        adc_param_get);
PARAM_ROUTE_RO(AIN_AO7,        adc_param_get);
PARAM_ROUTE_RO(AIN_AO13,       adc_param_get);
PARAM_ROUTE_RO(AIN_AVsense1,   adc_param_get);
PARAM_ROUTE_RO(AIN_AO2,        adc_param_get);
PARAM_ROUTE_RO(AIN_AO8,        adc_param_get);
PARAM_ROUTE_RO(AIN_AO14,       adc_param_get);
PARAM_ROUTE_RO(AIN_AVsense2,   adc_param_get);
PARAM_ROUTE_RO(AIN_AO3,        adc_param_get);
PARAM_ROUTE_RO(AIN_AO9,        adc_param_get);
PARAM_ROUTE_RO(AIN_AO15,       adc_param_get);
PARAM_ROUTE_RO(AIN_AVsense3,   adc_param_get);
PARAM_ROUTE_RO(AIN_AO4,        adc_param_get);
PARAM_ROUTE_RO(AIN_AO10,       adc_param_get);
PARAM_ROUTE_RO(AIN_AO16,       adc_param_get);
PARAM_ROUTE_RO(AIN_AVsense4,   adc_param_get);
PARAM_ROUTE_RO(AIN_AO5,        adc_param_get);
PARAM_ROUTE_RO(AIN_AO11,       adc_param_get);
PARAM_ROUTE_RO(AIN_AO17,       adc_param_get);
PARAM_ROUTE_RO(AIN_AVsense5,   adc_param_get);
PARAM_ROUTE_RO(AIN_AO6,        adc_param_get);
PARAM_ROUTE_RO(AIN_AO12,       adc_param_get);
PARAM_ROUTE_RO(AIN_AO18,       adc_param_get);
PARAM_ROUTE_RO(AIN_AVsense6,   adc_param_get);
PARAM_ROUTE_RO(AIN_DA11_test1, adc_param_get);
PARAM_ROUTE_RO(AIN_DA20_test1, adc_param_get);
PARAM_ROUTE_RO(AIN_DA33_test1, adc_param_get);
PARAM_ROUTE_RO(AIN_DA44_test1, adc_param_get);
PARAM_ROUTE_RO(AIN_DA11_test2, adc_param_get);
PARAM_ROUTE_RO(AIN_DA20_test2, adc_param_get);
PARAM_ROUTE_RO(AIN_DA33_test2, adc_param_get);
PARAM_ROUTE_RO(AIN_DA44_test2, adc_param_get);




LOG_MODULE_REGISTER(seq_mux_adc_drv, LOG_LEVEL_INF);

/* Получаем указатель на наш прибор из дерева устройств */
static const struct device *const seq_dev = DEVICE_DT_GET(DT_NODELABEL(my_sequencer));


static uint8_t __attribute__((__section__("DTCM"))) adc_thread_stack[AIN_TASK_STACK_SIZE];
static struct __attribute__((__section__("DTCM"))) k_thread thread_data;


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

    k_thread_create(&thread_data, (k_thread_stack_t *)adc_thread_stack, AIN_TASK_STACK_SIZE,

                    func, NULL, NULL, NULL, AIN_TASK_PRIORITY, 0, K_NO_WAIT);
    return 0;
}